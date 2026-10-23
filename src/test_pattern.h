#pragma once

#include <cstdint>

namespace rawreel {

// Test pattern (design §4.6): 75 % colour bars (191) in the upper two thirds, a horizontal grey
// ramp in the lower third, a 1-pixel white border. Integer math, sRGB-encoded RGBA8 code values, R
// in the low byte, alpha 255. Exactly what shaders/test_pattern.slang computes.
[[nodiscard]] std::uint32_t testPatternPixel(std::uint32_t x, std::uint32_t y, std::uint32_t width,
                                             std::uint32_t height) noexcept;

}  // namespace rawreel
