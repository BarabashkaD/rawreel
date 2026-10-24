#pragma once

// Headless Session test support: a fake clock and timer, a recording sink and event receiver, and
// a fixture that builds a host-only Session on pools of its own. Shows and events share one
// sequence counter, so a test can tell which shows came after an event.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/session.h"
#include "core/session_types.h"
#include "dvv/base/error.h"
#include "dvv/base/executors.h"
#include "dvv/gpu/device_id.h"
#include "dvv/media/types.h"

namespace rawreel::testing {

inline constexpr std::chrono::milliseconds WAIT_TIMEOUT{5000};
inline constexpr std::chrono::milliseconds FRAME_PERIOD{40};  // 25 fps
// The gate of the fixture's makeStack (closeGate/releaseGate of test_source_provider.h).
inline constexpr std::string_view STACK_GATE = "makestack";

class FakeClock final : public IClock {
public:
    [[nodiscard]] Clock::time_point now() const override;
    void advance(Clock::duration d);

private:
    mutable std::mutex mutex_;
    Clock::time_point now_ = Clock::time_point{} + std::chrono::hours(1);
};

// One pending (when, callback). advance() moves the clock and, when the deadline is due, takes the
// callback out and runs it on the calling thread (it posts the tick, as ThreadTimer's would). All
// under one mutex, so cancel() waits for a running callback like ThreadTimer's.
class FakeTimer final : public ITimer {
public:
    explicit FakeTimer(std::shared_ptr<FakeClock> clock) : clock_(std::move(clock)) {}
    void schedule(Clock::time_point when, std::move_only_function<void()> callback) override;
    void cancel() override;
    void advance(Clock::duration d);
    [[nodiscard]] std::optional<Clock::time_point> pending() const;

private:
    std::shared_ptr<FakeClock> clock_;
    mutable std::mutex mutex_;
    std::optional<Clock::time_point> when_;
    std::move_only_function<void()> callback_;
};

using Sequence = std::shared_ptr<std::atomic<std::uint64_t>>;

struct ShownFrame {
    std::uint64_t seq = 0;
    std::int64_t index = -1;
    std::uint32_t count = 0;
    std::uint64_t epoch = 0;
    dvv::media::OriginId origin = 0;  // the first view's key().origin
    bool indicesMatch = false;        // every views[s]->key().index == index
    bool inspected = false;           // the inspector's answer (true without one)
};

class RecordingSink final : public IDisplaySink {
public:
    explicit RecordingSink(Sequence sequence) : sequence_(std::move(sequence)) {}
    void show(FrameSet frames) override;
    void clear() override;
    // Runs inside show() (on the Session executor); must not keep the FrameSet.
    void setInspector(std::function<bool(const FrameSet&)> inspect);
    // Waits until the latest show has `index`.
    [[nodiscard]] bool waitShown(std::int64_t index,
                                 std::chrono::milliseconds timeout = WAIT_TIMEOUT);
    // Waits until `predicate(history)` holds.
    [[nodiscard]] bool waitFor(const std::function<bool(const std::vector<ShownFrame>&)>& predicate,
                               std::chrono::milliseconds timeout = WAIT_TIMEOUT);
    [[nodiscard]] std::vector<ShownFrame> history() const;
    [[nodiscard]] std::uint64_t clears() const;
    [[nodiscard]] bool holdsFrame() const;

private:
    Sequence sequence_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<ShownFrame> history_;
    std::optional<FrameSet> latest_;
    std::uint64_t clears_ = 0;
    std::function<bool(const FrameSet&)> inspect_;
};

struct SessionEvent {
    enum class Kind : std::uint8_t { StreamsChanged, FormatNeeded, StackChanged, Error };
    Kind kind = Kind::Error;
    std::uint64_t seq = 0;
    std::vector<StreamInfo> streams;  // StreamsChanged
    std::uint32_t slot = 0;           // FormatNeeded
    std::optional<dvv::media::SourceRequest> request;
    dvv::media::Ambiguous candidates;
    std::string reason;
    bool expired = false;  // StackChanged
    std::uint64_t epoch = 0;
    std::optional<dvv::Error> error;      // Error
    std::optional<SessionStatus> status;  // Error: the status probe's answer inside error()
};

class RecordingEvents final : public ISessionEvents {
public:
    explicit RecordingEvents(Sequence sequence) : sequence_(std::move(sequence)) {}
    void streamsChanged(std::vector<StreamInfo> streams) override;
    void formatNeeded(std::uint32_t slot, dvv::media::SourceRequest request,
                      dvv::media::Ambiguous candidates, std::string reason) override;
    void stackChanged(std::weak_ptr<dvv::gpu::ExecutionContext> context,
                      std::uint64_t epoch) override;
    void error(dvv::Error error) override;
    // Called inside error(); its answer is stored with the event.
    void setStatusProbe(std::function<SessionStatus()> probe);

    [[nodiscard]] bool waitFor(
        const std::function<bool(const std::vector<SessionEvent>&)>& predicate,
        std::chrono::milliseconds timeout = WAIT_TIMEOUT);
    [[nodiscard]] std::vector<SessionEvent> events() const;
    [[nodiscard]] std::vector<SessionEvent> of(SessionEvent::Kind kind) const;
    [[nodiscard]] std::weak_ptr<dvv::gpu::ExecutionContext> lastContext() const;

private:
    void record(SessionEvent event);

    Sequence sequence_;
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<SessionEvent> events_;
    std::weak_ptr<dvv::gpu::ExecutionContext> context_;
    std::function<SessionStatus()> probe_;
};

[[nodiscard]] std::size_t countKind(const std::vector<SessionEvent>& events,
                                    SessionEvent::Kind kind);

// Tag: the fixture runs on steadyClock() and makeThreadTimer() instead of the fakes.
struct RealTime {};

class SessionFixture {
public:
    SessionFixture();  // creates the Session (REQUIREs success)
    // The production clock and timer: `clock` and `timer` stay null, tick() must not be used.
    explicit SessionFixture(RealTime);
    SessionFixture(const SessionFixture&) = delete;
    SessionFixture& operator=(const SessionFixture&) = delete;
    SessionFixture(SessionFixture&&) = delete;
    SessionFixture& operator=(SessionFixture&&) = delete;
    // Awaits shutdown() unless a test did, drops everything and waits for the lifetime registry
    // to count nothing.
    ~SessionFixture();

    // Options for Session::create with the fixture's members and the counting host-only stack.
    [[nodiscard]] SessionOptions options();

    void open(std::uint32_t slot, std::string_view uri, std::optional<std::int64_t> start = {});
    // Polls status() every millisecond until `predicate` holds or the timeout passes.
    [[nodiscard]] bool waitStatus(const std::function<bool(const SessionStatus&)>& predicate,
                                  std::chrono::milliseconds timeout = WAIT_TIMEOUT);
    [[nodiscard]] bool waitPosition(std::int64_t position);
    // waitStatus(inflight == 0 && readyAhead >= min_ready).
    [[nodiscard]] bool settle(std::uint32_t min_ready);
    void tick();                                // timer.advance(40 ms)
    void shutdown();                            // syncWait(session->shutdown())
    [[nodiscard]] int stackCalls() const;       // completed makeStack calls
    [[nodiscard]] int stackMaxRunning() const;  // the most makeStack calls ever running at once

    std::shared_ptr<dvv::ThreadPool> cpu, io;
    std::shared_ptr<dvv::ComponentRegistry> components;
    std::shared_ptr<FakeClock> clock;
    std::shared_ptr<FakeTimer> timer;
    std::shared_ptr<IClock> realClock;  // RealTime only
    std::shared_ptr<ITimer> realTimer;  // RealTime only
    Sequence sequence;
    std::shared_ptr<RecordingSink> sink;
    std::shared_ptr<RecordingEvents> events;
    // makeStack (every call, the first in create() included) counts itself as running, waits at
    // STACK_GATE (10 s; then Errc::Internal "gate makestack timed out"), builds the host-only
    // stack and counts itself as completed. The atomics are written on the I/O pool. While
    // failNext is positive, a call that passed the gate takes one from it and fails with
    // Errc::Io "injected stack failure" instead (not counted as completed).
    struct StackLog {
        std::mutex mutex;
        int calls = 0;                                           // started calls
        std::vector<std::optional<dvv::gpu::DeviceId>> devices;  // makeStack's arguments
        std::atomic<int> completed{0};
        std::atomic<int> running{0};
        std::atomic<int> maxRunning{0};
        std::atomic<int> failNext{0};
    };
    std::shared_ptr<StackLog> stackLog = std::make_shared<StackLog>();
    std::unique_ptr<Session> session;
    bool shutDown = false;

private:
    void start(bool real_time);
    void teardown();  // the destructor's body
};

}  // namespace rawreel::testing
