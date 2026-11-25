#include "render/ruler_constants.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "core/overlay.h"
#include "core/ruler_labels.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "render/composite_constants.h"
#include "render/display_state.h"
#include "render/pass_regions.h"

namespace rawreel {

namespace {

// numberGap | tagPad << 8 | tagInset << 16 | tagMargin << 24, each at most 255.
std::uint32_t packedSpacing(const LabelSpacing& spacing) noexcept {
    const auto byte = [](std::uint32_t value) { return std::min(value, 255U); };
    return byte(spacing.numberGap) | byte(spacing.tagPad) << 8U | byte(spacing.tagInset) << 16U |
           byte(spacing.tagMargin) << 24U;
}

}  // namespace

std::array<RulerConstants, 2> rulerConstants(const OverlayParams& overlay,
                                             const dvv::media::ViewTransform& view,
                                             const PassRegions& regions,
                                             std::optional<dvv::media::ImagePixel> cursor,
                                             bool skip_encode) {
    const dvv::media::PixelRect& picture = regions.picture;
    const dvv::media::PixelRect& hugged = regions.rulers.hugged;
    const bool image = view.zoom() > 0;
    std::array<RulerConstants, 2> bands{};
    for (std::uint32_t axis = 0; axis < 2; ++axis) {
        RulerConstants& band = bands[axis];
        const dvv::media::AxisView view_axis =
            axis == 0 ? dvv::media::axisX(view) : dvv::media::axisY(view);
        const dvv::media::PixelRect& region = regions.bands[axis];
        band.regionX = region.x;
        band.regionY = region.y;
        band.regionW = region.width;
        band.regionH = region.height;
        band.axis = axis;
        // The view's axes, as compositeConstants takes them: the bands match the grid by
        // construction. P's origin and the ruler's place come from the regions.
        band.zoom = view_axis.zoom;
        band.offset = view_axis.offset;
        band.pictureStart = axis == 0 ? picture.x : picture.y;
        band.pictureExtent = view_axis.extent;
        band.imageSize = image ? view_axis.size : 0;
        band.strip = axis == 0 ? picture.y : picture.x;
        band.rulerEdge = axis == 0 ? hugged.y : hugged.x;
        band.rulerBegin = axis == 0 ? hugged.x : hugged.y;
        band.rulerEnd = band.rulerBegin + (axis == 0 ? hugged.width : hugged.height);
        band.tickBand = overlay.tickBand;
        band.tickWidth = overlay.tickWidth;
        band.markerDepth = overlay.markerDepth;
        band.litDepth = std::min(band.strip, overlay.litDepth);
        band.labelBand = band.strip > band.tickBand ? band.strip - band.tickBand : 0;
        band.cursorColour = overlay.cursorColour;
        band.labelSpacing = packedSpacing(overlay.labelSpacing);
        band.flags = (overlay.dark ? RULER_DARK : 0) | (skip_encode ? RULER_SKIP_ENCODE : 0) |
                     (overlay.lastValue == LastValue::Edge ? RULER_EDGE : 0) |
                     (overlay.cursorTagTextBlack ? RULER_TAG_TEXT_BLACK : 0);
        for (std::size_t l = 0; l < GRID_LEVELS; ++l) {
            band.levelShape[l] = levelStepLog2(overlay.levels[l]);
            band.tickLength[l] = overlay.tickLength[l];
        }
        if (!image) {
            continue;
        }
        band.flags |= RULER_IMAGE;
        if (overlay.numbers && overlay.glyphs) {
            band.flags |= RULER_NUMBERS;
        }
        const GridLevelParams& major = overlay.levels[0];
        if (dvv::media::levelVisible(levelStepLog2(major), view_axis.zoom, major.minimumSpacing)) {
            band.flags |= RULER_TICKS;
            for (std::size_t l = 0; l < GRID_LEVELS; ++l) {
                if (dvv::media::levelVisible(levelStepLog2(overlay.levels[l]), view_axis.zoom,
                                             overlay.levels[l].minimumSpacing)) {
                    band.levelShape[l] |= LEVEL_DRAWN;
                }
            }
        }
        if (cursor) {
            band.flags |= RULER_CURSOR;
            band.cursor = axis == 0 ? cursor->x : cursor->y;
            if ((band.flags & RULER_TICKS) != 0) {
                std::array<dvv::media::LineLevel, dvv::media::LINE_LEVELS> levels{};
                for (std::size_t l = 0; l < GRID_LEVELS; ++l) {
                    levels[l] = {.stepLog2 = band.levelShape[l] & LEVEL_STEP_MASK,
                                 .thickness = band.tickWidth,
                                 .drawn = (band.levelShape[l] & LEVEL_DRAWN) != 0,
                                 .halo = false};
                }
                if (const std::optional<std::uint32_t> lit =
                        dvv::media::nearestTick(band.cursor, band.imageSize, levels)) {
                    band.flags |= RULER_LIT;
                    band.litTick = *lit;
                }
            }
        }
    }
    return bands;
}

}  // namespace rawreel
