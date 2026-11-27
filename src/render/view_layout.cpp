#include "render/view_layout.h"

#include <algorithm>
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

namespace {

// Edge i of `parts` equal parts of `length`: length*i/parts (exact for every output size).
std::uint32_t edge(std::uint32_t length, std::uint32_t i, std::uint32_t parts) noexcept {
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(length) * i / parts);
}

void move(dvv::media::PixelRect& rect, dvv::media::PixelRect panel) noexcept {
    rect.x += panel.x;
    rect.y += panel.y;
}

}  // namespace

PanelGrid panelGrid(dvv::platform::PixelSize extent, std::uint32_t count) noexcept {
    if (count <= 1) {
        return {};
    }
    if (count == 4) {
        // The smaller sides times 4: 1x4 panels are w/4 x h, 2x2 panels w/2 x h/2.
        const std::uint64_t w = extent.width;
        const std::uint64_t h = extent.height;
        const std::uint64_t row = std::min(w, 4 * h);
        const std::uint64_t square = 2 * std::min(w, h);
        return row > square ? PanelGrid{.columns = 4, .rows = 1}
                            : PanelGrid{.columns = 2, .rows = 2};
    }
    return {.columns = count, .rows = 1};
}

std::vector<dvv::media::PixelRect> panelRects(dvv::platform::PixelSize extent, PanelGrid grid) {
    std::vector<dvv::media::PixelRect> rects;
    if (grid.columns == 0 || grid.rows == 0) {
        return rects;
    }
    rects.reserve(static_cast<std::size_t>(grid.columns) * grid.rows);
    for (std::uint32_t j = 0; j < grid.rows; ++j) {
        const std::uint32_t y = edge(extent.height, j, grid.rows);
        const std::uint32_t height = edge(extent.height, j + 1, grid.rows) - y;
        for (std::uint32_t i = 0; i < grid.columns; ++i) {
            const std::uint32_t x = edge(extent.width, i, grid.columns);
            rects.push_back({.x = x,
                             .y = y,
                             .width = edge(extent.width, i + 1, grid.columns) - x,
                             .height = height});
        }
    }
    return rects;
}

PassRegions panelRegions(dvv::media::PixelRect panel, dvv::media::ViewStrips strips,
                         const dvv::media::ViewTransform& view) noexcept {
    PassRegions regions = passRegions(panel.size(), strips, view);
    if (panel.x == 0 && panel.y == 0) {
        return regions;
    }
    move(regions.picture, panel);
    move(regions.pictureDrawn, panel);
    for (dvv::media::PixelRect& band : regions.bands) {
        move(band, panel);
    }
    for (dvv::media::PixelRect& rulers : regions.bandRulers) {
        move(rulers, panel);
    }
    move(regions.rulers.hugged, panel);
    move(regions.rulers.top, panel);
    move(regions.rulers.left, panel);
    move(regions.rulers.corner, panel);
    return regions;
}

std::optional<std::size_t> panelAt(std::span<const dvv::media::PixelRect> panels,
                                   dvv::media::PointD point) noexcept {
    for (std::size_t i = 0; i < panels.size(); ++i) {
        if (panels[i].contains(point)) {
            return i;
        }
    }
    return std::nullopt;
}

}  // namespace rawreel
