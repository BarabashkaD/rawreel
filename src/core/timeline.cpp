#include "core/timeline.h"

#include <algorithm>
#include <cmath>

namespace rawreel {

TickInterval chooseTicks(std::int64_t first, std::int64_t last, double track_px,
                         double min_minor_px) noexcept {
    if (last <= first || track_px <= 0) {
        return {};
    }
    const double px_per_frame = track_px / static_cast<double>(last - first);
    constexpr std::int64_t STEPS[] = {1, 2, 5};
    std::int64_t decade = 1;
    for (int guard = 0; guard < 18; ++guard) {
        for (const std::int64_t step : STEPS) {
            const std::int64_t minor = step * decade;
            if (static_cast<double>(minor) * px_per_frame >= min_minor_px) {
                return {.minor = minor, .major = decade * 10};
            }
        }
        decade *= 10;
    }
    return {};
}

std::vector<Tick> ticks(std::int64_t first, std::int64_t last, TickInterval interval) {
    std::vector<Tick> out;
    if (interval.minor <= 0 || last < first) {
        return out;
    }
    // First multiple of minor that is >= first.
    std::int64_t frame = (first / interval.minor) * interval.minor;
    if (frame < first) {
        frame += interval.minor;
    }
    for (; frame <= last; frame += interval.minor) {
        out.push_back({.frame = frame, .major = interval.major > 0 && frame % interval.major == 0});
    }
    return out;
}

double thumbX(std::int64_t value, std::int64_t min, std::int64_t max, double travel_start,
              double travel_end) noexcept {
    if (max <= min) {
        return travel_start;
    }
    const double fraction = static_cast<double>(value - min) / static_cast<double>(max - min);
    return travel_start + fraction * (travel_end - travel_start);
}

std::int64_t frameAt(double x, std::int64_t min, std::int64_t max, double travel_start,
                     double travel_end) noexcept {
    if (travel_end <= travel_start || max <= min) {
        return min;
    }
    const double fraction = std::clamp((x - travel_start) / (travel_end - travel_start), 0.0, 1.0);
    return min + std::llround(fraction * static_cast<double>(max - min));
}

namespace {

struct Box {
    double left = 0;
    double right = 0;
};

[[nodiscard]] Box boxAt(double left, double width) noexcept {
    return {.left = left, .right = left + width};
}

// Boxes that only touch do not overlap.
[[nodiscard]] bool overlaps(const Box& a, const Box& b) noexcept {
    return a.left < b.right && b.left < a.right;
}

[[nodiscard]] double clampedLeft(const LabelIn& label, double strip_width) noexcept {
    const double left = label.anchor - label.width / 2;
    if (label.width > strip_width) {
        return 0;
    }
    return std::clamp(left, 0.0, strip_width - label.width);
}

[[nodiscard]] bool inside(const Box& b, double strip_width) noexcept {
    return b.left >= 0 && b.right <= strip_width;
}

// `marker` is the original label's anchor; the caller has checked that the label exists.
[[nodiscard]] Side preferredSide(const TimelineLabelsIn& in, double marker) noexcept {
    if (in.current) {
        if (in.current->anchor > marker + in.hysteresis) {
            return Side::Left;
        }
        if (in.current->anchor < marker - in.hysteresis) {
            return Side::Right;
        }
        if (in.previousSide) {
            return *in.previousSide;
        }
        return in.current->anchor >= marker ? Side::Left : Side::Right;
    }
    return in.previousSide.value_or(Side::Left);
}

}  // namespace

TimelineLabelsOut placeLabels(const TimelineLabelsIn& in) {
    TimelineLabelsOut out;
    std::optional<Box> current_box;
    std::optional<Box> original_box;

    if (in.current) {
        out.current = clampedLeft(*in.current, in.stripWidth);
        current_box = boxAt(*out.current, in.current->width);
    }

    if (in.original) {
        const double marker = in.original->anchor;
        const double width = in.original->width;
        const Side preferred = preferredSide(in, marker);
        out.originalSide = preferred;
        const auto box_for = [&](Side side) {
            return side == Side::Left ? boxAt(marker - in.gap - width, width)
                                      : boxAt(marker + in.gap, width);
        };
        const auto fits = [&](const Box& b) {
            return inside(b, in.stripWidth) && !(current_box && overlaps(b, *current_box));
        };
        const Side other = preferred == Side::Left ? Side::Right : Side::Left;
        for (const Side side : {preferred, other}) {
            const Box b = box_for(side);
            if (fits(b)) {
                out.original = b.left;
                original_box = b;
                break;
            }
        }
    }

    const auto place_end = [&](const std::optional<LabelIn>& label) -> std::optional<double> {
        if (!label) {
            return std::nullopt;
        }
        const double left = clampedLeft(*label, in.stripWidth);
        const Box b = boxAt(left, label->width);
        if ((current_box && overlaps(b, *current_box)) ||
            (original_box && overlaps(b, *original_box))) {
            return std::nullopt;
        }
        return left;
    };
    out.first = place_end(in.first);
    out.last = place_end(in.last);
    return out;
}

}  // namespace rawreel
