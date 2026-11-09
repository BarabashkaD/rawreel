#pragma once

// The timeline strip's geometry (video control panel §5.2, §5.3): tick intervals, the thumb's x and
// the placement of the number labels. wx-free arithmetic in the strip's pixels; the GUI's
// TimelineStrip measures the text and draws.

#include <cstdint>
#include <optional>
#include <vector>

namespace rawreel {

struct TickInterval {
    std::int64_t minor = 0;  // 0: no ticks
    std::int64_t major = 0;
    friend bool operator==(const TickInterval&, const TickInterval&) = default;
};

// The smallest minor in 1, 2, 5 x 10^n with minor x (track_px / (last - first)) >= min_minor_px;
// major = 10^(n+1) for minor = m x 10^n. {0, 0} when last <= first or track_px <= 0.
[[nodiscard]] TickInterval chooseTicks(std::int64_t first, std::int64_t last, double track_px,
                                       double min_minor_px) noexcept;

struct Tick {
    std::int64_t frame = 0;
    bool major = false;
};

// The multiples of interval.minor in [first, last]; major when a multiple of interval.major.
[[nodiscard]] std::vector<Tick> ticks(std::int64_t first, std::int64_t last, TickInterval interval);

// travel_start + (value - min) / (max - min) x (travel_end - travel_start); travel_start if
// max <= min.
[[nodiscard]] double thumbX(std::int64_t value, std::int64_t min, std::int64_t max,
                            double travel_start, double travel_end) noexcept;

// The inverse of thumbX: (x - travel_start) / (travel_end - travel_start) clamped to [0, 1], then
// min + lround(fraction x (max - min)); min when travel_end <= travel_start or max <= min.
[[nodiscard]] std::int64_t frameAt(double x, std::int64_t min, std::int64_t max,
                                   double travel_start, double travel_end) noexcept;

enum class Side : std::uint8_t { Left, Right };

struct LabelIn {
    double anchor = 0;  // the x the label belongs to
    double width = 0;
};

struct TimelineLabelsIn {
    double stripWidth = 0;
    double gap = 0;         // between the marker line and the original label
    double hysteresis = 0;  // LABEL_FLIP_HYSTERESIS_DIP in pixels
    std::optional<LabelIn> current, original, first, last;  // original.anchor: the marker's x
    std::optional<Side> previousSide;                       // the original label's last side
};

struct TimelineLabelsOut {
    std::optional<double> current, original, first, last;  // left edges; nullopt: hidden
    std::optional<Side> originalSide;                      // set whenever `original` was given
};

// The ordered placement of spec §5.2: current, then original, then first and last.
[[nodiscard]] TimelineLabelsOut placeLabels(const TimelineLabelsIn& in);

}  // namespace rawreel
