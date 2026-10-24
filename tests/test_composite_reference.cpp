// The compositor's push constants, their reflection and its CPU reference (design M3 v2 §4.4,
// §8.1): fit placement, nearest enlargement, the multi-tap downscale and the background.
#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "core/view.h"
#include "dvv/base/json.h"
#include "dvv/media/display.h"
#include "rawreel_shaders/composite.h"
#include "render/composite_reference.h"
#include "render/viewport_renderer.h"

using dvv::media::encodeSrgb8;
using dvv::media::floatToHalf;
using dvv::media::halfToFloat;
using dvv::media::srgbDecode;
using dvv::platform::PixelSize;
using rawreel::COMPOSITE_DARK;
using rawreel::COMPOSITE_SKIP_ENCODE;
using rawreel::CompositeConstants;
using rawreel::compositeConstants;
using rawreel::compositePixel;
using rawreel::compositeReference;
using rawreel::HostDisplayImage;
using rawreel::ViewTransform;

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

constexpr rawreel::CompositeParams DARK{.dark = true};
constexpr rawreel::CompositeParams LIGHT{.dark = false};

}  // namespace

TEST_CASE("composite constants: layout") {
    CHECK(offsetof(CompositeConstants, viewportW) == 0);
    CHECK(offsetof(CompositeConstants, viewportH) == 4);
    CHECK(offsetof(CompositeConstants, imageW) == 8);
    CHECK(offsetof(CompositeConstants, imageH) == 12);
    CHECK(offsetof(CompositeConstants, zoom) == 16);
    CHECK(offsetof(CompositeConstants, originX) == 20);
    CHECK(offsetof(CompositeConstants, originY) == 24);
    CHECK(offsetof(CompositeConstants, flags) == 28);
    CHECK(sizeof(CompositeConstants) == 32);
}

TEST_CASE("composite shader: the push block matches CompositeConstants") {
    const auto reflection = dvv::json::parse(rawreel_shaders::composite().reflectionJson);
    REQUIRE(reflection.has_value());
    const dvv::json::Value* block = nullptr;
    for (const auto& parameter : reflection->at("parameters")) {
        if (dvv::json::get<std::string>(parameter.at("binding"), "kind") == "pushConstantBuffer") {
            block = &parameter;
        }
    }
    REQUIRE(block != nullptr);
    const auto& type = block->at("type");
    CHECK(dvv::json::get<std::uint32_t>(type.at("elementVarLayout").at("binding"), "size") == 32U);
    std::map<std::string, std::uint32_t> offsets;
    for (const auto& field : type.at("elementType").at("fields")) {
        offsets[dvv::json::get<std::string>(field, "name").value_or("?")] =
            dvv::json::get<std::uint32_t>(field.at("binding"), "offset").value_or(~0U);
    }
    const std::map<std::string, std::uint32_t> expected{
        {"viewportW", 0}, {"viewportH", 4}, {"imageW", 8},   {"imageH", 12},
        {"zoom", 16},     {"originX", 20},  {"originY", 24}, {"flags", 28}};
    CHECK(offsets == expected);
}

TEST_CASE("composite constants: mapping") {
    const CompositeConstants c = compositeConstants(DARK, {211, 97}, {37, 23}, false);
    CHECK(c.viewportW == 211);
    CHECK(c.viewportH == 97);
    CHECK(c.imageW == 37);
    CHECK(c.imageH == 23);
    CHECK(c.zoom == static_cast<float>(97.0 / 23.0));
    CHECK(c.originX == static_cast<float>(ViewTransform::fit({211, 97}, {37, 23}).origin().x));
    CHECK(c.originY == 0.0F);
    CHECK(std::abs((-static_cast<double>(c.originX) * static_cast<double>(c.zoom)) - 27.0) < 1e-4);
    CHECK(c.flags == COMPOSITE_DARK);
    CHECK(compositeConstants(DARK, {211, 97}, {37, 23}, true).flags ==
          (COMPOSITE_DARK | COMPOSITE_SKIP_ENCODE));
    CHECK(compositeConstants(LIGHT, {211, 97}, {37, 23}, false).flags == 0);
    const CompositeConstants empty = compositeConstants(DARK, {211, 97}, {0, 0}, false);
    CHECK(empty.imageW == 0);
    CHECK(empty.imageH == 0);
    CHECK(empty.zoom == 0.0F);
    CHECK(empty.viewportW == 211);
    CHECK(empty.viewportH == 97);
}

TEST_CASE("composite reference: background") {
    CHECK(bg(true) == Rgba8{31, 31, 31, 255});
    CHECK(bg(false) == Rgba8{217, 217, 217, 255});
    for (const bool dark : {true, false}) {
        CAPTURE(dark);
        const rawreel::CompositeParams params{.dark = dark};
        CHECK(compositeReference(0, 0, {211, 97}, params, std::nullopt) == bg(dark));
        CHECK(compositeReference(210, 96, {211, 97}, params, std::nullopt) == bg(dark));
    }
    const auto linear = static_cast<std::uint8_t>(std::lround(255.0F * srgbDecode(0.12F)));
    CHECK(compositePixel(compositeConstants(DARK, {4, 4}, {0, 0}, true), 0, 0, std::nullopt) ==
          grey8(linear));
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
    CHECK(compositePixel(compositeConstants(LIGHT, {1, 1}, {1, 1}, true), 0, 0, image) ==
          Rgba8{64, 64, 64, 255});
}

TEST_CASE("isSrgbFormat") {
    CHECK(rawreel::isSrgbFormat(VK_FORMAT_B8G8R8A8_SRGB));
    CHECK(rawreel::isSrgbFormat(VK_FORMAT_R8G8B8A8_SRGB));
    CHECK(rawreel::isSrgbFormat(VK_FORMAT_A8B8G8R8_SRGB_PACK32));
    CHECK_FALSE(rawreel::isSrgbFormat(VK_FORMAT_B8G8R8A8_UNORM));
    CHECK_FALSE(rawreel::isSrgbFormat(VK_FORMAT_R8G8B8A8_UNORM));
}
