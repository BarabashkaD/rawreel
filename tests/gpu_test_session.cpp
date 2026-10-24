// The Session on a real GPU (design M3 v2 §8.1 rawreel gpu row): one stream plays as display
// images, an injected device loss releases the context and reopen() continues, and a small-budget
// store evicts while GPU work is in flight.
#include <doctest/doctest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "core/session.h"
#include "core/session_types.h"
#include "core/timer.h"
#include "dvv/base/task.h"
#include "dvv/base/uri.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/gpu_registry.h"
#include "dvv/gpu/resources.h"
#include "dvv/gpu/testing/fault_injection.h"
#include "dvv/media/display.h"
#include "dvv/media/frame.h"
#include "dvv/media/frame_store.h"
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
    if (frames.count != 1 || !frames.views[0] || !*frames.views[0]) {
        return false;
    }
    const dvv::media::ReadLease& lease = *frames.views[0];
    if (lease.view().format != dvv::media::displayFormat()) {
        return false;
    }
    const auto* image = std::get_if<dvv::media::VkImageB>(&lease.binding());
    return image != nullptr && image->planeCount == 1 && image->planes[0] != nullptr &&
           image->planes[0]->format() == VK_FORMAT_R16G16B16A16_SFLOAT;
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
        options.gpus = environment.registry;
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
    // Shuts the Session down and releases it; afterwards no context may live and no validation
    // message may have been counted.
    void finish() {
        if (!session) {
            return;
        }
        dvv::syncWait(session->shutdown());
        events->setStatusProbe({});
        session.reset();
        CHECK(waitUntil([] { return liveCountOf("ExecutionContext") == 0; }, 5s));
        CHECK(environment.registry->validationMessageCount() == 0);
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
};

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
            [registry = env.registry, cpu = env.cpu, io = env.io, observed](
                std::optional<dvv::gpu::DeviceId> /*device*/) -> dvv::Result<rawreel::MediaStack> {
                std::optional<dvv::gpu::DeviceId> id;
                for (const dvv::gpu::DeviceId candidate : registry->rankedDevices()) {
                    if (registry->devices()[candidate.index()].usable()) {
                        id = candidate;
                        break;
                    }
                }
                if (!id) {
                    return dvv::makeError(dvv::Errc::Unsupported, "no usable GPU device");
                }
                DVV_TRY(std::shared_ptr<dvv::gpu::ExecutionContext> context,
                        registry->createContext(*id, {}));
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