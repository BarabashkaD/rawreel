#include "core/ruler_labels.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <vector>

#include "core/overlay.h"
#include "dvv/media/view_lines.h"

namespace rawreel {

namespace {

// Pixels are kept in [-2^30, 2^30], as in dvv-media's line rules, so no box end overflows.
constexpr std::int64_t PIXEL_LIMIT = std::int64_t{1} << 30;

std::int32_t saturated(std::int64_t pixel) noexcept {
    return static_cast<std::int32_t>(std::clamp(pixel, -PIXEL_LIMIT, PIXEL_LIMIT));
}

std::int32_t textLength(std::uint32_t value, std::uint32_t advance) noexcept {
    return static_cast<std::int32_t>(decimalDigits(value) * advance);
}

// A tag of `value` whose box begins at `box_begin`.
RulerLabel tagAt(RulerLabelKind kind, std::uint32_t value, std::int32_t box_begin,
                 const RulerLabelInput& input) noexcept {
    const auto pad = static_cast<std::int32_t>(input.spacing.tagPad);
    const std::int32_t length = textLength(value, input.advance) + 2 * pad;
    return {.kind = kind,
            .value = value,
            .textBegin = box_begin + pad,
            .boxBegin = box_begin,
            .boxEnd = box_begin + length};
}

// Whether the box [begin, end) touches `tag`'s box widened by `advance` on each side.
bool touches(std::int32_t begin, std::int32_t end, const RulerLabel& tag,
             std::uint32_t advance) noexcept {
    const auto widen = static_cast<std::int64_t>(advance);
    return begin < tag.boxEnd + widen && tag.boxBegin - widen < end;
}

}  // namespace

std::uint32_t decimalDigits(std::uint32_t value) noexcept {
    std::uint32_t digits = 1;
    for (; value >= 10; value /= 10) {
        ++digits;
    }
    return digits;
}

std::vector<RulerLabel> rulerLabels(const RulerLabelInput& input) {
    using enum RulerLabelKind;
    if (!input.numbers || input.advance == 0) {
        return {};
    }
    const std::optional<dvv::media::PixelRange> visible = dvv::media::visiblePixels(input.axis);
    if (!visible) {
        return {};
    }

    const dvv::media::AxisView& axis = input.axis;
    const std::uint32_t advance = input.advance;
    const auto pad = static_cast<std::int32_t>(input.spacing.tagPad);
    const auto margin = static_cast<std::int32_t>(input.spacing.tagMargin);
    const std::int32_t left = input.rulerBegin + margin;
    const std::int32_t right = input.rulerEnd - margin;
    const auto tag_length = [&](std::uint32_t value) {
        return textLength(value, advance) + 2 * pad;
    };

    std::optional<RulerLabel> first_tag, last_tag, cursor_tag;
    const std::uint32_t first = visible->first;
    const std::uint32_t last =
        input.lastValue == LastValue::Edge ? visible->last + 1 : visible->last;
    const std::int32_t first_length = tag_length(first);
    const std::int32_t last_length = tag_length(last);
    if (left + first_length <= right) {
        first_tag = tagAt(FirstTag, first, left, input);
        if (first_length + static_cast<std::int32_t>(advance) + last_length <= right - left) {
            last_tag = tagAt(LastTag, last, right - last_length, input);
        }
    }

    if (input.cursor && *input.cursor < axis.size) {
        const std::uint32_t c = *input.cursor;
        const std::int32_t length = tag_length(c);
        // Kept in range as a float, before the integer conversion, as the shader does.
        const float centre_at = std::floor(dvv::media::pixelCentreAt(axis, c));
        constexpr auto LIMIT = static_cast<float>(PIXEL_LIMIT);
        const float raised = centre_at >= -LIMIT ? centre_at : -LIMIT;
        const auto pixel = static_cast<std::int64_t>(raised > LIMIT ? LIMIT : raised);
        const std::int32_t centre = saturated(std::int64_t{input.pictureStart} + saturated(pixel));
        const std::int32_t begin = std::max(left, std::min(right - length, centre - length / 2));
        cursor_tag = tagAt(CursorTag, c, begin, input);
        const auto touched = [&](const std::optional<RulerLabel>& tag) {
            return tag && touches(tag->boxBegin, tag->boxEnd, *cursor_tag, advance);
        };
        if (touched(first_tag)) {
            first_tag.reset();
        }
        if (touched(last_tag)) {
            last_tag.reset();
        }
    }

    std::vector<RulerLabel> labels;
    if (input.ticksShown) {
        const float needed = static_cast<float>(decimalDigits(axis.size) * advance + advance);
        const std::uint32_t step_log2 =
            dvv::media::numberStepLog2(input.majorStepLog2, axis.zoom, needed, axis.size);
        const std::uint64_t step =
            step_log2 >= 32 ? std::uint64_t{1} << 32 : std::uint64_t{1} << step_log2;
        const auto gap = static_cast<std::int64_t>(input.spacing.numberGap);
        // A number's text begins at or after the ruler only where firstPixelOf(k) >= p0, so only
        // where k > u(p0 - 1); the walk starts one step below the multiple under that point and
        // ends at the first text that begins past the ruler (firstPixelOf does not decrease).
        const std::int64_t p0 = std::int64_t{input.rulerBegin} - input.pictureStart - gap;
        const float u = dvv::media::imageCoordinateAt(axis, saturated(p0 - 1));
        const double below =
            std::clamp(std::floor(static_cast<double>(u)), 0.0, static_cast<double>(axis.size));
        const std::uint64_t start_multiple = static_cast<std::uint64_t>(below) / step;
        const std::uint64_t start = start_multiple > 0 ? (start_multiple - 1) * step : 0;
        for (std::uint64_t k = start; k <= axis.size; k += step) {
            const auto value = static_cast<std::uint32_t>(k);
            const std::int32_t begin = saturated(
                input.pictureStart + std::int64_t{dvv::media::firstPixelOf(axis, value)} + gap);
            if (begin >= input.rulerEnd) {
                break;
            }
            const std::int32_t end = begin + textLength(value, advance);
            if (begin < input.rulerBegin || end > input.rulerEnd) {
                continue;
            }
            const auto touched = [&](const std::optional<RulerLabel>& tag) {
                return tag && touches(begin, end, *tag, advance);
            };
            if (touched(first_tag) || touched(last_tag) || touched(cursor_tag)) {
                continue;
            }
            labels.push_back({.kind = Number,
                              .value = value,
                              .textBegin = begin,
                              .boxBegin = begin,
                              .boxEnd = end});
        }
    }
    for (const std::optional<RulerLabel>& tag : {first_tag, last_tag, cursor_tag}) {
        if (tag) {
            labels.push_back(*tag);
        }
    }
    return labels;
}

bool cursorTagTextBlack(Rgb8 colour) noexcept {
    const auto decoded = [](std::uint8_t channel) {
        const double c = channel / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    const double luminance =
        0.2126 * decoded(colour.r) + 0.7152 * decoded(colour.g) + 0.0722 * decoded(colour.b);
    return luminance > 0.5;
}

}  // namespace rawreel
