#include "session_fixture.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "core/timer.h"
#include "dvv/base/lifetime.h"
#include "dvv/base/task.h"
#include "dvv/base/uri.h"
#include "dvv/media/frame_store.h"
#include "dvv/registry/capabilities.h"
#include "test_source_provider.h"

namespace rawreel::testing {

Clock::time_point FakeClock::now() const {
    std::lock_guard lock(mutex_);
    return now_;
}

void FakeClock::advance(Clock::duration d) {
    std::lock_guard lock(mutex_);
    now_ += d;
}

void FakeTimer::schedule(Clock::time_point when, std::move_only_function<void()> callback) {
    std::move_only_function<void()> replaced;
    std::lock_guard lock(mutex_);
    replaced = std::exchange(callback_, std::move(callback));
    when_ = when;
}

void FakeTimer::cancel() {
    std::move_only_function<void()> dropped;
    std::lock_guard lock(mutex_);
    dropped = std::exchange(callback_, {});
    when_.reset();
}

void FakeTimer::advance(Clock::duration d) {
    std::lock_guard lock(mutex_);
    clock_->advance(d);
    if (when_ && *when_ <= clock_->now()) {
        std::move_only_function<void()> callback = std::exchange(callback_, {});
        when_.reset();
        if (callback) {
            callback();  // posts the tick
        }
    }
}

std::optional<Clock::time_point> FakeTimer::pending() const {
    std::lock_guard lock(mutex_);
    return when_;
}

void RecordingSink::show(FrameSet frames) {
    ShownFrame shown;
    shown.seq = sequence_->fetch_add(1) + 1;
    shown.index = frames.index;
    shown.count = frames.count;
    shown.epoch = frames.epoch;
    shown.indicesMatch = frames.count > 0;
    std::function<bool(const FrameSet&)> inspect;
    {
        std::lock_guard lock(mutex_);
        inspect = inspect_;
    }
    shown.inspected = !inspect || inspect(frames);
    for (std::uint32_t s = 0; s < frames.count && s < frames.views.size(); ++s) {
        const auto& view = frames.views.at(s);
        if (!view || !*view) {
            shown.indicesMatch = false;
            continue;
        }
        if (s == 0) {
            shown.origin = view->key().origin;
        }
        if (view->key().index != frames.index) {
            shown.indicesMatch = false;
        }
    }
    std::lock_guard lock(mutex_);
    history_.push_back(shown);
    latest_ = std::move(frames);
    changed_.notify_all();
}

void RecordingSink::clear() {
    std::optional<FrameSet> dropped;
    std::lock_guard lock(mutex_);
    dropped = std::exchange(latest_, std::nullopt);
    ++clears_;
    changed_.notify_all();
}

void RecordingSink::setInspector(std::function<bool(const FrameSet&)> inspect) {
    std::lock_guard lock(mutex_);
    inspect_ = std::move(inspect);
}

bool RecordingSink::waitShown(std::int64_t index, std::chrono::milliseconds timeout) {
    return waitFor(
        [index](const std::vector<ShownFrame>& history) {
            return !history.empty() && history.back().index == index;
        },
        timeout);
}

bool RecordingSink::waitFor(const std::function<bool(const std::vector<ShownFrame>&)>& predicate,
                            std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout, [&] { return predicate(history_); });
}

std::vector<ShownFrame> RecordingSink::history() const {
    std::lock_guard lock(mutex_);
    return history_;
}

std::uint64_t RecordingSink::clears() const {
    std::lock_guard lock(mutex_);
    return clears_;
}

bool RecordingSink::holdsFrame() const {
    std::lock_guard lock(mutex_);
    return latest_.has_value();
}

void RecordingEvents::record(SessionEvent event) {
    event.seq = sequence_->fetch_add(1) + 1;
    std::lock_guard lock(mutex_);
    events_.push_back(std::move(event));
    changed_.notify_all();
}

void RecordingEvents::streamsChanged(std::vector<StreamInfo> streams) {
    SessionEvent event;
    event.kind = SessionEvent::Kind::StreamsChanged;
    event.streams = std::move(streams);
    record(std::move(event));
}

void RecordingEvents::formatNeeded(std::uint32_t slot, dvv::media::SourceRequest request,
                                   dvv::media::Ambiguous candidates, std::string reason) {
    SessionEvent event;
    event.kind = SessionEvent::Kind::FormatNeeded;
    event.slot = slot;
    event.request = std::move(request);
    event.candidates = std::move(candidates);
    event.reason = std::move(reason);
    record(std::move(event));
}

void RecordingEvents::stackChanged(std::weak_ptr<dvv::gpu::ExecutionContext> context,
                                   std::uint64_t epoch) {
    SessionEvent event;
    event.kind = SessionEvent::Kind::StackChanged;
    event.expired = context.expired();
    event.epoch = epoch;
    {
        std::lock_guard lock(mutex_);
        context_ = std::move(context);
    }
    record(std::move(event));
}

void RecordingEvents::error(dvv::Error error) {
    SessionEvent event;
    event.kind = SessionEvent::Kind::Error;
    event.error = std::move(error);
    std::function<SessionStatus()> probe;
    {
        std::lock_guard lock(mutex_);
        probe = probe_;
    }
    if (probe) {
        event.status = probe();
    }
    record(std::move(event));
}

void RecordingEvents::setStatusProbe(std::function<SessionStatus()> probe) {
    std::lock_guard lock(mutex_);
    probe_ = std::move(probe);
}

bool RecordingEvents::waitFor(
    const std::function<bool(const std::vector<SessionEvent>&)>& predicate,
    std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout, [&] { return predicate(events_); });
}

std::vector<SessionEvent> RecordingEvents::events() const {
    std::lock_guard lock(mutex_);
    return events_;
}

std::vector<SessionEvent> RecordingEvents::of(SessionEvent::Kind kind) const {
    std::vector<SessionEvent> matching;
    std::lock_guard lock(mutex_);
    for (const SessionEvent& event : events_) {
        if (event.kind == kind) {
            matching.push_back(event);
        }
    }
    return matching;
}

std::weak_ptr<dvv::gpu::ExecutionContext> RecordingEvents::lastContext() const {
    std::lock_guard lock(mutex_);
    return context_;
}

std::size_t countKind(const std::vector<SessionEvent>& events, SessionEvent::Kind kind) {
    std::size_t count = 0;
    for (const SessionEvent& event : events) {
        if (event.kind == kind) {
            ++count;
        }
    }
    return count;
}

SessionFixture::SessionFixture() {
    start(false);
}

SessionFixture::SessionFixture(RealTime) {
    start(true);
}

SessionFixture::SessionFixture(dvv::platform::FileReadMode file_reads) : fileReads(file_reads) {
    start(false);
}

void SessionFixture::start(bool real_time) {
    resetShots();
    releaseGate(STACK_GATE);
    auto cpu_pool = dvv::ExecutorFactory::makeThreadPool(4);
    REQUIRE(cpu_pool);
    cpu = std::move(*cpu_pool);
    auto io_pool = dvv::ExecutorFactory::makeThreadPool(4);
    REQUIRE(io_pool);
    io = std::move(*io_pool);
    components = dvv::ComponentRegistry::create(dvv::Capabilities{});
    REQUIRE(components);
    if (real_time) {
        realClock = steadyClock();
        auto made_timer = makeThreadTimer();
        REQUIRE(made_timer);
        realTimer = std::move(*made_timer);
    } else {
        clock = std::make_shared<FakeClock>();
        timer = std::make_shared<FakeTimer>(clock);
    }
    sequence = std::make_shared<std::atomic<std::uint64_t>>(0);
    sink = std::make_shared<RecordingSink>(sequence);
    events = std::make_shared<RecordingEvents>(sequence);
    auto made = Session::create(options());
    REQUIRE_MESSAGE(made, (made ? std::string() : made.error().describe()));
    session = std::move(*made);
    events->setStatusProbe([raw = session.get()] { return raw->status(); });
}

SessionFixture::~SessionFixture() {
    try {
        teardown();
    } catch (...) {  // a failed check was recorded before it threw
        std::fputs("SessionFixture: the teardown threw\n", stderr);
    }
}

void SessionFixture::teardown() {
    if (session && !shutDown) {
        dvv::syncWait(session->shutdown());
    }
    if (events) {
        events->setStatusProbe({});  // it points at the Session
    }
    session.reset();
    sink.reset();
    events.reset();
    timer.reset();
    clock.reset();
    realTimer.reset();
    realClock.reset();
    components.reset();
    cpu.reset();
    io.reset();
    const auto deadline = std::chrono::steady_clock::now() + WAIT_TIMEOUT;
    while (dvv::LifetimeRegistry::instance().liveCount() != 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(dvv::LifetimeRegistry::instance().liveCount() == 0);
}

SessionOptions SessionFixture::options() {
    SessionOptions options;
    options.components = components;
    options.cpu = cpu;
    options.io = io;
    options.clock = realClock ? realClock : std::shared_ptr<IClock>(clock);
    options.timer = realTimer ? realTimer : std::shared_ptr<ITimer>(timer);
    options.sink = sink;
    options.events = events;
    options.fileReads = fileReads;
    options.makeStack = [log = stackLog, cpu_pool = cpu, io_pool = io](
                            std::optional<dvv::gpu::DeviceId> device) -> dvv::Result<MediaStack> {
        {
            std::lock_guard lock(log->mutex);
            ++log->calls;
            log->devices.push_back(device);
        }
        const int running = log->running.fetch_add(1) + 1;
        int seen = log->maxRunning.load();
        while (seen < running && !log->maxRunning.compare_exchange_weak(seen, running)) {
        }
        const auto build = [&]() -> dvv::Result<MediaStack> {
            if (!passGate(STACK_GATE, std::chrono::seconds(10))) {
                return dvv::makeError(dvv::Errc::Internal, "gate makestack timed out");
            }
            int fail = log->failNext.load();
            while (fail > 0 && !log->failNext.compare_exchange_weak(fail, fail - 1)) {
            }
            if (fail > 0) {
                return dvv::makeError(dvv::Errc::Io, "injected stack failure");
            }
            DVV_TRY(std::shared_ptr<dvv::media::FrameStore> store,
                    dvv::media::FrameStore::create({.cpu = cpu_pool, .io = io_pool}));
            MediaStack stack;
            const dvv::media::Domain host = store->hostDomain();
            stack.viewFor = [host](const dvv::media::FrameDesc& desc) {
                return dvv::media::planesView(desc, dvv::media::Api::HostPlanes, host);
            };
            stack.store = std::move(store);
            return stack;
        };
        dvv::Result<MediaStack> stack = build();
        if (stack) {
            log->completed.fetch_add(1);
        }
        log->running.fetch_sub(1);
        return stack;
    };
    return options;
}

void SessionFixture::open(std::uint32_t slot, std::string_view uri,
                          std::optional<std::int64_t> start) {
    auto parsed = dvv::parseUri(uri);
    REQUIRE_MESSAGE(parsed, std::string(uri));
    session->open(slot, dvv::media::SourceRequest{.uri = std::move(*parsed), .hint = {}}, start);
}

bool SessionFixture::waitStatus(const std::function<bool(const SessionStatus&)>& predicate,
                                std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (predicate(session->status())) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

bool SessionFixture::waitPosition(std::int64_t position) {
    return waitStatus([position](const SessionStatus& s) { return s.player.position == position; });
}

bool SessionFixture::settle(std::uint32_t min_ready) {
    return waitStatus([min_ready](const SessionStatus& s) {
        return s.inflight == 0 && s.readyAhead >= min_ready;
    });
}

void SessionFixture::tick() {
    timer->advance(FRAME_PERIOD);
}

void SessionFixture::shutdown() {
    dvv::syncWait(session->shutdown());
    shutDown = true;
}

int SessionFixture::stackCalls() const {
    return stackLog->completed.load();
}

int SessionFixture::stackMaxRunning() const {
    return stackLog->maxRunning.load();
}

}  // namespace rawreel::testing
