// The compositor's push constants, their reflection and its CPU reference (design M3 v2 §4.4,
// §8.1): fit placement, nearest enlargement, the multi-tap downscale and the background.
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "composite_reference.h"
#include "dvv/base/json.h"
#include "dvv/media/display.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "rawreel_shaders/composite.h"
#include "rawreel_shaders/grid_lines.h"
#include "render/composite_constants.h"
#include "render/display_state.h"
#include "render/pass_regions.h"

using dvv::media::encodeSrgb8;
using dvv::media::floatToHalf;
using dvv::media::halfToFloat;
using dvv::media::resolveView;
using dvv::media::srgbDecode;
using dvv::media::ViewMode;
using dvv::media::ViewTransform;
using dvv::platform::PixelSize;
using rawreel::COMPOSITE_DARK;
using rawreel::COMPOSITE_SKIP_ENCODE;
using rawreel::CompositeConstants;
using rawreel::compositeConstants;
using rawreel::compositePixel;
using rawreel::compositeReference;
using rawreel::HostDisplayImage;

namespace {

using Rgba8 = std::array<std::uint8_t, 4>;

std::vector<std::uint16_t> filled(std::uint32_t w, std::uint32_t h, auto value) {
    std::vector<std::uint16_t> texels(std::size_t{w} * h * 4);
    for (std::uint32_t y = 0; y < h; ++y) {
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::size_t base = ((std::size_t{y} * w) + x) * 4;
            for (std::uint32_t c = 0; c < 3; ++c) {
                texels[base + c] = floatToHalf(value(x, y, c));
            }
            texels[base + 3] = floatToHalf(1.0F);
        }
    }
    return texels;
}

std::vector<std::uint16_t> white(std::uint32_t w, std::uint32_t h) {
    return filled(w, h, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 1.0F; });
}

std::vector<std::uint16_t> columnImage(std::uint32_t w, std::uint32_t h, std::uint32_t column) {
    return filled(w, h, [column](std::uint32_t x, std::uint32_t, std::uint32_t) {
        return x == column ? 1.0F : 0.0F;
    });
}

std::vector<std::uint16_t> pattern37x23() {
    return filled(37, 23, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
        return static_cast<float>(((x * 7) + (y * 13) + (c * 5)) % 17) / 16.0F;
    });
}

Rgba8 bg(bool dark) {
    const std::uint8_t grey = encodeSrgb8(srgbDecode(dark ? 0.12F : 0.85F));
    return {grey, grey, grey, 255};
}

Rgba8 grey8(std::uint8_t value) {
    return {value, value, value, 255};
}

HostDisplayImage hostImage(std::uint32_t w, std::uint32_t h,
                           const std::vector<std::uint16_t>& texels) {
    return HostDisplayImage{.width = w, .height = h, .rgba16f = texels};
}

const rawreel::DisplayParams DARK{.overlay = {.dark = true}};
const rawreel::DisplayParams LIGHT{.overlay = {.dark = false}};

}  // namespace

TEST_CASE("composite constants: layout") {
    CHECK(offsetof(CompositeConstants, viewportW) == 0);
    CHECK(offsetof(CompositeConstants, viewportH) == 4);
    CHECK(offsetof(CompositeConstants, imageW) == 8);
    CHECK(offsetof(CompositeConstants, imageH) == 12);
    CHECK(offsetof(CompositeConstants, zoom) == 16);
    CHECK(offsetof(CompositeConstants, flags) == 20);
    CHECK(offsetof(CompositeConstants, regionX) == 24);
    CHECK(offsetof(CompositeConstants, regionY) == 28);
    CHECK(offsetof(CompositeConstants, offsetX) == 32);
    CHECK(offsetof(CompositeConstants, offsetY) == 36);
    CHECK(offsetof(CompositeConstants, drawX) == 40);
    CHECK(offsetof(CompositeConstants, drawY) == 44);
    CHECK(offsetof(CompositeConstants, levelColour) == 48);
    CHECK(offsetof(CompositeConstants, levelShape) == 64);
    CHECK(sizeof(CompositeConstants) == 80);
}

TEST_CASE("composite shaders: the push block matches CompositeConstants") {
    for (const auto* shader : {&rawreel_shaders::composite(), &rawreel_shaders::gridLines()}) {
        CAPTURE(shader == &rawreel_shaders::gridLines());
        const auto reflection = dvv::json::parse(shader->reflectionJson);
        REQUIRE(reflection.has_value());
        const dvv::json::Value* block = nullptr;
        for (const auto& parameter : reflection->at("parameters")) {
            if (dvv::json::get<std::string>(parameter.at("binding"), "kind") ==
                "pushConstantBuffer") {
                block = &parameter;
            }
        }
        REQUIRE(block != nullptr);
        const auto& type = block->at("type");
        CHECK(dvv::json::get<std::uint32_t>(type.at("elementVarLayout").at("binding"), "size") ==
              80U);
        std::map<std::string, std::uint32_t> offsets;
        for (const auto& field : type.at("elementType").at("fields")) {
            offsets[dvv::json::get<std::string>(field, "name").value_or("?")] =
                dvv::json::get<std::uint32_t>(field.at("binding"), "offset").value_or(~0U);
        }
        const std::map<std::string, std::uint32_t> expected{
            {"viewportW", 0},    {"viewportH", 4},  {"imageW", 8},   {"imageH", 12},
            {"zoom", 16},        {"flags", 20},     {"regionX", 24}, {"regionY", 28},
            {"offsetX", 32},     {"offsetY", 36},   {"drawX", 40},   {"drawY", 44},
            {"levelColour", 48}, {"levelShape", 64}};
        CHECK(offsets == expected);
    }
}

TEST_CASE("composite constants: mapping") {
    const CompositeConstants c =
        compositeConstants(DARK.overlay, ViewTransform::fit({211, 97}, {37, 23}), {}, false);
    CHECK(c.viewportW == 211);
    CHECK(c.viewportH == 97);
    CHECK(c.imageW == 37);
    CHECK(c.imageH == 23);
    CHECK(c.zoom == static_cast<float>(97.0 / 23.0));
    CHECK(c.offsetX == 27);
    CHECK(c.offsetY == 0);
    CHECK(c.regionX == 0);
    CHECK(c.regionY == 0);
    const CompositeConstants placed = compositeConstants(
        DARK.overlay, ViewTransform::fit({211, 97}, {37, 23}),
        rawreel::PassRegions{.picture = {5, 7, 211, 97}, .pictureDrawn = {5, 7, 211, 97}}, false);
    CHECK(placed.regionX == 5);
    CHECK(placed.regionY == 7);
    CHECK(placed.offsetX == 27);
    for (const std::uint32_t shape : placed.levelShape) {
        CHECK((shape & rawreel::LEVEL_DRAWN) == 0);  // OverlayParams{}: no grid
    }
    CHECK(c.flags == COMPOSITE_DARK);
    CHECK(
        compositeConstants(DARK.overlay, ViewTransform::fit({211, 97}, {37, 23}), {}, true).flags ==
        (COMPOSITE_DARK | COMPOSITE_SKIP_ENCODE));
    CHECK(compositeConstants(LIGHT.overlay, ViewTransform::fit({211, 97}, {37, 23}), {}, false)
              .flags == 0);
    const CompositeConstants empty =
        compositeConstants(DARK.overlay, ViewTransform::fit({211, 97}, {0, 0}), {}, false);
    CHECK(empty.imageW == 0);
    CHECK(empty.imageH == 0);
    CHECK(empty.zoom == 0.0F);
    CHECK(empty.viewportW == 211);
    CHECK(empty.viewportH == 97);
}

TEST_CASE("composite constants: from a zoomed view") {
    const auto view = resolveView({ViewMode::Zoom, 2, {10, 8}, {37, 23}}, {30, 19}, {37, 23});
    const CompositeConstants c = compositeConstants({.dark = true}, view, {}, false);
    CHECK(c.zoom == 2.0F);
    CHECK(c.offsetX == view.offset().x);
    CHECK(c.offsetY == view.offset().y);
    CHECK(c.viewportW == 30);
    CHECK(c.viewportH == 19);
    CHECK(c.imageW == 37);
    CHECK(c.imageH == 23);
    CHECK(c.flags == COMPOSITE_DARK);
    CHECK(compositeConstants({.dark = true}, view, {}, true).flags ==
          (COMPOSITE_DARK | COMPOSITE_SKIP_ENCODE));
    CHECK(compositeConstants({.dark = false}, view, {}, false).flags == 0);
}

TEST_CASE("composite reference: a request for another frame size draws the fit") {
    const std::vector<std::uint16_t> texels = white(37, 23);
    const HostDisplayImage image = hostImage(37, 23, texels);
    const rawreel::DisplayParams other{.view = {ViewMode::Zoom, 2, {10, 8}, {36, 23}},
                                       .overlay = {.dark = true}};
    for (std::uint32_t y = 0; y < 19; ++y) {
        for (std::uint32_t x = 0; x < 30; ++x) {
            CAPTURE(x);
            CAPTURE(y);
            CHECK(compositeReference(x, y, {30, 19}, other, image) ==
                  compositeReference(x, y, {30, 19}, DARK, image));
        }
    }
}

TEST_CASE("composite reference: background") {
    CHECK(bg(true) == Rgba8{31, 31, 31, 255});
    CHECK(bg(false) == Rgba8{217, 217, 217, 255});
    for (const bool dark : {true, false}) {
        CAPTURE(dark);
        const rawreel::DisplayParams params{.overlay = {.dark = dark}};
        CHECK(compositeReference(0, 0, {211, 97}, params, std::nullopt) == bg(dark));
        CHECK(compositeReference(210, 96, {211, 97}, params, std::nullopt) == bg(dark));
    }
    const auto linear = static_cast<std::uint8_t>(std::lround(255.0F * srgbDecode(0.12F)));
    CHECK(compositePixel(
              compositeConstants(DARK.overlay, ViewTransform::fit({4, 4}, {0, 0}), {}, true), 0, 0,
              std::nullopt) == grey8(linear));
}

TEST_CASE("composite reference: fit placement") {
    const std::vector<std::uint16_t> texels = white(37, 23);
    const HostDisplayImage image = hostImage(37, 23, texels);
    const Rgba8 full = grey8(255);
    // Height-limited: x offset 27, the image covers columns 27..182.
    CHECK(compositeReference(26, 48, {211, 97}, DARK, image) == bg(true));
    CHECK(compositeReference(27, 48, {211, 97}, DARK, image) == full);
    CHECK(compositeReference(182, 48, {211, 97}, DARK, image) == full);
    CHECK(compositeReference(183, 48, {211, 97}, DARK, image) == bg(true));
    // Width-limited at zoom 2: y offset 17, the image covers rows 17..62.
    CHECK(compositeReference(37, 16, {74, 80}, DARK, image) == bg(true));
    CHECK(compositeReference(37, 17, {74, 80}, DARK, image) == full);
    CHECK(compositeReference(37, 62, {74, 80}, DARK, image) == full);
    CHECK(compositeReference(37, 63, {74, 80}, DARK, image) == bg(true));
}

TEST_CASE("composite reference: 1:1 and integer zoom") {
    const std::vector<std::uint16_t> texels = pattern37x23();
    const HostDisplayImage image = hostImage(37, 23, texels);
    std::size_t wrong_1x1 = 0;
    std::size_t wrong_2x2 = 0;
    for (std::uint32_t y = 0; y < 23; ++y) {
        for (std::uint32_t x = 0; x < 37; ++x) {
            const std::size_t base = ((std::size_t{y} * 37) + x) * 4;
            const Rgba8 wanted{encodeSrgb8(halfToFloat(texels[base])),
                               encodeSrgb8(halfToFloat(texels[base + 1])),
                               encodeSrgb8(halfToFloat(texels[base + 2])), 255};
            const Rgba8 one = compositeReference(x, y, {37, 23}, DARK, image);
            wrong_1x1 += one == wanted ? 0 : 1;
            for (std::uint32_t l = 0; l < 2; ++l) {
                for (std::uint32_t k = 0; k < 2; ++k) {
                    const Rgba8 two =
                        compositeReference((2 * x) + k, 17 + (2 * y) + l, {74, 80}, DARK, image);
                    wrong_2x2 += two == one ? 0 : 1;
                }
            }
        }
    }
    CHECK(wrong_1x1 == 0);
    CHECK(wrong_2x2 == 0);
}

TEST_CASE("composite reference: multi-tap downscale") {
    const std::vector<std::uint16_t> column8 = columnImage(8, 8, 0);
    const HostDisplayImage image8 = hostImage(8, 8, column8);
    // Zoom 0.25, n = 2 (a plain bilinear would give 0.5 at pixel 0).
    CHECK(compositeReference(0, 0, {2, 2}, DARK, image8) == grey8(encodeSrgb8(0.25F)));
    CHECK(compositeReference(1, 0, {2, 2}, DARK, image8) == grey8(encodeSrgb8(0.0F)));
    // Zoom 0.125, n = 4.
    CHECK(compositeReference(0, 0, {1, 1}, DARK, image8) == grey8(encodeSrgb8(0.125F)));
    // Zoom 0.75, n = 1: the plain bilinear.
    const Rgba8 bilinear = compositeReference(0, 0, {6, 6}, DARK, image8);
    const int wanted = encodeSrgb8(5.0F / 6.0F);
    for (std::size_t c = 0; c < 3; ++c) {
        CAPTURE(c);
        CHECK(std::abs(static_cast<int>(bilinear[c]) - wanted) <= 1);
    }
    CHECK(bilinear[3] == 255);
    // Zoom 1/16: n is capped at 4, so the view undersamples below 1:8 (the documented limit).
    const std::vector<std::uint16_t> column16a = columnImage(16, 16, 0);
    CHECK(compositeReference(0, 0, {1, 1}, DARK, hostImage(16, 16, column16a)) ==
          grey8(encodeSrgb8(0.0F)));
    const std::vector<std::uint16_t> column16b = columnImage(16, 16, 1);
    CHECK(compositeReference(0, 0, {1, 1}, DARK, hostImage(16, 16, column16b)) ==
          grey8(encodeSrgb8(0.125F)));
}

TEST_CASE("composite reference: skip encode") {
    const std::vector<std::uint16_t> texels =
        filled(1, 1, [](std::uint32_t, std::uint32_t, std::uint32_t) { return 0.25F; });
    const std::optional<HostDisplayImage> image = hostImage(1, 1, texels);
    CHECK(compositePixel(
              compositeConstants(LIGHT.overlay, ViewTransform::fit({1, 1}, {1, 1}), {}, true), 0, 0,
              image) == Rgba8{64, 64, 64, 255});
}

namespace {

// White at the given opacity in %, packed as GridLevelParams::colour.
constexpr std::uint32_t whiteAt(std::uint32_t percent) {
    return 0xFFFFFFU | (((percent * 255) + 50) / 100) << 24U;
}

// A Plain grid at backing scale 2: major 32 (4 px, at least 24 px apart) and minor 8 (2 px,
// 8 px), the second minor off.
rawreel::OverlayParams plainGrid() {
    rawreel::OverlayParams overlay{.dark = true};
    overlay.levels[0] = {
        .stepLog2 = 5, .minimumSpacing = 24, .thickness = 4, .colour = whiteAt(85), .shown = true};
    overlay.levels[1] = {
        .stepLog2 = 3, .minimumSpacing = 8, .thickness = 2, .colour = whiteAt(50), .shown = true};
    overlay.levels[2] = {
        .stepLog2 = 1, .minimumSpacing = 6, .thickness = 2, .colour = whiteAt(25), .shown = false};
    return overlay;
}

std::vector<std::uint16_t> gradient64x48() {
    return filled(64, 48, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
        return static_cast<float>(((x * 3) + (y * 5) + (c * 7)) % 64) / 63.0F;
    });
}

// The encoded texel (x, y) of `texels` (64 wide), per channel.
std::array<float, 3> encodedTexel(const std::vector<std::uint16_t>& texels, std::uint32_t x,
                                  std::uint32_t y) {
    const std::size_t base = ((std::size_t{y} * 64) + x) * 4;
    return {dvv::media::srgbEncode(halfToFloat(texels[base])),
            dvv::media::srgbEncode(halfToFloat(texels[base + 1])),
            dvv::media::srgbEncode(halfToFloat(texels[base + 2]))};
}

std::uint8_t unorm(float x) {
    return static_cast<std::uint8_t>(std::lround(255.0F * std::clamp(x, 0.0F, 1.0F)));
}

// mix(encoded, colour, a) per channel, as unorm8; `decode`: srgbDecode before the unorm.
Rgba8 blended(const std::array<float, 3>& encoded, const std::array<float, 3>& colour, float a,
              bool decode = false) {
    Rgba8 out{0, 0, 0, 255};
    for (std::size_t c = 0; c < 3; ++c) {
        const float mixed = encoded[c] + (a * (colour[c] - encoded[c]));
        out[c] = unorm(decode ? srgbDecode(mixed) : mixed);
    }
    return out;
}

// 400 % with offset 0: P 256 x 192 shows the 64 x 48 image whole.
constexpr dvv::media::ViewRequest AT_400{ViewMode::Zoom, 4, {32, 24}, {64, 48}};
constexpr PixelSize P_400{256, 192};

}  // namespace

TEST_CASE("composite reference: the grid's Plain lines at 400 %") {
    const std::vector<std::uint16_t> texels = gradient64x48();
    const HostDisplayImage image = hostImage(64, 48, texels);
    REQUIRE(resolveView(AT_400, P_400, {64, 48}).offset() == dvv::media::PixelOffset{0, 0});
    const rawreel::DisplayParams params{.view = AT_400, .overlay = plainGrid()};
    // (128, 50): on the major line k = 32 ([126, 130)), image pixel (32, 12).
    CHECK(compositeReference(128, 50, P_400, params, image) ==
          blended(encodedTexel(texels, 32, 12), {1.0F, 1.0F, 1.0F}, 217.0F / 255.0F));
    // (130, 50): off every line, the plain texel.
    const std::array<float, 3> plain = encodedTexel(texels, 32, 12);
    CHECK(compositeReference(130, 50, P_400, params, image) ==
          Rgba8{unorm(plain[0]), unorm(plain[1]), unorm(plain[2]), 255});
    // (96, 50): on the minor line k = 24 ([95, 97)) at 50 %, image pixel (24, 12).
    CHECK(compositeReference(96, 50, P_400, params, image) ==
          blended(encodedTexel(texels, 24, 12), {1.0F, 1.0F, 1.0F}, 128.0F / 255.0F));
}

TEST_CASE("composite reference: adaptive, halo and skip encode") {
    const std::vector<std::uint16_t> texels = gradient64x48();
    const HostDisplayImage image = hostImage(64, 48, texels);
    const std::array<float, 3> on_line = encodedTexel(texels, 32, 12);
    const float a = 217.0F / 255.0F;
    SUBCASE("adaptive") {
        rawreel::DisplayParams params{.view = AT_400, .overlay = plainGrid()};
        params.overlay.levels[0].adaptive = true;
        const float luma = (0.2126F * on_line[0]) + (0.7152F * on_line[1]) + (0.0722F * on_line[2]);
        const float shade = luma > 0.5F ? 0.0F : 1.0F;
        CHECK(compositeReference(128, 50, P_400, params, image) ==
              blended(on_line, {shade, shade, shade}, a));
    }
    SUBCASE("halo") {
        rawreel::DisplayParams params{.view = AT_400, .overlay = plainGrid()};
        params.overlay.levels[0].halo = true;
        // (130, 50): the halo pixel right of the major line [126, 130).
        CHECK(compositeReference(130, 50, P_400, params, image) ==
              blended(on_line, {0.0F, 0.0F, 0.0F}, rawreel::GRID_HALO_STRENGTH * a));
    }
    SUBCASE("skip encode") {
        const rawreel::OverlayParams overlay = plainGrid();
        const CompositeConstants constants =
            compositeConstants(overlay, resolveView(AT_400, P_400, {64, 48}), {}, true);
        CHECK(compositePixel(constants, 128, 50, image) ==
              blended(on_line, {1.0F, 1.0F, 1.0F}, a, true));
    }
}

TEST_CASE("composite reference: no line outside the image span") {
    const std::vector<std::uint16_t> texels = gradient64x48();
    const HostDisplayImage image = hostImage(64, 48, texels);
    // Fit 64 x 48 into 256 x 256: zoom 4, the image covers rows [32, 224).
    const rawreel::DisplayParams params{.overlay = plainGrid()};
    REQUIRE(resolveView(params.view, {256, 256}, {64, 48}).offset() ==
            dvv::media::PixelOffset{0, 32});
    for (const std::uint32_t y : {0U, 10U, 31U, 224U, 255U}) {
        CAPTURE(y);
        CHECK(compositeReference(128, y, {256, 256}, params, image) == bg(true));
        CHECK(compositeReference(0, y, {256, 256}, params, image) == bg(true));
    }
    CHECK(compositeReference(128, 32, {256, 256}, params, image) != bg(true));
}

TEST_CASE("composite reference: a line beats a halo of the same level") {
    const std::vector<std::uint16_t> texels = gradient64x48();
    const HostDisplayImage image = hostImage(64, 48, texels);
    rawreel::DisplayParams params{.view = AT_400, .overlay = plainGrid()};
    params.overlay.levels[0].thickness = 2;
    params.overlay.levels[0].halo = true;
    params.overlay.levels[1].shown = false;
    const float a = 217.0F / 255.0F;
    // k = 32 on x: line [127, 129), halo 126 and 129; on y: line [127, 129).
    const std::array<float, 3> texel = encodedTexel(texels, 32, 32);
    CHECK(compositeReference(129, 128, P_400, params, image) ==
          blended(texel, {1.0F, 1.0F, 1.0F}, a));
    // On the x halo alone: the halo.
    CHECK(
        compositeReference(129, 120, P_400, params, image) ==
        blended(encodedTexel(texels, 32, 30), {0.0F, 0.0F, 0.0F}, rawreel::GRID_HALO_STRENGTH * a));
}

TEST_CASE("composite reference: a draw start shifts the pixel, not the lines") {
    const std::vector<std::uint16_t> texels =
        filled(100, 50, [](std::uint32_t x, std::uint32_t y, std::uint32_t c) {
            return static_cast<float>(((x * 3) + (y * 5) + (c * 7)) % 64) / 63.0F;
        });
    const HostDisplayImage image = hostImage(100, 50, texels);
    // The 100 x 50 image at zoom 1, centred in P of the output 300 x 200 with the strips {30, 30}.
    const PixelSize extent{300, 200};
    const dvv::media::ViewStrips strips{30, 30};
    const PixelSize p = dvv::media::pictureRegion(extent, strips).size();
    const rawreel::DisplayParams params{.view = {ViewMode::Zoom, 1, {50, 25}, {100, 50}},
                                        .overlay = plainGrid()};
    const ViewTransform view = resolveView(params.view, p, {100, 50});
    const rawreel::PassRegions regions = rawreel::passRegions(extent, strips, view);
    REQUIRE(regions.pictureDrawn != regions.picture);
    const CompositeConstants constants = compositeConstants(params.overlay, view, regions, false);
    REQUIRE(constants.drawX == regions.pictureDrawn.x - regions.picture.x);
    REQUIRE(constants.drawY == regions.pictureDrawn.y - regions.picture.y);
    REQUIRE(constants.drawX > 0);
    REQUIRE(constants.drawY > 0);
    std::size_t wrong = 0;
    std::size_t on_line = 0;
    for (std::uint32_t y = 0; y < regions.pictureDrawn.height; ++y) {
        for (std::uint32_t x = 0; x < regions.pictureDrawn.width; ++x) {
            const Rgba8 full =
                compositeReference(x + constants.drawX, y + constants.drawY, p, params, image);
            wrong += compositePixel(constants, x, y, image) != full ? 1 : 0;
            on_line +=
                full != compositeReference(x + constants.drawX, y + constants.drawY, p,
                                           {.view = params.view, .overlay = {.dark = true}}, image)
                    ? 1
                    : 0;
        }
    }
    CHECK(wrong == 0);
    CHECK(on_line > 0);  // the grid draws in the shifted region
}

TEST_CASE("composite constants: the thickness field holds 16 and more") {
    rawreel::OverlayParams overlay = plainGrid();
    overlay.levels[0].thickness = 16;
    overlay.levels[1].thickness = 300;
    const CompositeConstants c =
        compositeConstants(overlay, resolveView(AT_400, P_400, {64, 48}), {}, false);
    CHECK(((c.levelShape[0] >> rawreel::LEVEL_THICKNESS_SHIFT) & 0xFFU) == 16);
    CHECK(((c.levelShape[1] >> rawreel::LEVEL_THICKNESS_SHIFT) & 0xFFU) == 255);
    CHECK((c.levelShape[0] & rawreel::LEVEL_ADAPTIVE) == 0);
    CHECK((c.levelShape[1] & rawreel::LEVEL_ADAPTIVE) == 0);
    CHECK((c.levelShape[0] & rawreel::LEVEL_STEP_MASK) == 5);
    CHECK((c.levelShape[0] & rawreel::LEVEL_DRAWN) != 0);
    CHECK(c.levelColour[0] == whiteAt(85));
}

TEST_CASE("composite constants: a stepLog2 above 15 is drawn as 15, not wrapped") {
    rawreel::OverlayParams overlay = plainGrid();
    overlay.levels[0].stepLog2 = 16;
    const ViewTransform view = resolveView(AT_400, P_400, {64, 48});
    const CompositeConstants c = compositeConstants(overlay, view, {}, false);
    CHECK((c.levelShape[0] & rawreel::LEVEL_STEP_MASK) == 15);
    CHECK((c.levelShape[0] & rawreel::LEVEL_DRAWN) != 0);
    // Visible at 2^16 x 4 but not at 2^15 x 4: the visibility uses the clamped step too.
    overlay.levels[0].minimumSpacing = 200000;
    CHECK(rawreel::gridLevelsDrawn(overlay, view) ==
          std::array<bool, rawreel::GRID_LEVELS>{false, false, false});
    CHECK((compositeConstants(overlay, view, {}, false).levelShape[0] & rawreel::LEVEL_DRAWN) == 0);
}

TEST_CASE("composite constants: gridLevelsDrawn") {
    using Drawn = std::array<bool, rawreel::GRID_LEVELS>;
    const ViewTransform at_100 =
        resolveView({ViewMode::Zoom, 1, {32, 24}, {64, 48}}, {64, 48}, {64, 48});
    rawreel::OverlayParams overlay = plainGrid();
    overlay.levels[2].shown = true;
    CHECK(rawreel::gridLevelsDrawn(overlay, resolveView(AT_400, P_400, {64, 48})) ==
          Drawn{true, true, true});
    SUBCASE("major unchecked: none") {
        overlay.levels[0].shown = false;
        CHECK(rawreel::gridLevelsDrawn(overlay, resolveView(AT_400, P_400, {64, 48})) ==
              Drawn{false, false, false});
    }
    SUBCASE("major hidden by spacing: none") {
        const ViewTransform at_25 =
            resolveView({ViewMode::Zoom, 0.25, {32, 24}, {64, 48}}, {16, 12}, {64, 48});
        CHECK(rawreel::gridLevelsDrawn(overlay, at_25) == Drawn{false, false, false});
    }
    SUBCASE("minor unchecked") {
        overlay.levels[1].shown = false;
        CHECK(rawreel::gridLevelsDrawn(overlay, at_100) == Drawn{true, false, false});
    }
    SUBCASE("second minor hidden by spacing (2 x 1 < 6)") {
        CHECK(rawreel::gridLevelsDrawn(overlay, at_100) == Drawn{true, true, false});
    }
}
