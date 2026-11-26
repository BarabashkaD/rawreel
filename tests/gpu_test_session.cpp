// The Session on a real GPU (design M3 v2 §8.1 rawreel gpu row): one stream plays as display
// images, an injected device loss releases the context and reopen() continues, a small-budget
// store evicts while GPU work is in flight, and a hidden component set reaches the shown view and
// its image.
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "core/session.h"
#include "core/session_types.h"
#include "core/timer.h"
#include "dvv/base/error.h"
#include "dvv/base/task.h"
#include "dvv/base/uri.h"
#include "dvv/gpu/context_vulkan.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/gpu_instance.h"
#include "dvv/gpu/image.h"
#include "dvv/gpu/testing/fault_injection.h"
#include "dvv/media/display.h"
#include "dvv/media/display_vulkan.h"
#include "dvv/media/frame.h"
#include "dvv/media/frame_store.h"
#include "dvv/media/frame_vulkan.h"
#include "dvv/media/types.h"
#include "gpu_support.h"
#include "session_fixture.h"

using rawreel::FrameSet;
using rawreel::SessionStatus;
using rawreel::testing::liveCountOf;
using rawreel::testing::RecordingEvents;
using rawreel::testing::RecordingSink;
using rawreel::testing::SessionEvent;
using rawreel::testing::ShownFrame;
using Kind = rawreel::testing::SessionEvent::Kind;
using namespace std::chrono_literals;

namespace {

constexpr std::string_view S = "synthetic:gradient?w=64&h=48&fmt=yuv420p10le&frames=120";
constexpr std::int64_t S_FRAMES = 120;

// The shown views are ToDisplay's images: one RGBA16F storage image per frame.
bool isDisplayImage(const FrameSet& frames) {
    if (frames.count != 1 || !frames.views[0]) {
        return false;
    }
    const dvv::media::ReadLease& lease = frames.views[0];
    if (lease.view().format != dvv::media::displayFormat()) {
        return false;
    }
    const dvv::gpu::GpuImage* const image = dvv::media::vulkan::image(lease, 0);
    return image != nullptr && dvv::media::vulkan::image(lease, 1) == nullptr &&
           dvv::gpu::vulkanFormat(*image) == dvv::media::DISPLAY_IMAGE_FORMAT;
}

bool waitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

bool hasShow(const std::vector<ShownFrame>& history, std::int64_t index,
             std::optional<std::uint64_t> epoch = std::nullopt) {
    return std::ranges::any_of(history, [&](const ShownFrame& shown) {
        return shown.index == index && (!epoch || shown.epoch == *epoch);
    });
}

using StackMaker =
    std::function<rawreel::StackFactory(const rawreel::testing::GpuTestEnvironment&)>;

// The store of the latest stack, written on the stack factory's thread and read by the test.
class ObservedStore {
public:
    void set(const std::shared_ptr<dvv::media::FrameStore>& store) {
        const std::lock_guard lock(mutex_);
        store_ = store;
    }
    [[nodiscard]] std::shared_ptr<dvv::media::FrameStore> lock() const {
        const std::lock_guard lock(mutex_);
        return store_.lock();
    }
    // The latest store's stats; the store is not kept (the Session must be able to release it).
    [[nodiscard]] dvv::media::StoreStats stats() const {
        const std::shared_ptr<dvv::media::FrameStore> store = lock();
        REQUIRE(store);
        return store->stats();
    }

private:
    mutable std::mutex mutex_;
    std::weak_ptr<dvv::media::FrameStore> store_;
};

// gpuStackFactory (the Session's default stack), with every made store kept in `observed`.
StackMaker observingStack(const std::shared_ptr<ObservedStore>& observed) {
    return [observed](const rawreel::testing::GpuTestEnvironment& env) {
        return rawreel::StackFactory(
            [inner = rawreel::gpuStackFactory(env.gpuInstance, env.cpu, env.io, false),
             observed](std::optional<dvv::gpu::DeviceId> device) mutable
                -> dvv::Result<rawreel::MediaStack> {
                DVV_TRY(rawreel::MediaStack stack, inner(device));
                observed->set(stack.store);
                return stack;
            });
    };
}

// The runs of the converter `name` ("Produce", "GpuUnpack", "ToDisplay", ...); 0 when absent.
std::uint64_t runsOf(const dvv::media::StoreStats& stats, std::string_view name) {
    for (const dvv::media::ConverterStats& converter : stats.converters) {
        if (converter.name == name) {
            return converter.runs;
        }
    }
    return 0;
}

// A share of the latest shown first view, kept by GpuSession::keepShown's inspector.
struct KeptLease {
    mutable std::mutex mutex;
    dvv::media::ReadLease lease;
};

// A Session on the real clock and timer with a recording sink (inspecting every show) and event
// receiver. Without `stack_maker` the Session's makeStack stays empty, which selects
// gpuStackFactory.
class GpuSession {
public:
    explicit GpuSession(const StackMaker& stack_maker = {}) {
        sink->setInspector(isDisplayImage);
        auto timer = rawreel::makeThreadTimer();
        REQUIRE(timer);
        rawreel::SessionOptions options;
        options.components = environment.components;
        options.cpu = environment.cpu;
        options.io = environment.io;
        options.gpus = environment.gpuInstance;
        options.device = std::nullopt;
        options.presentation = false;
        options.inflight = 4;
        options.clock = rawreel::steadyClock();
        options.timer = std::move(*timer);
        options.sink = sink;
        options.events = events;
        if (stack_maker) {
            options.makeStack = stack_maker(environment);
        }
        auto made = rawreel::Session::create(std::move(options));
        REQUIRE_MESSAGE(made, (made ? std::string() : made.error().describe()));
        session = std::move(*made);
        events->setStatusProbe([raw = session.get()] { return raw->status(); });
    }
    GpuSession(const GpuSession&) = delete;
    GpuSession& operator=(const GpuSession&) = delete;
    GpuSession(GpuSession&&) = delete;
    GpuSession& operator=(GpuSession&&) = delete;
    ~GpuSession() {
        try {
            finish();
        } catch (...) {  // a failed check was recorded before it threw
            std::fputs("GpuSession: finish() threw\n", stderr);
        }
    }

    void open(std::string_view uri) {
        auto parsed = dvv::parseUri(uri);
        REQUIRE(parsed);
        session->open(0, dvv::media::SourceRequest{.uri = std::move(*parsed), .hint = {}});
    }
    [[nodiscard]] bool waitStatus(const std::function<bool(const SessionStatus&)>& predicate,
                                  std::chrono::milliseconds timeout) {
        return waitUntil([&] { return predicate(session->status()); }, timeout);
    }
    // From now on every show keeps a share of its first view (the latest one replaces the one
    // before), until releaseShown() or finish(). The display image check stays.
    void keepShown() {
        sink->setInspector([kept = kept_](const FrameSet& frames) {
            if (frames.count >= 1 && frames.views[0]) {
                dvv::media::ReadLease share = frames.views[0].share();
                const std::lock_guard lock(kept->mutex);
                kept->lease = std::move(share);
            }
            return isDisplayImage(frames);
        });
    }
    // Drops the kept share (its store and context may then go).
    void releaseShown() {
        dvv::media::ReadLease released;
        const std::lock_guard lock(kept_->mutex);
        released = std::move(kept_->lease);
    }
    // A share of the kept view; empty before the first show after keepShown().
    [[nodiscard]] dvv::media::ReadLease shownLease() const {
        const std::lock_guard lock(kept_->mutex);
        return kept_->lease ? kept_->lease.share() : dvv::media::ReadLease{};
    }
    // Waits until the kept view hides `hidden`.
    [[nodiscard]] bool waitShownHidden(dvv::media::HiddenComponents hidden,
                                       std::chrono::milliseconds timeout) const {
        return waitUntil(
            [&] {
                const dvv::media::ReadLease lease = shownLease();
                return lease && lease.view().hidden == hidden;
            },
            timeout);
    }
    // The kept view's display image (RGBA16F) as floats, four per texel.
    [[nodiscard]] std::vector<float> readShown() const {
        const dvv::media::ReadLease lease = shownLease();
        REQUIRE(lease);
        dvv::gpu::GpuImage* const image = dvv::media::vulkan::image(lease, 0);
        REQUIRE(image != nullptr);
        const std::shared_ptr<dvv::gpu::ExecutionContext> context = events->lastContext().lock();
        REQUIRE(context);
        // Not owning: the lease keeps the image while the copy runs.
        const std::shared_ptr<dvv::gpu::GpuImage> alias(std::shared_ptr<void>(), image);
        auto bytes = rawreel::testing::readImage(*context, *environment.cpu, alias, 8);
        REQUIRE_MESSAGE(bytes, (bytes ? std::string() : bytes.error().describe()));
        std::vector<float> texels(bytes->size() / sizeof(std::uint16_t));
        for (std::size_t i = 0; i < texels.size(); ++i) {
            std::uint16_t half = 0;
            std::memcpy(&half, bytes->data() + (i * sizeof(half)), sizeof(half));
            texels[i] = dvv::media::halfToFloat(half);
        }
        return texels;
    }
    // Shuts the Session down and releases it; afterwards no context may live and no validation
    // message may have been counted.
    void finish() {
        if (!session) {
            return;
        }
        releaseShown();
        dvv::syncWait(session->shutdown());
        events->setStatusProbe({});
        session.reset();
        CHECK(waitUntil([] { return liveCountOf("ExecutionContext") == 0; }, 5s));
        CHECK(environment.gpuInstance->validationMessageCount() == 0);
    }

    // What happened so far, for failure messages: the events and the last shows.
    [[nodiscard]] std::string summary() const {
        std::string text =
            std::format("status: position {}, playing {}, epoch {}, inflight {}\n",
                        session->status().player.position, session->status().player.playing,
                        session->status().epoch, session->status().inflight);
        for (const SessionEvent& event : events->events()) {
            text += std::format("event {} kind {} epoch {} streams {} {}\n", event.seq,
                                static_cast<int>(event.kind), event.epoch, event.streams.size(),
                                event.error ? event.error->describe() : std::string());
        }
        const std::vector<ShownFrame> history = sink->history();
        const std::size_t first = history.size() > 8 ? history.size() - 8 : 0;
        text += std::format("{} shows; last:", history.size());
        for (std::size_t i = first; i < history.size(); ++i) {
            text += std::format(" {}@{}{}", history[i].index, history[i].epoch,
                                history[i].inspected ? "" : "!");
        }
        return text;
    }

    rawreel::testing::GpuTestEnvironment environment = rawreel::testing::makeGpuTestEnvironment();
    rawreel::testing::Sequence sequence = std::make_shared<std::atomic<std::uint64_t>>(0);
    std::shared_ptr<RecordingSink> sink = std::make_shared<RecordingSink>(sequence);
    std::shared_ptr<RecordingEvents> events = std::make_shared<RecordingEvents>(sequence);
    std::unique_ptr<rawreel::Session> session;

private:
    std::shared_ptr<KeptLease> kept_ = std::make_shared<KeptLease>();
};

constexpr dvv::media::HiddenComponents SHOW_ALL{};
constexpr dvv::media::HiddenComponents FIRST_OFF{.components = {true, false, false},
                                                 .chroma = dvv::media::ChromaFill::Mid};
constexpr dvv::media::HiddenComponents SECOND_OFF{.components = {false, true, false},
                                                  .chroma = dvv::media::ChromaFill::Mid};
constexpr dvv::media::HiddenComponents CHROMA_MID{.components = {false, true, true},
                                                  .chroma = dvv::media::ChromaFill::Mid};
constexpr dvv::media::HiddenComponents CHROMA_ZERO{.components = {false, true, true},
                                                   .chroma = dvv::media::ChromaFill::Zero};

// Opens `uri` in a session that keeps its shown view, and waits until frame 0 is shown and
// nothing is in flight any more.
void openSettled(GpuSession& g, std::string_view uri) {
    g.keepShown();
    g.open(uri);
    REQUIRE_MESSAGE(
        g.sink->waitFor([](const std::vector<ShownFrame>& history) { return hasShow(history, 0); },
                        30s),
        g.summary());
    REQUIRE_MESSAGE(g.waitShownHidden(SHOW_ALL, 30s), g.summary());
    REQUIRE_MESSAGE(g.waitStatus([](const SessionStatus& s) { return s.inflight == 0; }, 30s),
                    g.summary());
}

// Sets `hidden` and waits until the shown view hides it and nothing is in flight any more.
void setSettled(GpuSession& g, dvv::media::HiddenComponents hidden) {
    g.session->setHiddenComponents(hidden);
    REQUIRE_MESSAGE(g.waitShownHidden(hidden, 30s), g.summary());
    REQUIRE_MESSAGE(g.waitStatus([](const SessionStatus& s) { return s.inflight == 0; }, 30s),
                    g.summary());
}

}  // namespace

TEST_CASE("gpu session: one stream plays as display images") {
    GpuSession g;
    g.open(S);
    // play() before the open job attached the stream would find no player.
    REQUIRE_MESSAGE(
        g.sink->waitFor([](const std::vector<ShownFrame>& history) { return hasShow(history, 0); },
                        30s),
        g.summary());
    g.session->play();
    REQUIRE_MESSAGE(
        g.sink->waitFor([](const std::vector<ShownFrame>& history) { return hasShow(history, 30); },
                        30s),
        g.summary());
    const std::vector<ShownFrame> history = g.sink->history();
    for (const ShownFrame& shown : history) {
        CHECK(shown.count == 1);
        CHECK(shown.indicesMatch);
        CHECK(shown.inspected);
    }
    const SessionStatus status = g.session->status();
    CHECK(status.epoch == 1);
    CHECK(status.gpuAvailable);
    CHECK(g.events->of(Kind::Error).empty());
    g.finish();
}

TEST_CASE("gpu session: an injected device loss reports an error and reopen continues") {
    GpuSession g;
    g.open(S);
    // play() before the open job attached the stream would find no player.
    REQUIRE_MESSAGE(
        g.sink->waitFor([](const std::vector<ShownFrame>& history) { return hasShow(history, 0); },
                        30s),
        g.summary());
    g.session->play();
    REQUIRE_MESSAGE(
        g.sink->waitFor([](const std::vector<ShownFrame>& history) { return hasShow(history, 10); },
                        30s),
        g.summary());
    {
        const std::shared_ptr<dvv::gpu::ExecutionContext> context = g.events->lastContext().lock();
        REQUIRE(context);
        dvv::gpu::testing::injectDeviceLost(*context);
    }

    REQUIRE(g.events->waitFor(
        [](const std::vector<SessionEvent>& events) {
            return std::ranges::any_of(events, [](const SessionEvent& e) {
                return e.kind == Kind::Error && e.error && e.error->code == dvv::Errc::DeviceLost;
            });
        },
        5s));
    REQUIRE(g.events->waitFor(
        [](const std::vector<SessionEvent>& events) {
            return std::ranges::any_of(events, [](const SessionEvent& e) {
                return e.kind == Kind::StackChanged && e.expired && e.epoch == 0;
            });
        },
        5s));
    CHECK(g.waitStatus([](const SessionStatus& s) { return !s.gpuAvailable; }, 5s));
    const std::vector<SessionEvent> errors = g.events->of(Kind::Error);
    REQUIRE(errors.size() == 1);
    REQUIRE(errors[0].status);
    const std::int64_t kept = errors[0].status.value_or(SessionStatus{}).player.position;
    CHECK(kept >= 0);
    std::optional<std::int64_t> last_shown;
    for (const ShownFrame& shown : g.sink->history()) {
        if (shown.epoch == 1) {
            last_shown = shown.index;
        }
    }
    REQUIRE(last_shown);
    CHECK(kept == last_shown.value_or(-2));
    // No destructor joined its own thread (that would have aborted the run).
    CHECK(waitUntil([] { return liveCountOf("ExecutionContext") == 0; }, 5s));

    g.session->reopen();
    CHECK(g.waitStatus(
        [](const SessionStatus& s) { return s.epoch == 2 && liveCountOf("ExecutionContext") == 1; },
        30s));
    REQUIRE_MESSAGE(
        g.sink->waitFor(
            [kept](const std::vector<ShownFrame>& history) { return hasShow(history, kept, 2); },
            30s),
        g.summary());
    CHECK_FALSE(g.session->status().player.playing);

    g.session->play();
    REQUIRE_MESSAGE(g.sink->waitFor(
                        [kept](const std::vector<ShownFrame>& history) {
                            return hasShow(history, kept + 10, 2);
                        },
                        30s),
                    g.summary());
    for (const ShownFrame& shown : g.sink->history()) {
        if (shown.epoch == 2) {
            CHECK(shown.inspected);
            CHECK(shown.indicesMatch);
        }
    }
    CHECK(g.events->of(Kind::Error).size() == 1);
    g.finish();
}

TEST_CASE("gpu session: a small-budget store evicts under in-flight GPU work") {
    // Three frames of S per tier (64x48 yuv420p10le): staging 3 · 9216 bytes; device 3 · (9216
    // original + 9216 planes + 24576 display image) bytes.
    constexpr std::size_t STAGING_BUDGET = 27648;
    constexpr std::size_t DEVICE_BUDGET = 129024;
    auto observed = std::make_shared<std::weak_ptr<dvv::media::FrameStore>>();
    const StackMaker small_budget = [observed](const rawreel::testing::GpuTestEnvironment& env) {
        return rawreel::StackFactory(
            [gpu_instance = env.gpuInstance, cpu = env.cpu, io = env.io, observed](
                std::optional<dvv::gpu::DeviceId> /*device*/) -> dvv::Result<rawreel::MediaStack> {
                std::optional<dvv::gpu::DeviceId> id;
                for (const dvv::gpu::DeviceId candidate : gpu_instance->rankedDevices()) {
                    if (gpu_instance->devices()[candidate.index()].usable()) {
                        id = candidate;
                        break;
                    }
                }
                if (!id) {
                    return dvv::makeError(dvv::Errc::Unsupported, "no usable GPU device");
                }
                DVV_TRY(std::shared_ptr<dvv::gpu::ExecutionContext> context,
                        gpu_instance->createContext(*id, {}));
                DVV_TRY(std::shared_ptr<dvv::media::FrameStore> store,
                        dvv::media::FrameStore::create(
                            dvv::media::FrameStoreOptions{.cpu = cpu,
                                                          .io = io,
                                                          .gpu = context,
                                                          .stagingBudget = STAGING_BUDGET,
                                                          .deviceBudget = DEVICE_BUDGET}));
                const std::optional<dvv::media::Domain> domain = store->deviceDomain();
                if (!domain) {
                    return dvv::makeError(dvv::Errc::Internal,
                                          "the GPU store has no device domain");
                }
                *observed = store;
                rawreel::MediaStack stack;
                stack.context = std::move(context);
                stack.viewFor = [domain = *domain](const dvv::media::FrameDesc& /*desc*/) {
                    return dvv::media::displayView(domain);
                };
                stack.store = std::move(store);
                return stack;
            });
    };
    GpuSession g(small_budget);
    g.open(S);
    // play() before the open job attached the stream would find no player.
    REQUIRE_MESSAGE(
        g.sink->waitFor([](const std::vector<ShownFrame>& history) { return hasShow(history, 0); },
                        30s),
        g.summary());
    g.session->play();
    REQUIRE_MESSAGE(
        g.sink->waitFor(
            [](const std::vector<ShownFrame>& history) { return hasShow(history, S_FRAMES - 1); },
            60s),
        g.summary());
    const std::vector<ShownFrame> history = g.sink->history();
    std::vector<std::int64_t> shown;
    for (const ShownFrame& frame : history) {
        shown.push_back(frame.index);
        CHECK(frame.inspected);
        CHECK(frame.indicesMatch);
    }
    std::vector<std::int64_t> expected;
    expected.reserve(static_cast<std::size_t>(S_FRAMES));
    for (std::int64_t k = 0; k < S_FRAMES; ++k) {
        expected.push_back(k);
    }
    CHECK(shown == expected);
    CHECK(g.events->of(Kind::Error).empty());
    {
        const std::shared_ptr<dvv::media::FrameStore> store = observed->lock();
        REQUIRE(store);
        const std::uint64_t evictions = store->stats().evictions;
        CAPTURE(evictions);
        CHECK(evictions > 0);
    }
    g.finish();
}

TEST_CASE("gpu session: hidden set toggle in pause converts the frame and the window again") {
    auto observed = std::make_shared<ObservedStore>();
    GpuSession g(observingStack(observed));
    openSettled(g, S);
    const dvv::media::StoreStats before = observed->stats();
    const std::size_t shows = g.sink->history().size();
    const std::uint32_t window = g.session->status().readyAhead;
    CHECK(window == 8);  // the largest window: a small frame

    setSettled(g, FIRST_OFF);
    const dvv::media::StoreStats after = observed->stats();
    CHECK(g.shownLease().view().hidden == FIRST_OFF);
    const std::uint64_t to_display = runsOf(after, "ToDisplay") - runsOf(before, "ToDisplay");
    CAPTURE(to_display);
    // One pass for the shown frame, then one per window frame (the read-ahead for play).
    CHECK(to_display == 1 + window);
    CHECK(to_display == after.misses - before.misses);  // one ToDisplay per new display view
    CHECK(g.session->status().readyAhead == window);
    CHECK(runsOf(after, "GpuUnpack") == runsOf(before, "GpuUnpack"));
    CHECK(runsOf(after, "Produce") == runsOf(before, "Produce"));
    const std::vector<ShownFrame> history = g.sink->history();
    REQUIRE(history.size() == shows + 1);
    CHECK(history.back().index == 0);
    CHECK(history.back().inspected);
    CHECK(g.session->status().player.position == 0);
    CHECK(g.events->of(Kind::Error).empty());
    g.finish();
}

TEST_CASE("gpu session: hidden set A, B, A ends in a hit") {
    auto observed = std::make_shared<ObservedStore>();
    GpuSession g(observingStack(observed));
    openSettled(g, S);
    setSettled(g, FIRST_OFF);
    setSettled(g, SECOND_OFF);
    const dvv::media::StoreStats before = observed->stats();

    setSettled(g, FIRST_OFF);
    const dvv::media::StoreStats after = observed->stats();
    CHECK(runsOf(after, "ToDisplay") == runsOf(before, "ToDisplay"));
    CHECK(after.hits > before.hits);
    CHECK(after.misses == before.misses);
    CHECK(g.sink->history().back().index == 0);
    CHECK(g.events->of(Kind::Error).empty());
    g.finish();
}

TEST_CASE("gpu session: hidden set burst ends on the last set") {
    auto observed = std::make_shared<ObservedStore>();
    GpuSession g(observingStack(observed));
    openSettled(g, "synthetic:gradient?w=64&h=48&fmt=yuv420p&frames=120");
    const std::uint64_t leases = observed->stats().leases;

    for (int k = 0; k < 10; ++k) {
        g.session->setHiddenComponents(k % 2 == 0 ? FIRST_OFF : CHROMA_ZERO);
    }
    // A later command: the burst has run before the wait below can see a CHROMA_ZERO show.
    g.session->setLoop(true);
    REQUIRE_MESSAGE(g.waitStatus([](const SessionStatus& s) { return s.player.loop; }, 30s),
                    g.summary());
    REQUIRE_MESSAGE(g.waitShownHidden(CHROMA_ZERO, 30s), g.summary());
    REQUIRE_MESSAGE(g.waitStatus([](const SessionStatus& s) { return s.inflight == 0; }, 30s),
                    g.summary());
    CHECK(g.shownLease().view().hidden == CHROMA_ZERO);
    CHECK(observed->stats().leases == leases);
    for (const ShownFrame& shown : g.sink->history()) {
        CHECK(shown.index == 0);
        CHECK(shown.inspected);
    }
    CHECK(g.session->status().player.position == 0);
    CHECK(g.events->of(Kind::Error).empty());
    g.finish();
}

TEST_CASE("gpu session: hidden set survives a device loss") {
    GpuSession g;
    openSettled(g, S);
    setSettled(g, FIRST_OFF);
    {
        const std::shared_ptr<dvv::gpu::ExecutionContext> context = g.events->lastContext().lock();
        REQUIRE(context);
        dvv::gpu::testing::injectDeviceLost(*context);
    }
    // In pause nothing runs on the GPU: the next set makes the work that finds the loss.
    g.session->setHiddenComponents(SECOND_OFF);
    REQUIRE(g.events->waitFor(
        [](const std::vector<SessionEvent>& events) {
            return std::ranges::any_of(events, [](const SessionEvent& e) {
                return e.kind == Kind::StackChanged && e.expired && e.epoch == 0;
            });
        },
        5s));
    g.releaseShown();  // the lost context may go

    g.session->reopen();
    REQUIRE_MESSAGE(
        g.sink->waitFor(
            [](const std::vector<ShownFrame>& history) { return hasShow(history, 0, 2); }, 30s),
        g.summary());
    REQUIRE_MESSAGE(g.waitShownHidden(SECOND_OFF, 30s), g.summary());
    CHECK(g.sink->history().back().epoch == 2);
    const std::vector<SessionEvent> errors = g.events->of(Kind::Error);
    REQUIRE(errors.size() == 1);
    const auto& error = errors[0].error;
    const dvv::Errc code = error ? error->code : dvv::Errc::Internal;
    CHECK(code == dvv::Errc::DeviceLost);
    g.finish();
}

TEST_CASE("gpu session: hidden set reaches the shown image") {
    constexpr std::size_t CHANNELS = 4;  // RGBA
    GpuSession g;
    SUBCASE("yuv420p without chroma is grey") {
        openSettled(g, "synthetic:gradient?w=64&h=48&fmt=yuv420p&frames=4");
        // The gradient's Cb and Cr are a quarter of the range apart, so it has colour everywhere.
        const std::vector<float> shown = g.readShown();
        float spread = 0.0F;
        for (std::size_t i = 0; i + CHANNELS <= shown.size(); i += CHANNELS) {
            spread = std::max(
                {spread, std::abs(shown[i] - shown[i + 1]), std::abs(shown[i + 1] - shown[i + 2])});
        }
        CHECK(spread > 0.05F);

        setSettled(g, CHROMA_MID);
        const std::vector<float> grey = g.readShown();
        REQUIRE(grey.size() == shown.size());
        float grey_spread = 0.0F;
        for (std::size_t i = 0; i + CHANNELS <= grey.size(); i += CHANNELS) {
            grey_spread = std::max({grey_spread, std::abs(grey[i] - grey[i + 1]),
                                    std::abs(grey[i + 1] - grey[i + 2])});
        }
        CHECK(grey_spread < 2e-3F);
    }
    SUBCASE("rgb24 without red has no red") {
        openSettled(g, "synthetic:gradient?w=64&h=48&fmt=rgb24&frames=4");
        const std::vector<float> shown = g.readShown();
        float red = 0.0F;
        for (std::size_t i = 0; i < shown.size(); i += CHANNELS) {
            red = std::max(red, shown[i]);
        }
        CHECK(red > 0.05F);

        setSettled(g, FIRST_OFF);
        const std::vector<float> hidden = g.readShown();
        REQUIRE(hidden.size() == shown.size());
        float hidden_red = 0.0F;
        for (std::size_t i = 0; i < hidden.size(); i += CHANNELS) {
            hidden_red = std::max(hidden_red, std::abs(hidden[i]));
        }
        CHECK(hidden_red < 1e-3F);
    }
    CHECK(g.events->of(Kind::Error).empty());
    g.finish();
}
