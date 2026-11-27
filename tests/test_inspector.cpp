// The pixel inspector's model and pure rules (design §3, §4).

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

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
    CHECK(wholeZoom(2.4) == 2);
    CHECK(wholeZoom(2.6) == 3);
    CHECK(wholeZoom(900) == 256);
    CHECK(wholeZoom(0.2) == 1);
    CHECK(wholeZoom(std::numeric_limits<double>::quiet_NaN()) == 1);
    CHECK(nextWholeZoom(4, true) == 6);
    CHECK_FALSE(nextWholeZoom(1, false).has_value());
    CHECK(nextWholeZoom(5, true) == 6);
    CHECK(nextWholeZoom(5, false) == 4);
    CHECK_FALSE(nextWholeZoom(256, true).has_value());
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
