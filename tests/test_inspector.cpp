// The pixel inspector's model and pure rules (design §3, §4).

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

#include "core/inspector.h"
#include "core/overlay.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/sample_grid.h"
#include "dvv/media/view.h"
#include "dvv/platform/pixel_size.h"

namespace {

using namespace rawreel;
using dvv::media::ImagePixel;

ClipGrid clipOf(std::string_view format, std::uint32_t width, std::uint32_t height) {
    const auto id = dvv::media::findFormat(format);
    REQUIRE(id.has_value());
    return {.image = {.width = width, .height = height}, .format = id.value_or(0)};
}

const ValueMetrics METRICS{.advance = 14, .lineHeight = 28, .padX = 4, .padY = 4};

}  // namespace

TEST_CASE("inspector: shown is the probe while live") {
    Inspector inspector;
    CHECK_FALSE(inspector.shown().has_value());
    inspector.setPin(ImagePixel{.x = 1, .y = 2});
    inspector.setProbe(ImagePixel{.x = 5, .y = 6});
    CHECK(inspector.shown() == ImagePixel{.x = 1, .y = 2});
    inspector.setLive(true);
    CHECK(inspector.shown() == ImagePixel{.x = 5, .y = 6});
    inspector.setLive(false);
    CHECK(inspector.shown() == ImagePixel{.x = 1, .y = 2});
}

TEST_CASE("inspector: the probe stays when the cursor leaves") {
    Inspector inspector;
    inspector.setProbe(ImagePixel{.x = 5, .y = 6});
    inspector.setProbe(std::nullopt);
    CHECK(inspector.probe() == ImagePixel{.x = 5, .y = 6});
    inspector.setLive(true);
    CHECK(inspector.shown() == ImagePixel{.x = 5, .y = 6});
}

TEST_CASE("inspector: pixel moves clamp at the image edge") {
    const ClipGrid clip = clipOf("yuv420p", 37, 23);
    Inspector inspector;
    CHECK_FALSE(inspector.movePin(1, 0, false, clip));
    inspector.setPin(ImagePixel{.x = 35, .y = 1});
    CHECK(inspector.movePin(5, -5, false, clip));
    CHECK(inspector.pin() == ImagePixel{.x = 36, .y = 0});
    CHECK(inspector.movePin(-100, 100, false, clip));
    CHECK(inspector.pin() == ImagePixel{.x = 0, .y = 22});
}

TEST_CASE("inspector: block moves clamp at a clipped chroma block") {
    const ClipGrid clip = clipOf("yuv420p", 37, 23);
    Inspector inspector;
    inspector.setPin(ImagePixel{.x = 35, .y = 21});
    CHECK(inspector.movePin(1, 1, true, clip));
    CHECK(inspector.pin() == ImagePixel{.x = 36, .y = 22});
    CHECK(inspector.movePin(1, 1, true, clip));
    CHECK(inspector.pin() == ImagePixel{.x = 36, .y = 22});
    CHECK(inspector.movePin(-1, -1, true, clip));
    CHECK(inspector.pin() == ImagePixel{.x = 34, .y = 20});
    const dvv::media::PixelBlock block = chromaBlockAt(clip, ImagePixel{.x = 36, .y = 22});
    CHECK(block == dvv::media::PixelBlock{.x = 36, .y = 22, .width = 1, .height = 1});
    CHECK(chromaBlockAt(clipOf("rgb24", 8, 8), ImagePixel{.x = 3, .y = 4}) ==
          dvv::media::PixelBlock{.x = 3, .y = 4, .width = 1, .height = 1});
}

TEST_CASE("inspector: a new clip keeps or drops the pin") {
    Inspector inspector;
    inspector.setPin(ImagePixel{.x = 30, .y = 10});
    inspector.clipChanged({.width = 64, .height = 64});
    CHECK(inspector.pin() == ImagePixel{.x = 30, .y = 10});
    inspector.clipChanged({.width = 20, .height = 64});
    CHECK_FALSE(inspector.pin().has_value());
}

TEST_CASE("inspector: whole zoom rounding and steps") {
    CHECK(wholeZoom(2.4, 1) == 2);
    CHECK(wholeZoom(2.6, 1) == 3);
    CHECK(wholeZoom(900, 1) == 256);
    CHECK(wholeZoom(0.2, 1) == 1);
    CHECK(wholeZoom(std::numeric_limits<double>::quiet_NaN(), 1) == 1);
    CHECK(wholeZoom(0.2, 0) == 1);  // a minimum of 0 counts as 1
    CHECK(nextWholeZoom(4, true, 1) == 6);
    CHECK_FALSE(nextWholeZoom(1, false, 1).has_value());
    CHECK(nextWholeZoom(5, true, 1) == 6);
    CHECK(nextWholeZoom(5, false, 1) == 4);
    CHECK_FALSE(nextWholeZoom(256, true, 1).has_value());
}

TEST_CASE("inspector: the zoom-out floor shows at most 3 x 3 major cells") {
    using dvv::platform::PixelSize;
    const auto floor_of = [](PixelSize picture, std::uint32_t step, PixelSize image = {}) {
        const std::array pictures{picture};
        return minZoom(pictures, step, image);
    };
    CHECK(floor_of({.width = 900, .height = 600}, 32) == 10);  // ceil(900 / 96)
    CHECK(floor_of({.width = 600, .height = 900}, 32) == 10);  // the larger side, either axis
    CHECK(floor_of({.width = 960, .height = 960}, 32) == 10);  // exactly 3 cells: no round up
    CHECK(floor_of({.width = 961, .height = 10}, 32) == 11);
    CHECK(floor_of({.width = 386, .height = 1112}, 32) == 12);  // one tall Planes panel
    CHECK(floor_of({.width = 300, .height = 200}, 256) == 1);   // a large step on a small P
    CHECK(floor_of({.width = 100000, .height = 10}, 2) == WHOLE_ZOOM_MAX);  // clamped at 256
    CHECK(floor_of({}, 32) == 1);                                           // no picture
    CHECK(floor_of({.width = 300, .height = 200}, 0) == 100);  // a step of 0 counts as 1
    CHECK(minZoom({}, 32, {.width = 90, .height = 90}) == 1);  // no panel
    // The largest panel sets the cell floor.
    const std::array panels{PixelSize{.width = 386, .height = 1112},
                            PixelSize{.width = 385, .height = 1112},
                            PixelSize{.width = 900, .height = 1250}};
    CHECK(minZoom(panels, 32, {.width = 4000, .height = 3000}) == 14);  // ceil(1250 / 96)
    // A clip larger than 3 x 3 major cells on either axis keeps the cell floor.
    CHECK(floor_of({.width = 1250, .height = 900}, 32, {.width = 97, .height = 90}) == 14);
    CHECK(floor_of({.width = 1250, .height = 900}, 32, {.width = 90, .height = 97}) == 14);
    // A clip of at most 3 x 3 cells zooms out until it shows whole: floor(900 / 96) = 9 < 14.
    CHECK(floor_of({.width = 1250, .height = 900}, 32, {.width = 96, .height = 96}) == 9);
    CHECK(floor_of({.width = 1250, .height = 900}, 32, {.width = 90, .height = 90}) == 10);
    // A tiny clip shows whole at the cell floor already: the cell floor stays.
    CHECK(floor_of({.width = 1250, .height = 900}, 32, {.width = 20, .height = 20}) == 14);
    // A small clip larger than P still has the floor 1.
    CHECK(floor_of({.width = 80, .height = 50}, 32, {.width = 96, .height = 96}) == 1);
    // In every panel: the panel where the clip fits least sets it.
    CHECK(minZoom(panels, 32, {.width = 96, .height = 96}) == 4);  // floor(385 / 96)
    // Zoom out from above lands exactly on the floor and stops there; zoom in from it goes on.
    const std::uint32_t floor = floor_of({.width = 900, .height = 600}, 32);
    std::vector<std::uint32_t> out;
    for (std::optional<std::uint32_t> z = 64; z; z = nextWholeZoom(*z, false, floor)) {
        out.push_back(*z);
    }
    CHECK(out == std::vector<std::uint32_t>{64, 48, 32, 24, 16, 12, 10});
    CHECK_FALSE(nextWholeZoom(10, false, 10).has_value());
    CHECK_FALSE(nextWholeZoom(7, false, 10).has_value());  // below the floor: no step out
    CHECK(nextWholeZoom(10, true, 10) == 12);
    CHECK(nextWholeZoom(4, true, 10) == 10);  // in from below the floor: to the floor at least
    CHECK(nextWholeZoom(13, false, 12) == 12);
    CHECK_FALSE(nextWholeZoom(256, true, WHOLE_ZOOM_MAX).has_value());
    CHECK_FALSE(nextWholeZoom(256, false, WHOLE_ZOOM_MAX).has_value());
    // A requested zoom within [floor, 256].
    CHECK(wholeZoom(3.0, 10) == 10);
    CHECK(wholeZoom(14.4, 10) == 14);
    CHECK(wholeZoom(900, 10) == WHOLE_ZOOM_MAX);
    CHECK(wholeZoom(std::numeric_limits<double>::quiet_NaN(), 10) == 10);
}

TEST_CASE("inspector: the whole fit zoom of a clip in the panels") {
    using dvv::platform::PixelSize;
    const std::array one{PixelSize{.width = 1250, .height = 900}};
    CHECK(wholeFitZoom(one, {.width = 96, .height = 96}) == 9);             // floor(900 / 96)
    CHECK(wholeFitZoom(one, {.width = 100, .height = 10}) == 12);           // floor(1250 / 100)
    CHECK(wholeFitZoom(one, {.width = 2, .height = 1}) == WHOLE_ZOOM_MAX);  // clamped at 256
    CHECK(wholeFitZoom(one, {.width = 4000, .height = 3000}) == 1);         // larger than P: 1
    CHECK(wholeFitZoom(one, {}) == 1);                                      // no image
    CHECK(wholeFitZoom({}, {.width = 96, .height = 96}) == 1);              // no panel
    const std::array two{PixelSize{.width = 400, .height = 900},
                         PixelSize{.width = 380, .height = 900}};
    CHECK(wholeFitZoom(two, {.width = 96, .height = 96}) == 3);  // floor(380 / 96)
}

TEST_CASE("inspector: follow centre is the middle of the pixel") {
    CHECK(followCentre({.x = 3, .y = 7}) == dvv::media::PointD{.x = 3.5, .y = 7.5});
}

TEST_CASE("inspector: cell view picks the coarsest fitting level") {
    const GridConfig grid;
    const dvv::platform::PixelSize picture{.width = 700, .height = 900};
    const dvv::platform::PixelSize image{.width = 1000, .height = 1000};
    const ImagePixel pin{.x = 70, .y = 10};
    const auto level0 = cellView(pin, grid, 20, picture, image);
    REQUIRE(level0.has_value());
    CHECK(level0.value_or(CellView{}).level == 0);
    CHECK(level0.value_or(CellView{}).centre == dvv::media::PointD{.x = 80, .y = 16});
    const auto level1 = cellView(pin, grid, 25, picture, image);
    REQUIRE(level1.has_value());
    CHECK(level1.value_or(CellView{}).level == 1);
    CHECK_FALSE(cellView(pin, grid, 400, picture, image).has_value());
}

TEST_CASE("inspector: numbers fit") {
    CHECK(numbersFitZoom(METRICS, InspectorLayout::Structure, clipOf("yuv420p10le", 64, 64)) == 95);
    CHECK(numbersFitZoom(METRICS, InspectorLayout::Structure, clipOf("gray10le", 64, 64)) == 64);
    CHECK(numbersFitZoom(METRICS, InspectorLayout::Structure, clipOf("rgba64le", 64, 64)) == 120);
    CHECK(numbersFitZoom(METRICS, InspectorLayout::Planes, clipOf("yuv420p10le", 64, 64)) == 64);
}

TEST_CASE("inspector: numbers fit, labels only") {
    dvv::media::PixelFormatDesc desc;
    desc.componentCount = 1;
    desc.components[0] = {.kind = dvv::media::ComponentKind::U, .bits = 1, .log2SubX = 1};
    // The width needs ceil((2 + 1) * 14 + 8, 2) = 25; the label line needs 28 + 4 above the
    // pads, 28 + 4 + 8 = 40.
    CHECK(numbersFitZoom(METRICS, InspectorLayout::Structure, desc) == 40);
    // A one-digit line over the label: 28 + 8, and the label's 28 + 4, = 68 (the width 22).
    desc.componentCount = 2;
    desc.components[1] = desc.components[0];
    desc.components[0] = {.kind = dvv::media::ComponentKind::Y, .bits = 1};
    CHECK(numbersFitZoom(METRICS, InspectorLayout::Structure, desc) == 68);
}

TEST_CASE("inspector: fit cell") {
    const GridConfig grid;
    CHECK(fitCellZoom(grid, {.width = 700, .height = 900}) == 21);
    CHECK(fitCellZoom(grid, {.width = 10, .height = 10}) == 1);
}

TEST_CASE("inspector: what recentres") {
    CHECK(recentres(InspectorEvent::FirstPin));
    CHECK(recentres(InspectorEvent::PinMoved));
    CHECK(recentres(InspectorEvent::ProbeMoved));
    CHECK(recentres(InspectorEvent::ModeChanged));
    CHECK(recentres(InspectorEvent::Centre));
    CHECK_FALSE(recentres(InspectorEvent::Other));
}
