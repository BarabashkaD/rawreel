#pragma once

// How the view's panels tile the output: one panel per picture the view shows side by side, each
// with its own picture region and ruler strips. A panel is drawn as a whole output of its size
// would be, moved by its origin.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/pixel_size.h"
#include "render/pass_regions.h"

namespace rawreel {

struct PanelGrid {
    std::uint32_t columns = 1, rows = 1;
    bool operator==(const PanelGrid&) const = default;
};

// The grid of `count` panels in `extent`: 1x1 for a count up to 1; for 4 the one of 1x4 (four
// columns) and 2x2 whose panels have the larger smaller side, 2x2 on a tie; else one row of
// `count` columns.
[[nodiscard]] PanelGrid panelGrid(dvv::platform::PixelSize extent, std::uint32_t count) noexcept;

// The panels of `grid` in `extent`, row-major, tiling it exactly: column i is
// w*(i+1)/columns - w*i/columns wide, row j likewise of the height. No gap: each panel's own
// ruler strips separate the panels.
[[nodiscard]] std::vector<dvv::media::PixelRect> panelRects(dvv::platform::PixelSize extent,
                                                            PanelGrid grid);

// passRegions(panel's size, strips, view) with every rect moved by the panel's origin. `view` is
// resolved against the panel's picture region.
[[nodiscard]] PassRegions panelRegions(dvv::media::PixelRect panel, dvv::media::ViewStrips strips,
                                       const dvv::media::ViewTransform& view) noexcept;

// The first of `panels` that contains `point` (PixelRect::contains), none outside them all.
[[nodiscard]] std::optional<std::size_t> panelAt(std::span<const dvv::media::PixelRect> panels,
                                                 dvv::media::PointD point) noexcept;

}  // namespace rawreel
