#pragma once

// Timing statistics of the rawreel-gui perf selftest (design M3 v2 §8.4 perf, E7), wx-free.

#include <cstdint>
#include <span>
#include <vector>

#include "core/player.h"
#include "dvv/media/types.h"

namespace rawreel {

struct ShowSample {
    std::int64_t index = 0;
    Clock::time_point at{};
};
struct ShowTiming {
    double actualFps = 0;         // (n - 1) / (last - first)
    double p99LatenessMs = 0;     // nearest-rank p99 of the per-show lateness
    std::uint64_t reanchors = 0;  // shows more than one period behind the schedule (stalls)
};
// The schedule starts at the first show and advances by 1/fps per show; a show more than one
// period late re-anchors it (as PlayerLogic does after a stall) and is not counted as lateness;
// early shows count 0.
[[nodiscard]] ShowTiming showTiming(std::span<const ShowSample> shows, dvv::media::Rational fps);
[[nodiscard]] double percentile(std::vector<double> values, double p);  // nearest rank; 0 if empty
[[nodiscard]] double median(std::vector<double> values);  // mean of the middle pair; 0 if empty

}  // namespace rawreel
