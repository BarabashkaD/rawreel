#include <doctest/doctest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/perf_stats.h"
#include "core/player.h"
#include "dvv/media/types.h"

using rawreel::Clock;
using rawreel::median;
using rawreel::percentile;
using rawreel::ShowSample;
using rawreel::ShowTiming;
using rawreel::showTiming;

namespace {

// `count` samples, one every `period`, from a fixed start.
std::vector<ShowSample> regular(std::int64_t count, std::chrono::nanoseconds period) {
    const Clock::time_point t0{std::chrono::seconds(1000)};
    std::vector<ShowSample> samples;
    samples.reserve(static_cast<std::size_t>(count));
    for (std::int64_t i = 0; i < count; ++i) {
        samples.push_back({.index = i, .at = t0 + i * period});
    }
    return samples;
}

}  // namespace

TEST_CASE("perf: steady 60 fps") {
    const std::vector<ShowSample> samples = regular(601, std::chrono::nanoseconds(16'666'667));
    const ShowTiming timing = showTiming(samples, dvv::media::Rational{60, 1});
    CHECK(timing.actualFps == doctest::Approx(60).epsilon(1e-6));
    CHECK(timing.p99LatenessMs <= 0.001);
    CHECK(timing.reanchors == 0);
}

TEST_CASE("perf: one late show") {
    std::vector<ShowSample> samples = regular(100, std::chrono::milliseconds(40));
    samples[50].at += std::chrono::milliseconds(5);
    const ShowTiming timing = showTiming(samples, dvv::media::Rational{25, 1});
    CHECK(timing.p99LatenessMs == doctest::Approx(5.0));  // 99 latenesses, nearest rank 99
    CHECK(timing.reanchors == 0);
}

TEST_CASE("perf: a stall re-anchors") {
    std::vector<ShowSample> samples = regular(100, std::chrono::milliseconds(40));
    for (std::size_t i = 10; i < samples.size(); ++i) {
        samples[i].at += std::chrono::milliseconds(100);
    }
    const ShowTiming timing = showTiming(samples, dvv::media::Rational{25, 1});
    CHECK(timing.reanchors == 1);
    CHECK(timing.p99LatenessMs <= 0.001);
}

TEST_CASE("perf: percentile and median") {
    std::vector<double> values;
    for (int i = 1; i <= 100; ++i) {
        values.push_back(static_cast<double>(i));
    }
    CHECK(percentile(values, 99.0) == 99.0);
    CHECK(percentile({}, 99.0) == 0.0);
    CHECK(median({3, 1, 2}) == 2.0);
    CHECK(median({4, 1, 3, 2}) == 2.5);
    CHECK(median({}) == 0.0);
}
