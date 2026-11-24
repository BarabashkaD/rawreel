// The ruler pass's push constants, their reflection and its CPU reference.

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "composite_reference.h"
#include "dvv/base/json.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/pixel_size.h"
#include "rawreel_shaders/rulers.h"
#include "render/composite_constants.h"
#include "render/display_state.h"
#include "render/pass_regions.h"
#include "render/ruler_constants.h"
#include "ruler_reference.h"

using dvv::media::ImagePixel;
using dvv::media::resolveView;
using dvv::media::ViewMode;
using dvv::media::ViewTransform;
using dvv::platform::PixelSize;
using rawreel::RULER_CURSOR;
using rawreel::RULER_IMAGE;
using rawreel::RULER_LIT;
using rawreel::RULER_TICKS;
using rawreel::RulerConstants;
using rawreel::rulerPixel;

namespace {

using Rgba8 = std::array<std::uint8_t, 4>;

constexpr Rgba8 hex(std::uint32_t packed) {
    return {static_cast<std::uint8_t>(packed >> 16U), static_cast<std::uint8_t>(packed >> 8U),
            static_cast<std::uint8_t>(packed), 255};
}

// Retina values (2 backing pixels per pt) of steps 32 / 8 / 2 and minimum spacings 12 / 4 / 3 pt;
// strips of 30 with a tick band of 20, the lit tick 4 pt (8 px) deeper.
rawreel::OverlayParams retina() {
    rawreel::OverlayParams overlay;
    overlay.levels[0] = {.stepLog2 = 5, .minimumSpacing = 24};
    overlay.levels[1] = {.stepLog2 = 3, .minimumSpacing = 8};
    overlay.levels[2] = {.stepLog2 = 1, .minimumSpacing = 6};
    overlay.strips = {.top = 30, .left = 30};
    overlay.tickBand = 20;
    overlay.tickWidth = 2;
    overlay.markerDepth = 6;
    overlay.litDepth = 28;
    overlay.tickLength = {20, 12, 6};
    overlay.cursorColour = 0xff3b30;
    return overlay;
}

// The output pixel of the top band at axis pixel `along` (from P's edge) and `depth` (0 next
// to P).
Rgba8 topAt(const RulerConstants& band, std::int32_t along, std::uint32_t depth) {
    return rulerPixel(band, band.pictureStart + static_cast<std::uint32_t>(along),
                      band.strip - 1 - depth);
}

// The two bands as the renderer computes them: from passRegions of the extent, the strips and the
// view.
std::array<RulerConstants, 2> bandsOf(const rawreel::OverlayParams& overlay,
                                      const ViewTransform& view, PixelSize extent,
                                      std::optional<ImagePixel> cursor, bool skip_encode) {
    return rawreel::rulerConstants(
        overlay, view, rawreel::passRegions(extent, overlay.strips, view), cursor, skip_encode);
}

}  // namespace

TEST_CASE("ruler constants: layout") {
    CHECK(offsetof(RulerConstants, regionX) == 0);
    CHECK(offsetof(RulerConstants, regionY) == 4);
    CHECK(offsetof(RulerConstants, regionW) == 8);
    CHECK(offsetof(RulerConstants, regionH) == 12);
    CHECK(offsetof(RulerConstants, axis) == 16);
    CHECK(offsetof(RulerConstants, flags) == 20);
    CHECK(offsetof(RulerConstants, zoom) == 24);
    CHECK(offsetof(RulerConstants, offset) == 28);
    CHECK(offsetof(RulerConstants, pictureStart) == 32);
    CHECK(offsetof(RulerConstants, pictureExtent) == 36);
    CHECK(offsetof(RulerConstants, imageSize) == 40);
    CHECK(offsetof(RulerConstants, strip) == 44);
    CHECK(offsetof(RulerConstants, tickBand) == 48);
    CHECK(offsetof(RulerConstants, tickWidth) == 52);
    CHECK(offsetof(RulerConstants, markerDepth) == 56);
    CHECK(offsetof(RulerConstants, cursor) == 60);
    CHECK(offsetof(RulerConstants, labelBand) == 64);
    CHECK(offsetof(RulerConstants, rulerEdge) == 68);
    CHECK(offsetof(RulerConstants, rulerBegin) == 72);
    CHECK(offsetof(RulerConstants, rulerEnd) == 76);
    CHECK(offsetof(RulerConstants, levelShape) == 80);
    CHECK(offsetof(RulerConstants, tickLength) == 96);
    CHECK(offsetof(RulerConstants, cursorColour) == 112);
    CHECK(offsetof(RulerConstants, litTick) == 116);
    CHECK(offsetof(RulerConstants, litDepth) == 120);
    CHECK(sizeof(RulerConstants) == 128);
}

TEST_CASE("rulers shader: the push block matches RulerConstants") {
    const auto reflection = dvv::json::parse(rawreel_shaders::rulers().reflectionJson);
    REQUIRE(reflection.has_value());
    const dvv::json::Value* block = nullptr;
    for (const auto& parameter : reflection->at("parameters")) {
        if (dvv::json::get<std::string>(parameter.at("binding"), "kind") == "pushConstantBuffer") {
            block = &parameter;
        }
    }
    REQUIRE(block != nullptr);
    const auto& type = block->at("type");
    CHECK(dvv::json::get<std::uint32_t>(type.at("elementVarLayout").at("binding"), "size") == 128U);
    std::map<std::string, std::uint32_t> offsets;
    for (const auto& field : type.at("elementType").at("fields")) {
        offsets[dvv::json::get<std::string>(field, "name").value_or("?")] =
            dvv::json::get<std::uint32_t>(field.at("binding"), "offset").value_or(~0U);
    }
    const std::map<std::string, std::uint32_t> expected{
        {"regionX", 0},       {"regionY", 4},        {"regionW", 8},        {"regionH", 12},
        {"axis", 16},         {"flags", 20},         {"zoom", 24},          {"offset", 28},
        {"pictureStart", 32}, {"pictureExtent", 36}, {"imageSize", 40},     {"strip", 44},
        {"tickBand", 48},     {"tickWidth", 52},     {"markerDepth", 56},   {"cursor", 60},
        {"labelBand", 64},    {"rulerEdge", 68},     {"rulerBegin", 72},    {"rulerEnd", 76},
        {"levelShape", 80},   {"tickLength", 96},    {"cursorColour", 112}, {"litTick", 116},
        {"litDepth", 120},    {"reserved1", 124}};
    CHECK(offsets == expected);
}

TEST_CASE("ruler constants: the bands") {
    const rawreel::OverlayParams overlay = retina();
    const PixelSize extent{211, 97};
    const ViewTransform view = ViewTransform::fit({181, 67}, {37, 23});
    const auto bands = bandsOf(overlay, view, extent, ImagePixel{4, 5}, true);
    CHECK(bands[0].regionX == 0);
    CHECK(bands[0].regionY == 0);
    CHECK(bands[0].regionW == 211);
    CHECK(bands[0].regionH == 30);
    CHECK(bands[0].axis == 0);
    CHECK(bands[0].pictureStart == 30);
    CHECK(bands[0].pictureExtent == 181);
    CHECK(bands[0].imageSize == 37);
    CHECK(bands[0].offset == dvv::media::axisX(view).offset);
    CHECK(bands[0].cursor == 4);
    CHECK(bands[0].labelBand == 10);
    CHECK(bands[1].regionX == 0);
    CHECK(bands[1].regionY == 30);
    // The picture is narrower than P: the left band reaches the hugged rect.
    CHECK(bands[1].regionW == 66);  // H.x: P.x 30 and the Fit offset 36
    CHECK(bands[1].regionH == 67);
    CHECK(bands[1].axis == 1);
    CHECK(bands[1].pictureStart == 30);
    CHECK(bands[1].pictureExtent == 67);
    CHECK(bands[1].imageSize == 23);
    CHECK(bands[1].cursor == 5);
    for (const RulerConstants& band : bands) {
        CHECK((band.flags & rawreel::RULER_SKIP_ENCODE) != 0);
        CHECK((band.flags & rawreel::RULER_DARK) == 0);
        CHECK((band.flags & RULER_CURSOR) != 0);
        CHECK((band.flags & RULER_IMAGE) != 0);
        CHECK(band.tickLength == std::array<std::uint32_t, 4>{20, 12, 6, 0});
    }
    // An extent smaller than the strips: the top band clipped to it, the left band empty.
    const auto small = bandsOf(overlay, view, {8, 8}, std::nullopt, false);
    CHECK(small[0].regionW == 8);
    CHECK(small[0].regionH == 8);
    CHECK(small[0].strip == 8);  // clamped to the extent
    CHECK(small[1].regionW * small[1].regionH == 0);
    // Hidden rulers: both bands empty.
    rawreel::OverlayParams hidden = overlay;
    hidden.strips = {};
    for (const RulerConstants& band : bandsOf(hidden, view, extent, std::nullopt, false)) {
        CHECK(band.regionW * band.regionH == 0);
    }
}

TEST_CASE("ruler constants: the bands take the view's axes, as the grid does") {
    // A view resolved against another size than P (a caller error): the bands still follow the
    // view, as compositeConstants does, so the ticks cannot drift from the grid lines.
    const rawreel::OverlayParams overlay = retina();
    const ViewTransform view = ViewTransform::fit({181, 67}, {37, 23});
    const PixelSize extent{300, 200};
    const auto bands = bandsOf(overlay, view, extent, std::nullopt, false);
    const rawreel::CompositeConstants grid = rawreel::compositeConstants(
        overlay, view, rawreel::passRegions(extent, overlay.strips, view), false);
    for (std::uint32_t axis = 0; axis < 2; ++axis) {
        const dvv::media::AxisView expected =
            axis == 0 ? dvv::media::axisX(view) : dvv::media::axisY(view);
        const RulerConstants& band = bands[axis];
        CHECK(band.zoom == expected.zoom);
        CHECK(band.zoom == grid.zoom);
        CHECK(band.offset == expected.offset);
        CHECK(band.offset == (axis == 0 ? grid.offsetX : grid.offsetY));
        CHECK(band.imageSize == expected.size);
        CHECK(band.pictureExtent == expected.extent);
        CHECK(band.pictureExtent == (axis == 0 ? grid.viewportW : grid.viewportH));
        CHECK(band.pictureStart == 30);
    }
}

TEST_CASE("ruler reference: ticks, levels and the disabled ruler") {
    const rawreel::OverlayParams overlay = retina();
    const PixelSize extent{286, 158};  // P = 256 x 128, the image at zoom 1
    const ViewTransform view =
        resolveView({ViewMode::Zoom, 1, {128, 64}, {256, 128}}, {256, 128}, {256, 128});
    REQUIRE(view.offset() == dvv::media::PixelOffset{0, 0});
    const auto bands = bandsOf(overlay, view, extent, ImagePixel{44, 3}, false);
    const RulerConstants& top = bands[0];
    REQUIRE((top.flags & RULER_TICKS) != 0);  // 32 px >= 24
    CHECK((top.levelShape[0] & rawreel::LEVEL_DRAWN) != 0);
    CHECK((top.levelShape[1] & rawreel::LEVEL_DRAWN) != 0);  // 8 >= 8
    CHECK((top.levelShape[2] & rawreel::LEVEL_DRAWN) == 0);  // 2 < 6
    const Rgba8 background = hex(rawreel::RULER_BACKGROUND_LIGHT);
    // k = 32 covers axis pixels [31, 33) at width 2: a major tick 20 deep.
    CHECK(topAt(top, 31, 0) == hex(rawreel::RULER_MAJOR_LIGHT));
    CHECK(topAt(top, 32, 19) == hex(rawreel::RULER_MAJOR_LIGHT));
    CHECK(topAt(top, 32, 20) == background);  // the label band
    CHECK(topAt(top, 33, 5) == background);
    // k = 8: a minor tick 12 deep; k = 2: the second minor is hidden.
    CHECK(topAt(top, 8, 11) == hex(rawreel::RULER_MINOR_LIGHT));
    CHECK(topAt(top, 8, 12) == background);
    CHECK(topAt(top, 2, 3) == background);
    CHECK(topAt(top, 3, 0) == hex(rawreel::RULER_SEPARATOR_LIGHT));
    // The outer edge k = 0 inside the image; the corner left of P.
    CHECK(topAt(top, 0, 1) == hex(rawreel::RULER_MAJOR_LIGHT));
    CHECK(rulerPixel(top, 0, 0) == hex(rawreel::RULER_CORNER_LIGHT));
    CHECK(rulerPixel(top, 29, 29) == hex(rawreel::RULER_CORNER_LIGHT));
    // The marker at image pixel 44, 6 deep, over the background.
    CHECK(topAt(top, 44, 5) == Rgba8{251, 75, 65, 255});
    CHECK(topAt(top, 44, 6) == background);
    CHECK(topAt(top, 43, 5) == background);  // 45 lies in the lit tick 48

    // 400 % below: 8 px < 24, no tick; the marker still drawn.
    const ViewTransform quarter =
        resolveView({ViewMode::Zoom, 0.25, {128, 64}, {1024, 512}}, {256, 128}, {1024, 512});
    const auto far = bandsOf(overlay, quarter, extent, ImagePixel{44, 3}, false);
    CHECK((far[0].flags & RULER_TICKS) == 0);
    CHECK((far[1].flags & RULER_TICKS) == 0);
    std::size_t ticks = 0;
    for (std::uint32_t x = 30; x < extent.width; ++x) {
        ticks += rulerPixel(far[0], x, 10) == background ? 0 : 1;
    }
    CHECK(ticks == 0);
    const dvv::media::PixelSpan marker = dvv::media::cursorSpan(dvv::media::axisX(quarter), 44);
    REQUIRE_FALSE(marker.empty());
    CHECK(topAt(far[0], marker.begin, 0) != hex(rawreel::RULER_SEPARATOR_LIGHT));
    CHECK(topAt(far[0], marker.begin, 3) == Rgba8{251, 75, 65, 255});
}

TEST_CASE("ruler reference: the label-fit rule") {
    rawreel::OverlayParams overlay = retina();
    overlay.numbers = true;
    overlay.digitAdvance = 13.2F;
    const ViewTransform view =
        resolveView({ViewMode::Zoom, 0.8, {1920, 240}, {3840, 480}}, {800, 400}, {3840, 480});
    REQUIRE(static_cast<float>(view.zoom()) == 0.8F);
    const auto bands = bandsOf(overlay, view, {830, 430}, ImagePixel{1900, 100}, false);
    // 4 digits: 52.8 > 2 x 32 x 0.8 = 51.2; 3 digits: 39.6 fits.
    CHECK((bands[0].flags & RULER_TICKS) == 0);
    CHECK((bands[1].flags & RULER_TICKS) != 0);
    CHECK((bands[0].flags & RULER_CURSOR) != 0);
    CHECK((bands[1].flags & RULER_CURSOR) != 0);
    for (const RulerConstants& band : bands) {
        const dvv::media::AxisView axis =
            band.axis == 0 ? dvv::media::axisX(view) : dvv::media::axisY(view);
        const dvv::media::PixelSpan span = dvv::media::cursorSpan(axis, band.cursor);
        REQUIRE_FALSE(span.empty());
        const std::uint32_t along = band.pictureStart + static_cast<std::uint32_t>(span.begin);
        const std::uint32_t across = band.rulerEdge - 2;  // depth 1: over the background
        CHECK(rulerPixel(band, band.axis == 0 ? along : across, band.axis == 0 ? across : along) ==
              Rgba8{251, 75, 65, 255});
    }
    // Numbers off: both rulers show ticks.
    overlay.numbers = false;
    for (const RulerConstants& band : bandsOf(overlay, view, {830, 430}, std::nullopt, false)) {
        CHECK((band.flags & RULER_TICKS) != 0);
    }
}

TEST_CASE("ruler reference: plain strips without an image") {
    rawreel::OverlayParams overlay = retina();
    overlay.dark = true;
    const PixelSize extent{130, 90};
    const ViewTransform none = resolveView({}, {100, 60}, {});
    REQUIRE(none.zoom() == 0.0);
    const auto bands = bandsOf(overlay, none, extent, ImagePixel{1, 1}, false);
    std::size_t wrong = 0;
    for (const RulerConstants& band : bands) {
        CHECK((band.flags & (RULER_IMAGE | RULER_TICKS | RULER_CURSOR)) == 0);
        for (std::uint32_t y = band.regionY; y < band.regionY + band.regionH; ++y) {
            for (std::uint32_t x = band.regionX; x < band.regionX + band.regionW; ++x) {
                const std::uint32_t along = band.axis == 0 ? x : y;
                const std::uint32_t across = band.axis == 0 ? y : x;
                const Rgba8 expected = along < band.pictureStart ? hex(rawreel::RULER_CORNER_DARK)
                                       : across + 1 == band.strip
                                           ? hex(rawreel::RULER_SEPARATOR_DARK)
                                           : hex(rawreel::RULER_BACKGROUND_DARK);
                wrong += rulerPixel(band, x, y) == expected ? 0 : 1;
            }
        }
    }
    CHECK(wrong == 0);
}

TEST_CASE("ruler reference: the rulers hug a smaller picture") {
    rawreel::OverlayParams overlay = retina();
    overlay.dark = true;
    overlay.strips = {.top = 10, .left = 20};
    const PixelSize extent{200, 100};  // P = {20, 10, 180, 90}, H = {60, 30, 100, 50}
    const ViewTransform view =
        resolveView({ViewMode::Zoom, 1, {50, 25}, {100, 50}}, {180, 90}, {100, 50});
    const rawreel::PassRegions regions = rawreel::passRegions(extent, overlay.strips, view);
    REQUIRE(regions.rulers.hugged == dvv::media::PixelRect{60, 30, 100, 50});
    const auto bands = rawreel::rulerConstants(overlay, view, regions, std::nullopt, false);
    CHECK(bands[0].strip == 10);
    CHECK(bands[0].rulerEdge == 30);
    CHECK(bands[0].rulerBegin == 60);
    CHECK(bands[0].rulerEnd == 160);
    CHECK(bands[1].strip == 20);
    CHECK(bands[1].rulerEdge == 60);
    CHECK(bands[1].rulerBegin == 30);
    CHECK(bands[1].rulerEnd == 80);
    const auto grey = static_cast<std::uint8_t>(std::lround(255.0F * rawreel::BACKGROUND_DARK));
    const Rgba8 background{grey, grey, grey, 255};
    CHECK(rulerPixel(bands[0], 103, 25) == hex(rawreel::RULER_BACKGROUND_DARK));
    CHECK(rulerPixel(bands[0], 103, 29) == hex(rawreel::RULER_SEPARATOR_DARK));
    CHECK(rulerPixel(bands[0], 103, 19) == background);  // above the ruler
    CHECK(rulerPixel(bands[0], 100, 5) == background);   // the reserved strip
    CHECK(rulerPixel(bands[0], 165, 25) == background);  // past the ruler's end
    CHECK(rulerPixel(bands[0], 45, 25) == hex(rawreel::RULER_CORNER_DARK));
    CHECK(rulerPixel(bands[0], 39, 25) == background);  // left of the corner
    CHECK(rulerPixel(bands[1], 50, 43) == hex(rawreel::RULER_BACKGROUND_DARK));
    CHECK(rulerPixel(bands[1], 59, 43) == hex(rawreel::RULER_SEPARATOR_DARK));
    CHECK(rulerPixel(bands[1], 10, 40) == background);
    CHECK(rulerPixel(bands[1], 50, 85) == background);  // below the ruler
    // Every band pixel: a ruler colour exactly on the ruler rect the hit test uses, the corner
    // colour exactly on the corner, the background elsewhere, so the drawn rulers and the
    // clickable rects agree to the pixel.
    const auto on_ruler = [](const Rgba8& colour) {
        return colour == hex(rawreel::RULER_BACKGROUND_DARK) ||
               colour == hex(rawreel::RULER_SEPARATOR_DARK) ||
               colour == hex(rawreel::RULER_MAJOR_DARK) || colour == hex(rawreel::RULER_MINOR_DARK);
    };
    const std::array<dvv::media::PixelRect, 2> rulers{regions.rulers.top, regions.rulers.left};
    std::size_t wrong = 0;
    for (std::size_t b = 0; b < bands.size(); ++b) {
        const dvv::media::PixelRect band = regions.bands[b];
        for (std::uint32_t y = band.y; y < band.y + band.height; ++y) {
            for (std::uint32_t x = band.x; x < band.x + band.width; ++x) {
                const dvv::media::PointD centre{x + 0.5, y + 0.5};
                const Rgba8 colour = rulerPixel(bands[b], x, y);
                const bool corner = b == 0 && regions.rulers.corner.contains(centre);
                const bool ruler = rulers[b].contains(centre);
                const bool right = corner  ? colour == hex(rawreel::RULER_CORNER_DARK)
                                   : ruler ? on_ruler(colour)
                                           : colour == background;
                wrong += right ? 0 : 1;
            }
        }
    }
    CHECK(wrong == 0);
}

TEST_CASE("ruler reference: the lit tick") {
    rawreel::OverlayParams overlay = retina();
    overlay.tickWidth = 1;
    const PixelSize extent{286, 158};  // P = 256 x 128: the 64 x 32 image at zoom 4
    const ViewTransform view =
        resolveView({ViewMode::Zoom, 4, {32, 16}, {64, 32}}, {256, 128}, {64, 32});
    REQUIRE(view.offset() == dvv::media::PixelOffset{0, 0});
    const Rgba8 lit = hex(overlay.cursorColour);
    const auto top = bandsOf(overlay, view, extent, ImagePixel{5, 3}, false)[0];
    REQUIRE((top.levelShape[2] & rawreel::LEVEL_DRAWN) != 0);  // 8 px >= 6
    REQUIRE((top.flags & RULER_LIT) != 0);
    CHECK(top.litTick == 6);
    CHECK(top.litDepth == 28);
    // Image coordinate 6 at axis pixel 24: three tick widths of 1 light [23, 26), through the
    // tick band and 4 pt (8 px) into the label band.
    for (const std::uint32_t depth : {0U, 5U, 19U, 20U, 27U}) {
        CAPTURE(depth);
        CHECK(topAt(top, 23, depth) == lit);
        CHECK(topAt(top, 24, depth) == lit);
        CHECK(topAt(top, 25, depth) == lit);
        CHECK(topAt(top, 26, depth) != lit);
    }
    CHECK(topAt(top, 22, 5) != lit);  // the marker's blend, not the lit colour
    CHECK(topAt(top, 22, 27) != lit);
    CHECK(topAt(top, 24, 28) == hex(rawreel::RULER_BACKGROUND_LIGHT));

    // Three tick widths of 2 light [21, 27).
    overlay.tickWidth = 2;
    const auto wide = bandsOf(overlay, view, extent, ImagePixel{5, 3}, false)[0];
    REQUIRE((wide.flags & RULER_LIT) != 0);
    for (const std::uint32_t depth : {5U, 27U}) {
        CAPTURE(depth);
        for (std::int32_t along = 21; along < 27; ++along) {
            CHECK(topAt(wide, along, depth) == lit);
        }
    }
    CHECK(topAt(wide, 20, 27) != lit);
    CHECK(topAt(wide, 27, 27) != lit);
    CHECK(topAt(wide, 24, 28) != lit);

    // c = 0: the major tick at the outer edge, the lit tick inside the image from its first pixel
    // as every outer-edge line, and longer than the major tick, which fills the tick band here.
    overlay.tickWidth = 1;
    const auto edge = bandsOf(overlay, view, extent, ImagePixel{0, 3}, false)[0];
    REQUIRE((edge.flags & RULER_LIT) != 0);
    CHECK(edge.litTick == 0);
    CHECK(topAt(edge, 0, 27) == lit);
    CHECK(topAt(edge, 2, 27) == lit);
    CHECK(topAt(edge, 3, 27) != lit);
    CHECK(topAt(edge, 0, 28) != lit);
    // c = 63: the lit tick 64 at the far outer edge, inside the image up to its last pixel, as
    // every outer-edge line: [253, 256).
    const auto last_pixel = bandsOf(overlay, view, extent, ImagePixel{63, 3}, false)[0];
    REQUIRE((last_pixel.flags & RULER_LIT) != 0);
    CHECK(last_pixel.litTick == 64);
    CHECK(topAt(last_pixel, 253, 27) == lit);
    CHECK(topAt(last_pixel, 254, 27) == lit);
    CHECK(topAt(last_pixel, 255, 27) == lit);
    CHECK(topAt(last_pixel, 252, 27) != lit);
    const auto unlit = bandsOf(overlay, view, extent, std::nullopt, false)[0];
    CHECK(topAt(unlit, 0, 19) == hex(rawreel::RULER_MAJOR_LIGHT));
    CHECK(topAt(unlit, 0, 20) == hex(rawreel::RULER_BACKGROUND_LIGHT));

    // Zoom 1, the second minor hidden: c = 13 lights the minor tick 16 as deep as every lit tick.
    const rawreel::OverlayParams plain = retina();
    const ViewTransform one =
        resolveView({ViewMode::Zoom, 1, {128, 64}, {256, 128}}, {256, 128}, {256, 128});
    const auto minor = bandsOf(plain, one, extent, ImagePixel{13, 3}, false)[0];
    REQUIRE((minor.levelShape[2] & rawreel::LEVEL_DRAWN) == 0);
    REQUIRE((minor.flags & RULER_LIT) != 0);
    CHECK(minor.litTick == 16);
    for (const std::uint32_t depth : {11U, 12U, 27U}) {
        CAPTURE(depth);
        CHECK(topAt(minor, 13, depth) == lit);
        CHECK(topAt(minor, 18, depth) == lit);
        CHECK(topAt(minor, 12, depth) != lit);
        CHECK(topAt(minor, 19, depth) != lit);
    }
    CHECK(topAt(minor, 15, 28) == hex(rawreel::RULER_BACKGROUND_LIGHT));

    // A strip thinner than the lit depth: the lit tick stops at the strip's outer edge.
    rawreel::OverlayParams thin = retina();
    thin.strips = {.top = 24, .left = 24};
    const auto clipped = bandsOf(thin, one, {280, 152}, ImagePixel{13, 3}, false)[0];
    REQUIRE((clipped.flags & RULER_LIT) != 0);
    CHECK(clipped.strip == 24);
    CHECK(clipped.litDepth == 24);
    CHECK(topAt(clipped, 16, 23) == lit);

    // Ticks off at 25 %: no lit tick, the marker only.
    const ViewTransform quarter =
        resolveView({ViewMode::Zoom, 0.25, {128, 64}, {1024, 512}}, {256, 128}, {1024, 512});
    const auto far = bandsOf(plain, quarter, extent, ImagePixel{44, 3}, false);
    CHECK((far[0].flags & RULER_LIT) == 0);
    CHECK((far[1].flags & RULER_LIT) == 0);
    CHECK((far[0].flags & RULER_CURSOR) != 0);
}

TEST_CASE("ruler reference: the marker and the lit tick take the cursor colour") {
    rawreel::OverlayParams overlay = retina();
    overlay.cursorColour = 0x34c759;
    const PixelSize extent{286, 158};
    const ViewTransform view =
        resolveView({ViewMode::Zoom, 1, {128, 64}, {256, 128}}, {256, 128}, {256, 128});
    // c = 11: the lit tick 8 covers [5, 11), the marker at 11 lies beside it.
    const auto top = bandsOf(overlay, view, extent, ImagePixel{11, 3}, false)[0];
    REQUIRE((top.flags & RULER_LIT) != 0);
    REQUIRE(top.litTick == 8);
    CHECK(topAt(top, 7, 11) == hex(0x34c759));
    // The marker at 11, over the background: 0.9 of the way to the cursor colour.
    const Rgba8 background = hex(rawreel::RULER_BACKGROUND_LIGHT);
    const Rgba8 cursor = hex(0x34c759);
    const Rgba8 marker = topAt(top, 11, 5);
    for (std::size_t c = 0; c < 3; ++c) {
        const double want = background[c] + (0.9 * (cursor[c] - background[c]));
        CHECK(std::abs(marker[c] - want) <= 1.0);
    }
    CHECK(marker[3] == 255);
}
