#pragma once

// Value types and interfaces of the Session (design M3 v2 §4.3). Leases are shared, so the
// presenter thread can keep using a FrameSet while a newer one arrives; events never carry strong
// references to managed objects (D10).

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "core/player.h"
#include "core/view.h"  // MAX_STREAMS (= 1, the view section)
#include "dvv/base/error.h"
#include "dvv/base/move_only_function.h"
#include "dvv/media/display.h"
#include "dvv/media/frame_store.h"
#include "dvv/media/source.h"
#include "dvv/media/source_factory.h"

namespace dvv::gpu {
class ExecutionContext;
}

namespace rawreel {

struct StreamInfo {
    std::uint32_t slot = 0;
    std::string resolvedUri;
    dvv::media::SourceInfo info;
    dvv::media::ResolvedColour colour;
};
// One shown frame: the display views of every stream for one index (one stream at a time; the array
// keeps the v1.x tile shape, D2).
struct FrameSet {
    std::int64_t index = -1;
    std::uint32_t count = 0;
    std::array<std::shared_ptr<const dvv::media::ReadLease>, MAX_STREAMS> views{};
    std::uint64_t epoch = 0;  // the Session's stackEpoch (D7)
};
struct SessionStatus {
    PlayerStatus player;
    std::uint32_t streams = 0;
    std::uint64_t epoch = 0;
    bool gpuAvailable = false;  // a stack is installed
    std::string bottleneck;
    std::uint32_t inflight = 0;    // request tasks running
    std::uint32_t readyAhead = 0;  // ready-map entries after the position
    std::uint64_t revision = 0;    // 1 after the first publish; +1 per changed snapshot
};  // written at the end of each pump(); tests wait on inflight/readyAhead

// Every field except `revision`: what decides whether a new snapshot is published.
inline bool sameState(const SessionStatus& a, const SessionStatus& b) {
    return a.player == b.player && a.streams == b.streams && a.epoch == b.epoch &&
           a.gpuAvailable == b.gpuAvailable && a.bottleneck == b.bottleneck &&
           a.inflight == b.inflight && a.readyAhead == b.readyAhead;
}

class IDisplaySink {  // thread-safe, non-blocking (the viewport's DisplayState)
public:
    virtual ~IDisplaySink() = default;
    virtual void show(FrameSet frames) = 0;
    virtual void clear() = 0;  // drops every lease it holds
};
class ISessionEvents {  // called on the Session executor; must not block (GUI: CallAfter)
public:
    virtual ~ISessionEvents() = default;
    virtual void streamsChanged(std::vector<StreamInfo> streams) = 0;  // incl. warnings
    virtual void formatNeeded(std::uint32_t slot, dvv::media::SourceRequest request,
                              dvv::media::Ambiguous candidates, std::string reason) = 0;
    // At start, after a loss (expired weak_ptr: no GPU stack) and after every reopen().
    virtual void stackChanged(std::weak_ptr<dvv::gpu::ExecutionContext> context,
                              std::uint64_t epoch) = 0;
    // Non-fatal: infobar + log. Errc::DeviceLost -> the GUI shows the Reopen button.
    virtual void error(dvv::Error error) = 0;
};  // removed from the first M3 design: probed, exportFinished, deviceLostLimit
class IClock {
public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual Clock::time_point now() const = 0;
};
class ITimer {  // one pending deadline; schedule() replaces it
public:
    virtual ~ITimer() = default;
    virtual void schedule(Clock::time_point when, dvv::MoveOnlyFunction<void()> callback) = 0;
    // Binding: blocks until a running callback has returned; must not be called from inside a
    // callback. ThreadTimer's destructor implies cancel().
    virtual void cancel() = 0;
};

// The developer option --debug-seek-delay: an extra wait before every frame is produced.
// Thread-safe; shared by the GUI and the delayed sources (delayed_source.h).
class ProduceDelay {
public:
    void set(std::uint32_t ms) noexcept { ms_.store(ms); }
    [[nodiscard]] std::uint32_t get() const noexcept { return ms_.load(); }
    // Wakes every wait now and makes later waits return at once.
    void stop() noexcept;
    // Returns after `ms`, or at once / early after stop().
    void waitFor(std::uint32_t ms);

private:
    std::atomic<std::uint32_t> ms_{0};
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopped_ = false;  // under mutex_
};

}  // namespace rawreel
