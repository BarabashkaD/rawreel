#pragma once

// The CPU reference of the compositor (design M3 v2 §4.4). compositePixel()
// computes exactly what shaders/picture.slang computes, in the same order, in float32.

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "dvv/media/display.h"
#include "dvv/platform/pixel_size.h"
#include "render/picture_constants.h"
#include "render/view_state.h"

namespace rawreel {

inline constexpr std::uint32_t PICTURE_MAX_TAPS = 4;  // taps per axis below zoom 0.5
inline constexpr float BACKGROUND_DARK = 0.12F;       // sRGB-encoded grey of the background
inline constexpr float BACKGROUND_LIGHT = 0.85F;
// The alpha checkerboard's sRGB-encoded greys, checkerGrey of shaders/viewport_background.slang:
// per theme the even squares (the one at the image origin) and the odd ones.
inline constexpr float CHECKER_LIGHT_EVEN = 1.0F;
inline constexpr float CHECKER_LIGHT_ODD = 0.70F;
inline constexpr float CHECKER_DARK_EVEN = 0.35F;
inline constexpr float CHECKER_DARK_ODD = 0.25F;

struct HostDisplayImage {  // host copy of a display view
    std::uint32_t width = 0, height = 0;
    std::span<const std::uint16_t> rgba16f;  // half floats, row-major, width·height·4
};
// One pixel (RGBA8, alpha 255) for these constants: the pixel thread (px, py) draws, P pixel
// (px + drawX, py + drawY), which the shader writes to output pixel (regionX + px + drawX,
// regionY + py + drawY). When constants.imageW > 0, `image` is present with width == imageW and
// height == imageH; otherwise it is ignored.
[[nodiscard]] std::array<std::uint8_t, 4> compositePixel(
    const PictureConstants& constants, std::uint32_t px, std::uint32_t py,
    const std::optional<HostDisplayImage>& image) noexcept;
// The CPU reference of pixel (px, py) of a picture region P of size `viewport`, with view =
// resolveView(params.view, viewport, image size): compositePixel(pictureConstants(
// params, view, passRegions(viewport, {}, view), false), px, py, image) (no strips, so
// the pass draws all of P). Empty `image` (no frame) → background.
[[nodiscard]] std::array<std::uint8_t, 4> compositeReference(std::uint32_t px, std::uint32_t py,
                                                             dvv::platform::PixelSize viewport,
                                                             const ViewParams& params,
                                                             std::optional<HostDisplayImage> image);

}  // namespace rawreel
