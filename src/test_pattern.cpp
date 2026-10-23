#include "test_pattern.h"

#include <algorithm>

namespace rawreel {

namespace {

constexpr std::uint32_t BAR = 191;  // 75 % of 255, rounded down

constexpr std::uint32_t rgba(std::uint32_t r, std::uint32_t g, std::uint32_t b) noexcept {
    return r | (g << 8U) | (b << 16U) | (255U << 24U);
}

}  // namespace

std::uint32_t testPatternPixel(std::uint32_t x, std::uint32_t y, std::uint32_t width,
                               std::uint32_t height) noexcept {
    if (x == 0 || y == 0 || x == width - 1 || y == height - 1) {
        return rgba(255, 255, 255);
    }
    if (y * 3 < height * 2) {
        switch (x * 7 / width) {
            case 0: return rgba(BAR, BAR, BAR);  // white
            case 1: return rgba(BAR, BAR, 0);    // yellow
            case 2: return rgba(0, BAR, BAR);    // cyan
            case 3: return rgba(0, BAR, 0);      // green
            case 4: return rgba(BAR, 0, BAR);    // magenta
            case 5: return rgba(BAR, 0, 0);      // red
            default: return rgba(0, 0, BAR);     // blue
        }
    }
    const std::uint32_t v = x * 255 / std::max(width - 1, 1U);
    return rgba(v, v, v);
}

}  // namespace rawreel
