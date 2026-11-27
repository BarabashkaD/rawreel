#include "render/picture_constants.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "core/overlay.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "render/pass_regions.h"
#include "render/view_state.h"

using dvv::media::ViewTransform;

namespace rawreel {

std::uint32_t levelStepLog2(const GridLevelParams& level) noexcept {
    return std::min(level.stepLog2, LEVEL_STEP_MASK);
}

std::array<bool, GRID_LEVELS> gridLevelsDrawn(const OverlayParams& overlay,
                                              const ViewTransform& view) noexcept {
    const auto zoom = static_cast<float>(view.zoom());
    const auto visible = [&overlay, zoom](std::size_t l) {
        const GridLevelParams& level = overlay.levels[l];
        return level.shown &&
               dvv::media::levelVisible(levelStepLog2(level), zoom, level.minimumSpacing);
    };
    std::array<bool, GRID_LEVELS> drawn{};
    if (!visible(0)) {
        return drawn;
    }
    for (std::size_t l = 0; l < GRID_LEVELS; ++l) {
        drawn[l] = visible(l);
    }
    return drawn;
}

PictureConstants pictureConstants(const ViewParams& params, const ViewTransform& view,
                                  const PassRegions& regions, bool skip_encode) {
    const OverlayParams& overlay = params.overlay;
    const bool shown = view.zoom() > 0.0;
    const dvv::media::AxisView x = dvv::media::axisX(view);
    const dvv::media::AxisView y = dvv::media::axisY(view);
    PictureConstants constants{.viewportW = view.viewport().width,
                               .viewportH = view.viewport().height,
                               .imageW = shown ? view.image().width : 0,
                               .imageH = shown ? view.image().height : 0,
                               .zoom = x.zoom,
                               .flags = (overlay.dark ? PICTURE_DARK : 0U) |
                                        (skip_encode ? PICTURE_SKIP_ENCODE : 0U) |
                                        (params.alpha ? PICTURE_ALPHA : 0U),
                               .regionX = regions.picture.x,
                               .regionY = regions.picture.y,
                               .offsetX = x.offset,
                               .offsetY = y.offset,
                               .drawX = regions.pictureDrawn.x - regions.picture.x,
                               .drawY = regions.pictureDrawn.y - regions.picture.y,
                               .checkerSize = std::max(overlay.checkerSize, 1U)};
    const std::array<bool, GRID_LEVELS> drawn = gridLevelsDrawn(overlay, view);
    for (std::size_t l = 0; l < GRID_LEVELS; ++l) {
        const GridLevelParams& level = overlay.levels[l];
        constants.levelColour[l] = level.colour;
        constants.levelShape[l] =
            levelStepLog2(level) |
            (std::min(level.thickness, LEVEL_THICKNESS_MAX) << LEVEL_THICKNESS_SHIFT) |
            (level.adaptive ? LEVEL_ADAPTIVE : 0U) | (level.halo ? LEVEL_HALO : 0U) |
            (drawn[l] ? LEVEL_DRAWN : 0U);
    }
    return constants;
}

}  // namespace rawreel
