#include "ruler_reference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "composite_reference.h"
#include "dvv/media/display.h"
#include "dvv/media/view_lines.h"
#include "render/composite_constants.h"
#include "render/ruler_constants.h"

namespace rawreel {

namespace {

using Rgb = std::array<float, 3>;

Rgb rgb(std::uint32_t packed) {
    return {static_cast<float>((packed >> 16U) & 0xFFU) / 255.0F,
            static_cast<float>((packed >> 8U) & 0xFFU) / 255.0F,
            static_cast<float>(packed & 0xFFU) / 255.0F};
}

std::uint8_t unorm8(float x) {
    return static_cast<std::uint8_t>(std::lround(255.0F * std::clamp(x, 0.0F, 1.0F)));
}

}  // namespace

std::array<std::uint8_t, 4> rulerPixel(const RulerConstants& k, std::uint32_t x,
                                       std::uint32_t y) noexcept {
    const bool top = k.axis == 0;
    const std::uint32_t across = top ? y : x;
    const std::uint32_t along_pixel = top ? x : y;
    const std::int32_t along =
        static_cast<std::int32_t>(along_pixel) - static_cast<std::int32_t>(k.pictureStart);
    const bool dark = (k.flags & RULER_DARK) != 0;
    const bool rows = across >= k.rulerEdge - k.strip && across < k.rulerEdge;
    const bool ruler = rows && along_pixel >= k.rulerBegin && along_pixel < k.rulerEnd;
    const bool corner =
        top && rows && along_pixel >= k.rulerBegin - k.pictureStart && along_pixel < k.rulerBegin;
    const std::uint32_t depth = k.rulerEdge - 1 - across;
    const float grey = dark ? BACKGROUND_DARK : BACKGROUND_LIGHT;
    Rgb colour{grey, grey, grey};
    if (corner) {
        colour = rgb(dark ? RULER_CORNER_DARK : RULER_CORNER_LIGHT);
    } else if (ruler && depth == 0) {
        colour = rgb(dark ? RULER_SEPARATOR_DARK : RULER_SEPARATOR_LIGHT);
    } else if (ruler) {
        colour = rgb(dark ? RULER_BACKGROUND_DARK : RULER_BACKGROUND_LIGHT);
    }
    if (ruler && (k.flags & RULER_IMAGE) != 0) {
        const dvv::media::AxisView axis{
            .offset = k.offset, .zoom = k.zoom, .size = k.imageSize, .extent = k.pictureExtent};
        if ((k.flags & RULER_TICKS) != 0 && depth < k.tickBand) {
            std::array<dvv::media::LineLevel, dvv::media::LINE_LEVELS> levels{};
            for (std::size_t l = 0; l < levels.size(); ++l) {
                levels[l] = {.stepLog2 = k.levelShape[l] & LEVEL_STEP_MASK,
                             .thickness = k.tickWidth,
                             .drawn = (k.levelShape[l] & LEVEL_DRAWN) != 0,
                             .halo = false};
            }
            const std::optional<dvv::media::LineHit> hit = dvv::media::lineAt(axis, levels, along);
            if (hit && depth < k.tickLength[hit->level]) {
                colour = hit->level == 0 ? rgb(dark ? RULER_MAJOR_DARK : RULER_MAJOR_LIGHT)
                                         : rgb(dark ? RULER_MINOR_DARK : RULER_MINOR_LIGHT);
            }
        }
        if ((k.flags & RULER_LIT) != 0 && depth < k.litDepth) {
            const dvv::media::PixelSpan lit =
                dvv::media::lineSpan(axis, k.litTick, 3 * k.tickWidth);
            if (along >= lit.begin && along < lit.end) {
                colour = rgb(k.cursorColour);
            }
        }
        if ((k.flags & RULER_CURSOR) != 0 && depth < k.markerDepth) {
            const dvv::media::PixelSpan span = dvv::media::cursorSpan(axis, k.cursor);
            if (along >= span.begin && along < span.end) {
                const Rgb marker = rgb(k.cursorColour);
                for (std::size_t c = 0; c < colour.size(); ++c) {
                    colour[c] = colour[c] + (RULER_MARKER_OPACITY * (marker[c] - colour[c]));
                }
            }
        }
    }
    if ((k.flags & RULER_SKIP_ENCODE) != 0) {
        for (float& c : colour) {
            c = dvv::media::srgbDecode(c);
        }
    }
    return {unorm8(colour[0]), unorm8(colour[1]), unorm8(colour[2]), 255};
}

}  // namespace rawreel
