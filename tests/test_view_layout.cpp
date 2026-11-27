// How the view's panels tile the output and where each panel's passes draw.

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/pixel_size.h"
#include "render/pass_regions.h"
#include "render/view_layout.h"

using dvv::media::PixelRect;
using dvv::media::PointD;
using dvv::media::resolveView;
using dvv::media::ViewStrips;
using dvv::platform::PixelSize;
using rawreel::PanelGrid;
using rawreel::panelGrid;
using rawreel::panelRects;
using rawreel::PassRegions;

namespace {

PixelRect moved(PixelRect rect, PixelRect panel) {
    rect.x += panel.x;
    rect.y += panel.y;
    return rect;
}

}  // namespace

TEST_CASE("view layout: one panel is the whole extent") {
    const PixelSize extent{640, 360};
    for (const std::uint32_t count : {0U, 1U}) {
        CAPTURE(count);
        const PanelGrid grid = panelGrid(extent, count);
        CHECK(grid == PanelGrid{.columns = 1, .rows = 1});
        const std::vector<PixelRect> rects = panelRects(extent, grid);
        REQUIRE(rects.size() == 1);
        CHECK(rects[0] == PixelRect{.x = 0, .y = 0, .width = 640, .height = 360});
    }
}

TEST_CASE("view layout: four panels pick 2x2 or 1x4 by the smaller side") {
    CHECK(panelGrid({1600, 400}, 4) == PanelGrid{.columns = 4, .rows = 1});
    CHECK(panelGrid({800, 800}, 4) == PanelGrid{.columns = 2, .rows = 2});
    // A tie (1x4 panels 400x800, 2x2 panels 800x400: both smaller sides 400) is 2x2.
    CHECK(panelGrid({1600, 800}, 4) == PanelGrid{.columns = 2, .rows = 2});

    const std::vector<PixelRect> rects = panelRects({800, 800}, {.columns = 2, .rows = 2});
    REQUIRE(rects.size() == 4);  // row-major
    CHECK(rects[0] == PixelRect{.x = 0, .y = 0, .width = 400, .height = 400});
    CHECK(rects[1] == PixelRect{.x = 400, .y = 0, .width = 400, .height = 400});
    CHECK(rects[2] == PixelRect{.x = 0, .y = 400, .width = 400, .height = 400});
    CHECK(rects[3] == PixelRect{.x = 400, .y = 400, .width = 400, .height = 400});
}

TEST_CASE("view layout: three panels are 1x3 and tile the extent exactly") {
    const PixelSize extent{1001, 300};
    const PanelGrid grid = panelGrid(extent, 3);
    CHECK(grid == PanelGrid{.columns = 3, .rows = 1});
    const std::vector<PixelRect> rects = panelRects(extent, grid);
    REQUIRE(rects.size() == 3);
    CHECK(rects[0] == PixelRect{.x = 0, .y = 0, .width = 333, .height = 300});
    CHECK(rects[1] == PixelRect{.x = 333, .y = 0, .width = 334, .height = 300});
    CHECK(rects[2] == PixelRect{.x = 667, .y = 0, .width = 334, .height = 300});
    CHECK(panelGrid(extent, 2) == PanelGrid{.columns = 2, .rows = 1});
}

TEST_CASE("view layout: panelRegions moves passRegions by the panel origin") {
    const PixelRect panel{.x = 333, .y = 40, .width = 334, .height = 300};
    for (const ViewStrips strips : {ViewStrips{}, ViewStrips{.top = 18, .left = 24}}) {
        CAPTURE(strips.top);
        const PixelSize p = dvv::media::pictureRegion(panel.size(), strips).size();
        // Zoomed in: the picture covers P; zoomed out: the rulers hug a smaller picture.
        for (const double zoom : {4.0, 0.25}) {
            CAPTURE(zoom);
            const dvv::media::ViewTransform view =
                resolveView({.mode = dvv::media::ViewMode::Zoom, .zoom = zoom}, p, {200, 120});
            const PassRegions at_origin = rawreel::passRegions(panel.size(), strips, view);
            const PassRegions regions = rawreel::panelRegions(panel, strips, view);
            CHECK(regions.picture == moved(at_origin.picture, panel));
            CHECK(regions.pictureDrawn == moved(at_origin.pictureDrawn, panel));
            for (std::size_t b = 0; b < 2; ++b) {
                CHECK(regions.bands[b] == moved(at_origin.bands[b], panel));
                CHECK(regions.bandRulers[b] == moved(at_origin.bandRulers[b], panel));
            }
            CHECK(regions.rulers.hugged == moved(at_origin.rulers.hugged, panel));
            CHECK(regions.rulers.top == moved(at_origin.rulers.top, panel));
            CHECK(regions.rulers.left == moved(at_origin.rulers.left, panel));
            CHECK(regions.rulers.corner == moved(at_origin.rulers.corner, panel));
            // The panel at the output's origin is passRegions itself.
            CHECK(rawreel::panelRegions({.width = panel.width, .height = panel.height}, strips,
                                        view) == at_origin);
        }
    }
}

TEST_CASE("view layout: panelAt") {
    const std::vector<PixelRect> rects = panelRects({800, 800}, {.columns = 2, .rows = 2});
    CHECK(rawreel::panelAt(rects, PointD{.x = 0, .y = 0}) == std::optional<std::size_t>(0));
    CHECK(rawreel::panelAt(rects, PointD{.x = 399.5, .y = 10}) == std::optional<std::size_t>(0));
    CHECK(rawreel::panelAt(rects, PointD{.x = 400, .y = 10}) == std::optional<std::size_t>(1));
    CHECK(rawreel::panelAt(rects, PointD{.x = 10, .y = 400}) == std::optional<std::size_t>(2));
    CHECK(rawreel::panelAt(rects, PointD{.x = 799.9, .y = 799.9}) == std::optional<std::size_t>(3));
    CHECK_FALSE(rawreel::panelAt(rects, PointD{.x = 800, .y = 10}));
    CHECK_FALSE(rawreel::panelAt(rects, PointD{.x = -0.5, .y = 10}));
    CHECK_FALSE(rawreel::panelAt({}, PointD{.x = 1, .y = 1}));
}
