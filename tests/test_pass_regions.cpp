// How the picture pass and the two ruler bands split the output.

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/pixel_size.h"
#include "render/pass_regions.h"

using dvv::media::PixelRect;
using dvv::media::resolveView;
using dvv::media::ViewMode;
using dvv::media::ViewStrips;
using dvv::media::ViewTransform;
using dvv::platform::PixelSize;
using rawreel::PassRegions;
using rawreel::passRegions;

namespace {

bool inside(const PixelRect& rect, std::uint32_t x, std::uint32_t y) {
    return x >= rect.x && y >= rect.y && x - rect.x < rect.width && y - rect.y < rect.height;
}

// `inner` lies in `outer` (an empty `inner` too, wherever it sits).
bool within(const PixelRect& inner, const PixelRect& outer) {
    return inner.empty() || (inner.x >= outer.x && inner.y >= outer.y &&
                             inner.x + inner.width <= outer.x + outer.width &&
                             inner.y + inner.height <= outer.y + outer.height);
}

// The view of `image` at `zoom` (0: Fit), centred in P of `extent` and `strips`.
ViewTransform viewOf(PixelSize extent, ViewStrips strips, PixelSize image, double zoom) {
    const PixelSize p = dvv::media::pictureRegion(extent, strips).size();
    if (zoom == 0) {
        return resolveView({}, p, image);
    }
    return resolveView({ViewMode::Zoom, zoom, {image.width / 2.0, image.height / 2.0}, image}, p,
                       image);
}

}  // namespace

TEST_CASE("pass regions: the passes split the output") {
    const std::array<PixelSize, 4> extents{PixelSize{200, 100}, PixelSize{211, 97}, PixelSize{8, 8},
                                           PixelSize{100, 8}};
    const std::array<ViewStrips, 3> strips{ViewStrips{0, 0}, ViewStrips{10, 20},
                                           ViewStrips{30, 30}};
    struct Case {
        PixelSize image;
        double zoom;
    };
    const std::array<Case, 4> cases{Case{{100, 50}, 1}, Case{{100, 50}, 4}, Case{{100, 200}, 0},
                                    Case{{}, 0}};
    for (const PixelSize extent : extents) {
        for (const ViewStrips strip : strips) {
            for (const Case& c : cases) {
                const PassRegions r =
                    passRegions(extent, strip, viewOf(extent, strip, c.image, c.zoom));
                CHECK(r.picture == dvv::media::pictureRegion(extent, strip));
                // No region overshoots: the counts below see only the output's pixels.
                const PixelRect output{0, 0, extent.width, extent.height};
                CHECK(within(r.pictureDrawn, r.picture));
                CHECK(within(r.bands[0], output));
                CHECK(within(r.bands[1], output));
                CHECK(within(r.bandRulers[0], r.bands[0]));
                CHECK(within(r.bandRulers[1], r.bands[1]));
                if (strip != ViewStrips{}) {
                    CHECK(r.bandRulers[1] == r.rulers.left);
                }
                std::size_t wrong = 0;
                for (std::uint32_t y = 0; y < extent.height; ++y) {
                    for (std::uint32_t x = 0; x < extent.width; ++x) {
                        const int count = (inside(r.pictureDrawn, x, y) ? 1 : 0) +
                                          (inside(r.bands[0], x, y) ? 1 : 0) +
                                          (inside(r.bands[1], x, y) ? 1 : 0);
                        wrong += count == 1 ? 0 : 1;
                        // The top band's rulers: the top ruler and the corner, nothing else.
                        const bool ruler =
                            inside(r.rulers.top, x, y) || inside(r.rulers.corner, x, y);
                        wrong += inside(r.bandRulers[0], x, y) == ruler ? 0 : 1;
                    }
                }
                CHECK(wrong == 0);
                if (strip == ViewStrips{}) {
                    CHECK(r.pictureDrawn == r.picture);
                    CHECK(r.bands[0].empty());
                    CHECK(r.bands[1].empty());
                    CHECK(r.bandRulers[0].empty());
                    CHECK(r.bandRulers[1].empty());
                }
            }
        }
    }
}

TEST_CASE("pass regions: a picture covering P keeps the old regions") {
    const PixelSize extent{200, 100};
    const ViewStrips strips{.top = 10, .left = 20};
    const PassRegions r = passRegions(extent, strips, viewOf(extent, strips, {100, 50}, 4));
    CHECK(r.pictureDrawn == r.picture);
    CHECK(r.bands[0] == PixelRect{0, 0, 200, 10});
    CHECK(r.bands[1] == PixelRect{0, 10, 20, 90});
    CHECK(r.rulers.hugged == r.picture);
}

TEST_CASE("pass regions: a centred smaller picture") {
    const PixelSize extent{200, 100};
    const ViewStrips strips{.top = 10, .left = 20};
    const PassRegions r = passRegions(extent, strips, viewOf(extent, strips, {100, 50}, 1));
    CHECK(r.pictureDrawn == PixelRect{60, 30, 140, 70});
    CHECK(r.bands[0] == PixelRect{0, 0, 200, 30});
    CHECK(r.bands[1] == PixelRect{0, 30, 60, 70});
    CHECK(r.rulers.hugged == PixelRect{60, 30, 100, 50});
    // A cursor move redraws the top ruler with the corner, and the left ruler.
    CHECK(r.bandRulers[0] == PixelRect{40, 20, 120, 10});
    CHECK(r.bandRulers[1] == PixelRect{40, 30, 20, 50});
}
