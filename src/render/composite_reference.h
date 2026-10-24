#pragma once

// The compositor's push constants and its CPU reference (design M3 v2 §4.4). compositePixel()
// computes exactly what shaders/composite.slang computes, in the same order, in float32.

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "dvv/platform/native_window.h"
#include "render/display_state.h"

namespace rawreel {

inline constexpr std::uint32_t COMPOSITE_DARK = 1;         // flags bit0: dark background
inline constexpr std::uint32_t COMPOSITE_SKIP_ENCODE = 2;  // flags bit1: the swapchain is _SRGB
inline constexpr std::uint32_t COMPOSITE_MAX_TAPS = 4;     // taps per axis below zoom 0.5
inline constexpr float BACKGROUND_DARK = 0.12F;            // sRGB-encoded grey of the background
inline constexpr float BACKGROUND_LIGHT = 0.85F;

struct CompositeConstants {  // = PushConstants of composite.slang
    std::uint32_t viewportW = 0, viewportH = 0, imageW = 0, imageH = 0;
    float zoom = 0.0F, originX = 0.0F, originY = 0.0F;
    std::uint32_t flags = 0;
};
static_assert(sizeof(CompositeConstants) == 32);

// The constants of one composite at `viewport` (design §4.4): view = ViewTransform::fit(viewport,
// image); viewportW/H = viewport; zoom/originX/originY = float(view.zoom()/origin()); imageW/H =
// image when view.zoom() > 0, else 0 (no frame or an empty size: background only); flags =
// COMPOSITE_DARK when params.dark, | COMPOSITE_SKIP_ENCODE when skip_encode.
[[nodiscard]] CompositeConstants compositeConstants(const CompositeParams& params,
                                                    dvv::platform::PixelSize viewport,
                                                    dvv::platform::PixelSize image,
                                                    bool skip_encode);

struct HostDisplayImage {  // host copy of a display view
    std::uint32_t width = 0, height = 0;
    std::span<const std::uint16_t> rgba16f;  // half floats, row-major, width·height·4
};
// One output pixel (RGBA8, alpha 255) for these constants. When constants.imageW > 0, `image`
// is present with width == imageW and height == imageH; otherwise it is ignored.
[[nodiscard]] std::array<std::uint8_t, 4> compositePixel(
    const CompositeConstants& constants, std::uint32_t px, std::uint32_t py,
    const std::optional<HostDisplayImage>& image) noexcept;
// The CPU reference of one output pixel (design §4.4): compositePixel(compositeConstants(params,
// viewport, image ? {image->width, image->height} : {0, 0}, false), px, py, image). Empty `image`
// (no frame) → background.
[[nodiscard]] std::array<std::uint8_t, 4> compositeReference(std::uint32_t px, std::uint32_t py,
                                                             dvv::platform::PixelSize viewport,
                                                             const CompositeParams& params,
                                                             std::optional<HostDisplayImage> image);

}  // namespace rawreel
