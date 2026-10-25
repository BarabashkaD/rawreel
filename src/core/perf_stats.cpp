#include "core/perf_stats.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <utility>

namespace rawreel {

ShowTiming showTiming(std::span<const ShowSample> shows, dvv::media::Rational fps) {
    ShowTiming timing;
    if (shows.size() < 2 || fps.num <= 0 || fps.den <= 0) {
        return timing;
    }
    using Ms = std::chrono::duration<double, std::milli>;
    const double period = 1000.0 * static_cast<double>(fps.den) / static_cast<double>(fps.num);
    std::vector<double> lateness;
    lateness.reserve(shows.size() - 1);
    Clock::time_point anchor = shows.front().at;
    std::int64_t steps = 0;  // shows since the anchor; no accumulated rounding
    for (std::size_t i = 1; i < shows.size(); ++i) {
        ++steps;
        const double late = Ms(shows[i].at - anchor).count() - static_cast<double>(steps) * period;
        if (late > period) {  // a stall: PlayerLogic re-anchors at the late show
            ++timing.reanchors;
            anchor = shows[i].at;
            steps = 0;
            continue;
        }
        lateness.push_back(std::max(late, 0.0));
    }
    const double seconds =
        std::chrono::duration<double>(shows.back().at - shows.front().at).count();
    timing.actualFps = seconds > 0 ? static_cast<double>(shows.size() - 1) / seconds : 0;
    timing.p99LatenessMs = percentile(std::move(lateness), 99.0);
    return timing;
}

double percentile(std::vector<double> values, double p) {
    if (values.empty()) {
        return 0;
    }
    std::ranges::sort(values);
    // Nearest rank: the smallest value with at least p % of the values at or below it.
    const auto rank = static_cast<std::size_t>(
        std::ceil(std::clamp(p, 0.0, 100.0) / 100.0 * static_cast<double>(values.size())));
    return values[std::clamp<std::size_t>(rank, 1, values.size()) - 1];
}

double median(std::vector<double> values) {
    if (values.empty()) {
        return 0;
    }
    std::ranges::sort(values);
    const std::size_t middle = values.size() / 2;
    return values.size() % 2 == 1 ? values[middle] : (values[middle - 1] + values[middle]) / 2;
}

}  // namespace rawreel
