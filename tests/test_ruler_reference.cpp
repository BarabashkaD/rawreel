// The ruler pass's push constants, their reflection and its CPU reference.

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <string>

#include "composite_reference.h"
#include "core/overlay.h"
#include "dvv/base/json.h"
#include "dvv/gpu/glyph_strip.h"
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
using rawreel::labelCoverage;
using rawreel::labelStrip;
using rawreel::RULER_CURSOR;
using rawreel::RULER_IMAGE;
using rawreel::RULER_LIT;
using rawreel::RULER_NUMBERS;
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
    return rulerPixel(band, nullptr, band.pictureStart + static_cast<std::uint32_t>(along),
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
    CHECK(offsetof(RulerConstants, labelSpacing) == 124);
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
        {"litDepth", 120},    {"labelSpacing", 124}};
    CHECK(offsets == expected);
}

TEST_CASE("rulers shader: binds the output and the glyph strip") {
    const auto reflection = dvv::json::parse(rawreel_shaders::rulers().reflectionJson);
    REQUIRE(reflection.has_value());
    std::map<std::string, std::uint32_t> bindings;
    for (const auto& parameter : reflection->at("parameters")) {
        const auto& binding = parameter.at("binding");
        if (dvv::json::get<std::string>(binding, "kind") == "descriptorTableSlot") {
            bindings[dvv::json::get<std::string>(parameter, "name").value_or("?")] =
                dvv::json::get<std::uint32_t>(binding, "index").value_or(~0U);
        }
    }
    const std::map<std::string, std::uint32_t> expected{{"output", 0}, {"glyphs", 1}};
    CHECK(bindings == expected);
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

TEST_CASE("ruler constants: the label flags and spacing") {
    rawreel::OverlayParams overlay = retina();
    const ViewTransform view = ViewTransform::fit({181, 67}, {37, 23});
    const PixelSize extent{211, 97};
    overlay.labelSpacing = {.numberGap = 4, .tagPad = 6, .tagInset = 2, .tagMargin = 255};
    constexpr std::uint32_t LABEL_FLAGS =
        rawreel::RULER_NUMBERS | rawreel::RULER_EDGE | rawreel::RULER_TAG_TEXT_BLACK;
    for (const RulerConstants& band : bandsOf(overlay, view, extent, std::nullopt, false)) {
        CHECK(band.labelSpacing == (4U | 6U << 8U | 2U << 16U | 255U << 24U));
        CHECK((band.flags & LABEL_FLAGS) == 0);
    }
    // A field above 255 is capped: it does not spill into the next byte.
    overlay.labelSpacing = {.numberGap = 4, .tagPad = 300, .tagInset = 2, .tagMargin = 1000};
    for (const RulerConstants& band : bandsOf(overlay, view, extent, std::nullopt, false)) {
        CHECK(band.labelSpacing == (4U | 255U << 8U | 2U << 16U | 255U << 24U));
    }
    // Numbers on but no strip, or a strip but numbers off: no numbers.
    overlay.numbers = true;
    for (const RulerConstants& band : bandsOf(overlay, view, extent, std::nullopt, false)) {
        CHECK((band.flags & rawreel::RULER_NUMBERS) == 0);
    }
    overlay.glyphs = std::make_shared<const dvv::gpu::GlyphStrip>();
    overlay.numbers = false;
    for (const RulerConstants& band : bandsOf(overlay, view, extent, std::nullopt, false)) {
        CHECK((band.flags & rawreel::RULER_NUMBERS) == 0);
    }
    overlay.numbers = true;
    overlay.lastValue = rawreel::LastValue::Edge;
    overlay.cursorTagTextBlack = true;
    for (const RulerConstants& band : bandsOf(overlay, view, extent, std::nullopt, false)) {
        CHECK((band.flags & LABEL_FLAGS) == LABEL_FLAGS);
    }
    // Without an image the rulers show only their background: no numbers.
    for (const RulerConstants& band :
         bandsOf(overlay, ViewTransform{}, extent, std::nullopt, false)) {
        CHECK((band.flags & rawreel::RULER_NUMBERS) == 0);
        CHECK((band.flags & rawreel::RULER_EDGE) != 0);
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
    CHECK(rulerPixel(top, nullptr, 0, 0) == hex(rawreel::RULER_CORNER_LIGHT));
    CHECK(rulerPixel(top, nullptr, 29, 29) == hex(rawreel::RULER_CORNER_LIGHT));
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
        ticks += rulerPixel(far[0], nullptr, x, 10) == background ? 0 : 1;
    }
    CHECK(ticks == 0);
    const dvv::media::PixelSpan marker = dvv::media::cursorSpan(dvv::media::axisX(quarter), 44);
    REQUIRE_FALSE(marker.empty());
    CHECK(topAt(far[0], marker.begin, 0) != hex(rawreel::RULER_SEPARATOR_LIGHT));
    CHECK(topAt(far[0], marker.begin, 3) == Rgba8{251, 75, 65, 255});
}

TEST_CASE("ruler reference: the numbers never turn a ruler off") {
    // A 4K image at 80 % on a 2x display: four-digit numbers wider than two major spacings
    // (32 x 0.8 = 25.6 px) still leave both rulers their ticks.
    rawreel::OverlayParams overlay = retina();
    const ViewTransform view =
        resolveView({ViewMode::Zoom, 0.8, {1920, 240}, {3840, 480}}, {800, 400}, {3840, 480});
    REQUIRE(static_cast<float>(view.zoom()) == 0.8F);
    for (const bool numbers : {true, false}) {
        CAPTURE(numbers);
        overlay.numbers = numbers;
        const auto bands = bandsOf(overlay, view, {830, 430}, ImagePixel{1900, 100}, false);
        for (const RulerConstants& band : bands) {
            CHECK((band.flags & RULER_TICKS) != 0);
            CHECK((band.flags & RULER_CURSOR) != 0);
            const dvv::media::AxisView axis =
                band.axis == 0 ? dvv::media::axisX(view) : dvv::media::axisY(view);
            const dvv::media::PixelSpan span = dvv::media::cursorSpan(axis, band.cursor);
            REQUIRE_FALSE(span.empty());
            const std::uint32_t along = band.pictureStart + static_cast<std::uint32_t>(span.begin);
            const std::uint32_t across = band.rulerEdge - 2;  // depth 1: over the background
            CHECK(rulerPixel(band, nullptr, band.axis == 0 ? along : across,
                             band.axis == 0 ? across : along) == Rgba8{251, 75, 65, 255});
        }
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
                wrong += rulerPixel(band, nullptr, x, y) == expected ? 0 : 1;
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
    CHECK(rulerPixel(bands[0], nullptr, 103, 25) == hex(rawreel::RULER_BACKGROUND_DARK));
    CHECK(rulerPixel(bands[0], nullptr, 103, 29) == hex(rawreel::RULER_SEPARATOR_DARK));
    CHECK(rulerPixel(bands[0], nullptr, 103, 19) == background);  // above the ruler
    CHECK(rulerPixel(bands[0], nullptr, 100, 5) == background);   // the reserved strip
    CHECK(rulerPixel(bands[0], nullptr, 165, 25) == background);  // past the ruler's end
    CHECK(rulerPixel(bands[0], nullptr, 45, 25) == hex(rawreel::RULER_CORNER_DARK));
    CHECK(rulerPixel(bands[0], nullptr, 39, 25) == background);  // left of the corner
    CHECK(rulerPixel(bands[1], nullptr, 50, 43) == hex(rawreel::RULER_BACKGROUND_DARK));
    CHECK(rulerPixel(bands[1], nullptr, 59, 43) == hex(rawreel::RULER_SEPARATOR_DARK));
    CHECK(rulerPixel(bands[1], nullptr, 10, 40) == background);
    CHECK(rulerPixel(bands[1], nullptr, 50, 85) == background);  // below the ruler
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
                const Rgba8 colour = rulerPixel(bands[b], nullptr, x, y);
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

namespace {

// `text` blended over `under` (both 0xRRGGBB) by `coverage` / 255 in encoded space, one float
// operation per statement as the reference rounds.
Rgba8 blended(std::uint32_t under, std::uint32_t text, std::uint32_t coverage) {
    Rgba8 out{0, 0, 0, 255};
    const float alpha = static_cast<float>(coverage) / 255.0F;
    for (std::size_t c = 0; c < 3; ++c) {
        const std::uint32_t shift = 16 - (8 * static_cast<std::uint32_t>(c));
        const float u = static_cast<float>((under >> shift) & 0xFFU) / 255.0F;
        const float t = static_cast<float>((text >> shift) & 0xFFU) / 255.0F;
        const float difference = t - u;
        const float step = alpha * difference;
        const float mixed = u + step;
        const float scaled = 255.0F * std::clamp(mixed, 0.0F, 1.0F);
        out[c] = static_cast<std::uint8_t>(std::lround(scaled));
    }
    return out;
}

// The 256 x 128 image at zoom 1 in P = {30, 30, 256, 128} with the retina rulers: a label band of
// 10 rows above (or columns left of) the tick band, numbers every 32 px, spacing {4, 6, 2, 4}.
// The text rows are 1 to 7 of the band (the 7-row strip centred in 10), the tag fill rows 2 to 7.
struct LabelScene {
    rawreel::OverlayParams overlay = retina();
    ViewTransform view =
        resolveView({ViewMode::Zoom, 1, {128, 64}, {256, 128}}, {256, 128}, {256, 128});
    PixelSize extent{286, 158};

    explicit LabelScene(std::uint32_t strip_height = 7) {
        overlay.numbers = true;
        overlay.glyphs = labelStrip(5, strip_height);
        overlay.labelSpacing = {.numberGap = 4, .tagPad = 6, .tagInset = 2, .tagMargin = 4};
    }
    [[nodiscard]] std::array<RulerConstants, 2> bands(std::optional<ImagePixel> cursor) const {
        return bandsOf(overlay, view, extent, cursor, false);
    }
};

}  // namespace

TEST_CASE("ruler reference: a number's pixels sample its digits' cells on both rulers") {
    const LabelScene scene;
    const auto bands = scene.bands(std::nullopt);
    const RulerConstants& top = bands[0];
    const RulerConstants& left = bands[1];
    REQUIRE((top.flags & RULER_NUMBERS) != 0);
    REQUIRE(top.pictureStart == 30);
    REQUIRE(top.rulerEdge == 30);
    REQUIRE(left.rulerEdge == 30);
    const dvv::gpu::GlyphStrip* strip = scene.overlay.glyphs.get();
    // Top: 64's text begins at 30 + 64 + 4 = 98, "6" in [98, 103), "4" in [103, 108).
    for (std::uint32_t x = 96; x < 110; ++x) {
        for (std::uint32_t y = 0; y < 10; ++y) {
            CAPTURE(x);
            CAPTURE(y);
            Rgba8 expected = hex(rawreel::RULER_BACKGROUND_LIGHT);
            if (x >= 98 && x < 108 && y >= 1 && y < 8) {
                expected = blended(rawreel::RULER_BACKGROUND_LIGHT, rawreel::RULER_TEXT_LIGHT,
                                   labelCoverage(x < 103 ? 6 : 4, (x - 98) % 5, y - 1));
            }
            CHECK(rulerPixel(top, strip, x, y) == expected);
        }
    }
    // Left, read bottom to top: 32's text is [66, 76) along y, its first column at y = 75; "3"
    // in [71, 76), "2" in [66, 71); the glyph rows run along x from the band's outer edge.
    for (std::uint32_t y = 64; y < 78; ++y) {
        for (std::uint32_t x = 0; x < 10; ++x) {
            CAPTURE(x);
            CAPTURE(y);
            Rgba8 expected = hex(rawreel::RULER_BACKGROUND_LIGHT);
            if (y >= 66 && y < 76 && x >= 1 && x < 8) {
                const std::uint32_t offset = 75 - y;
                expected = blended(rawreel::RULER_BACKGROUND_LIGHT, rawreel::RULER_TEXT_LIGHT,
                                   labelCoverage(offset < 5 ? 3 : 2, offset % 5, x - 1));
            }
            CHECK(rulerPixel(left, strip, x, y) == expected);
        }
    }
}

TEST_CASE("ruler reference: the end tags, Last pixel and Edge") {
    LabelScene scene;
    scene.overlay.dark = true;
    for (const bool edge : {false, true}) {
        CAPTURE(edge);
        scene.overlay.lastValue = edge ? rawreel::LastValue::Edge : rawreel::LastValue::Pixel;
        const RulerConstants top = scene.bands(std::nullopt)[0];
        const dvv::gpu::GlyphStrip* strip = scene.overlay.glyphs.get();
        const Rgba8 fill = hex(rawreel::RULER_TAG_FILL_DARK);
        const Rgba8 background = hex(rawreel::RULER_BACKGROUND_DARK);
        // The first tag "0": [34, 51), text from 40; 0's number is dropped under it.
        CHECK(rulerPixel(top, strip, 33, 2) == background);
        CHECK(rulerPixel(top, strip, 34, 2) == fill);
        CHECK(rulerPixel(top, strip, 50, 7) == fill);
        CHECK(rulerPixel(top, strip, 34, 1) == background);  // the inset row
        CHECK(rulerPixel(top, strip, 34, 8) == background);
        CHECK(rulerPixel(top, strip, 42, 4) == blended(rawreel::RULER_TAG_FILL_DARK,
                                                       rawreel::RULER_TAG_TEXT_DARK,
                                                       labelCoverage(0, 2, 3)));
        // The last tag "255" or "256": [255, 282), text from 261, its last digit from 271.
        CHECK(rulerPixel(top, strip, 254, 2) == background);
        CHECK(rulerPixel(top, strip, 255, 2) == fill);
        CHECK(rulerPixel(top, strip, 281, 7) == fill);
        CHECK(rulerPixel(top, strip, 282, 2) == background);
        CHECK(rulerPixel(top, strip, 273, 4) == blended(rawreel::RULER_TAG_FILL_DARK,
                                                        rawreel::RULER_TAG_TEXT_DARK,
                                                        labelCoverage(edge ? 6 : 5, 2, 3)));
        // Glyph rows outside the fill draw over the ruler: row 1 is the strip's row 0.
        CHECK(rulerPixel(top, strip, 273, 1) == blended(rawreel::RULER_BACKGROUND_DARK,
                                                        rawreel::RULER_TAG_TEXT_DARK,
                                                        labelCoverage(edge ? 6 : 5, 2, 0)));
    }
}

TEST_CASE("ruler reference: the cursor tag at the start and the end of the ruler") {
    const LabelScene scene;
    const dvv::gpu::GlyphStrip* strip = scene.overlay.glyphs.get();
    const Rgba8 cursor = hex(scene.overlay.cursorColour);
    const Rgba8 background = hex(rawreel::RULER_BACKGROUND_LIGHT);
    // Cursor 0: centred on 30, clamped to the margin, [34, 51); the first tag is dropped.
    const RulerConstants start = scene.bands(ImagePixel{0, 3})[0];
    CHECK(rulerPixel(start, strip, 33, 1) == background);  // above the lit tick
    CHECK(rulerPixel(start, strip, 34, 2) == cursor);
    CHECK(rulerPixel(start, strip, 50, 7) == cursor);
    CHECK(rulerPixel(start, strip, 51, 7) == background);
    CHECK(rulerPixel(start, strip, 42, 4) == blended(scene.overlay.cursorColour,
                                                     rawreel::RULER_CURSOR_TAG_WHITE,
                                                     labelCoverage(0, 2, 3)));
    // Cursor 255: centred on 285, clamped to [255, 282), black text; the last tag is dropped.
    RulerConstants end = scene.bands(ImagePixel{255, 3})[0];
    end.flags |= rawreel::RULER_TAG_TEXT_BLACK;
    CHECK(rulerPixel(end, strip, 254, 2) == background);
    CHECK(rulerPixel(end, strip, 255, 2) == cursor);
    CHECK(rulerPixel(end, strip, 281, 7) == cursor);
    CHECK(rulerPixel(end, strip, 282, 1) == background);
    CHECK(rulerPixel(end, strip, 273, 4) == blended(scene.overlay.cursorColour,
                                                    rawreel::RULER_CURSOR_TAG_BLACK,
                                                    labelCoverage(5, 2, 3)));
}

TEST_CASE("ruler reference: a strip taller than the label band stays in the band") {
    const LabelScene scene(40);
    for (const RulerConstants& band : scene.bands(ImagePixel{100, 50})) {
        const bool top = band.axis == 0;
        std::size_t changed = 0;
        for (std::uint32_t y = band.regionY; y < band.regionY + band.regionH; ++y) {
            for (std::uint32_t x = band.regionX; x < band.regionX + band.regionW; ++x) {
                const std::uint32_t across = top ? y : x;
                const std::uint32_t along = top ? x : y;
                const bool label_band = across >= band.rulerEdge - band.strip &&
                                        across < band.rulerEdge - band.tickBand &&
                                        along >= band.rulerBegin && along < band.rulerEnd;
                const Rgba8 drawn = rulerPixel(band, scene.overlay.glyphs.get(), x, y);
                const Rgba8 plain = rulerPixel(band, nullptr, x, y);
                if (label_band) {
                    changed += drawn == plain ? 0 : 1;
                } else {
                    CAPTURE(x);
                    CAPTURE(y);
                    CHECK(drawn == plain);
                }
            }
        }
        CHECK(changed > 0);
    }
}

TEST_CASE("ruler reference: numbers off leave the label band as without a strip") {
    LabelScene scene;
    scene.overlay.numbers = false;
    for (const RulerConstants& band : scene.bands(ImagePixel{100, 50})) {
        REQUIRE((band.flags & RULER_NUMBERS) == 0);
        std::size_t wrong = 0;
        for (std::uint32_t y = band.regionY; y < band.regionY + band.regionH; ++y) {
            for (std::uint32_t x = band.regionX; x < band.regionX + band.regionW; ++x) {
                wrong += rulerPixel(band, scene.overlay.glyphs.get(), x, y) ==
                                 rulerPixel(band, nullptr, x, y)
                             ? 0
                             : 1;
            }
        }
        CHECK(wrong == 0);
    }
}
