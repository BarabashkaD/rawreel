#pragma once

// Value types and interfaces of the Session (design M3 v2 §4.3). Leases are shared, so the
// presenter thread can keep using a FrameSet while a newer one arrives; events never carry strong
// references to managed objects (D10).

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/player.h"
#include "core/view.h"  // MAX_STREAMS (= 1, the view section)
#include "dvv/base/error.h"
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
// One shown frame: the display views of every stream for one index (one stream in v1.0; the array
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
};  // written at the end of each pump(); tests wait on inflight/readyAhead

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
    virtual void schedule(Clock::time_point when, std::move_only_function<void()> callback) = 0;
    // Binding: blocks until a running callback has returned; must not be called from inside a
    // callback. ThreadTimer's destructor implies cancel().
    virtual void cancel() = 0;
};

}  // namespace rawreel
