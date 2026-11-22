#pragma once

// The CPU reference of the compositor (design M3 v2 §4.4). compositePixel()
// computes exactly what shaders/composite.slang computes, in the same order, in float32.

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "core/view.h"
#include "dvv/media/display.h"
#include "dvv/platform/pixel_size.h"
#include "render/composite_constants.h"
#include "render/display_state.h"

namespace rawreel {

inline constexpr std::uint32_t COMPOSITE_MAX_TAPS = 4;  // taps per axis below zoom 0.5
inline constexpr float BACKGROUND_DARK = 0.12F;         // sRGB-encoded grey of the background
inline constexpr float BACKGROUND_LIGHT = 0.85F;

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
// resolveView(params.view, viewport, image size), false), px, py, image). Empty `image`
// (no frame) → background.
[[nodiscard]] std::array<std::uint8_t, 4> compositeReference(std::uint32_t px, std::uint32_t py,
                                                             dvv::platform::PixelSize viewport,
                                                             const CompositeParams& params,
                                                             std::optional<HostDisplayImage> image);

}  // namespace rawreel
