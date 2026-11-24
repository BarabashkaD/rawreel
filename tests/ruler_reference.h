#pragma once

// The CPU reference of the ruler pass: rulerPixel() computes exactly what shaders/rulers.slang
// computes, in the same order, in float32.

#include <array>
#include <cstdint>

#include "render/ruler_constants.h"

namespace rawreel {

// The ruler colours, 0xRRGGBB sRGB-encoded: light and dark theme.
inline constexpr std::uint32_t RULER_BACKGROUND_LIGHT = 0xd9d9d9, RULER_BACKGROUND_DARK = 0x323232;
inline constexpr std::uint32_t RULER_CORNER_LIGHT = 0xcfcfcf, RULER_CORNER_DARK = 0x262626;
inline constexpr std::uint32_t RULER_SEPARATOR_LIGHT = 0xb5b5b5, RULER_SEPARATOR_DARK = 0x3d3d3d;
inline constexpr std::uint32_t RULER_MAJOR_LIGHT = 0x333333, RULER_MAJOR_DARK = 0xe6e6e6;
inline constexpr std::uint32_t RULER_MINOR_LIGHT = 0x777777, RULER_MINOR_DARK = 0x9a9a9a;
inline constexpr float RULER_MARKER_OPACITY = 0.9F;  // blended in encoded space

// One pixel (RGBA8, alpha 255) of the band `constants`: output pixel (x, y), which lies in
// the band's region. Outside the ruler and the corner it is the viewport background,
// BACKGROUND_DARK or BACKGROUND_LIGHT of composite_reference.h.
[[nodiscard]] std::array<std::uint8_t, 4> rulerPixel(const RulerConstants& constants,
                                                     std::uint32_t x, std::uint32_t y) noexcept;

}  // namespace rawreel
