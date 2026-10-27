#pragma once

// The player logic of design M3 v2 (§4.3, D11): which frame to show when, what to prefetch and
// what to keep. Pure: no threads and no clock of its own; the Session feeds it times and answers.

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "dvv/media/types.h"

namespace rawreel {

using Clock = std::chrono::steady_clock;

struct PlayerConfig {
    std::optional<std::int64_t> length;  // frames of the shortest stream; nullopt = unbounded
    dvv::media::Rational fps{25, 1};     // stream 1's fps
    std::uint32_t window = 8;            // W: prefetch indices ahead of the position
    std::uint32_t inflight = 4;          // K: at most this many requested and not yet answered
};

struct PlayerStatus {
    std::int64_t position = -1;  // the last shown index (-1: none since the last reset)
    std::optional<std::int64_t> length;
    bool playing = false, loop = false, stalled = false;
    double targetFps = 0, actualFps = 0;
    std::uint64_t shown = 0, stalls = 0;
};

class PlayerLogic {
public:
    explicit PlayerLogic(PlayerConfig config);  // no stream set: nothing to show
    // New stream set; pauses; drops readiness.
    void reset(PlayerConfig config, std::int64_t position);
    void play(Clock::time_point now);
    void pause();
    void stop();                    // pause + seek(0)
    void step(std::int64_t delta);  // pauses; wraps with loop, else clamps; sets the direction
    void seek(std::int64_t index);  // pauses; clamps; sets the direction
    void setLoop(bool loop);
    void setWindow(std::uint32_t window);  // plan addition (OutOfMemory halving)
    // All streams of `index` are ready. True when the index is kept (inside keeps()); false: the
    // caller releases the leases. Always clears the outstanding mark.
    bool frameReady(std::int64_t index, Clock::time_point now);
    void frameFailed(std::int64_t index);  // pauses when the failed index is due (P3)
    void tick(Clock::time_point now);
    [[nodiscard]] std::optional<std::int64_t> takeShow();    // index to show now (once)
    [[nodiscard]] std::vector<std::int64_t> takeRequests();  // to acquire now (cap, window)
    [[nodiscard]] std::pair<std::int64_t, std::int64_t> keepRange() const;  // raw, may wrap
    // Plan addition: inside keepRange(), modulo the length when looping.
    [[nodiscard]] bool keeps(std::int64_t index) const;
    [[nodiscard]] std::optional<Clock::time_point> nextDeadline() const;
    // Plan addition: the pending seek, step, start or stall index, else the position (-1 before the
    // first show). The Session's out-of-memory retry restarts here.
    [[nodiscard]] std::int64_t target() const noexcept;
    [[nodiscard]] PlayerStatus status(Clock::time_point now) const;

private:
    // False before the first reset() and for a bounded stream of length 0: every operation is a
    // no-op then.
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] Clock::duration period() const noexcept;
    [[nodiscard]] std::int64_t clampIndex(std::int64_t index) const noexcept;
    [[nodiscard]] std::int64_t base() const noexcept;
    [[nodiscard]] std::optional<std::int64_t> next() const noexcept;
    // Moves to `target` (seek's and step's common part); `target` is clamped.
    void moveTo(std::int64_t target);
    // Shows `index`; a playback show records `now` for actualFps.
    void show(std::int64_t index, bool playback, Clock::time_point now);
    // Drops readiness outside keeps(). Called after every change of the keep range, because the
    // Session releases the leases of every index that leaves it (P4).
    void prune();

    PlayerConfig config_;
    bool started_ = false;  // reset() was called
    std::int64_t position_ = -1;
    std::optional<std::int64_t> wanted_;
    bool playing_ = false;
    bool loop_ = false;
    bool stalled_ = false;
    std::int64_t direction_ = 1;
    std::optional<Clock::time_point> deadline_;
    std::set<std::int64_t> ready_;
    std::set<std::int64_t> outstanding_;
    std::set<std::int64_t> failed_;
    std::optional<std::int64_t> show_;
    std::deque<Clock::time_point> shows_;  // playback shows of the last second
    std::uint64_t shown_ = 0;
    std::uint64_t stalls_ = 0;
};

}  // namespace rawreel
