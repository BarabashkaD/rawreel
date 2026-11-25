#pragma once

// The CPU reference of the ruler pass: rulerPixel() computes exactly what shaders/rulers.slang
// computes, in the same order, in float32.

#include <array>
#include <cstdint>
#include <memory>

#include "core/ruler_labels.h"
#include "dvv/gpu/glyph_strip.h"
#include "render/ruler_constants.h"

namespace rawreel {

// The ruler colours, 0xRRGGBB sRGB-encoded: light and dark theme.
inline constexpr std::uint32_t RULER_BACKGROUND_LIGHT = 0xd9d9d9, RULER_BACKGROUND_DARK = 0x323232;
inline constexpr std::uint32_t RULER_CORNER_LIGHT = 0xcfcfcf, RULER_CORNER_DARK = 0x262626;
inline constexpr std::uint32_t RULER_SEPARATOR_LIGHT = 0xb5b5b5, RULER_SEPARATOR_DARK = 0x3d3d3d;
inline constexpr std::uint32_t RULER_MAJOR_LIGHT = 0x333333, RULER_MAJOR_DARK = 0xe6e6e6;
inline constexpr std::uint32_t RULER_MINOR_LIGHT = 0x777777, RULER_MINOR_DARK = 0x9a9a9a;
inline constexpr float RULER_MARKER_OPACITY = 0.9F;  // blended in encoded space
// The labels: the numbers' text, the end tags' fill and text, the cursor tag's text (its fill is
// the cursor colour), square boxes.
inline constexpr std::uint32_t RULER_TEXT_LIGHT = 0x333333, RULER_TEXT_DARK = 0xc9c9c9;
inline constexpr std::uint32_t RULER_TAG_FILL_LIGHT = 0x4d4d4d, RULER_TAG_TEXT_LIGHT = 0xf5f5f5;
inline constexpr std::uint32_t RULER_TAG_FILL_DARK = 0xe8e8e8, RULER_TAG_TEXT_DARK = 0x1b1b1b;
inline constexpr std::uint32_t RULER_CURSOR_TAG_BLACK = 0x000000, RULER_CURSOR_TAG_WHITE = 0xffffff;

// One pixel (RGBA8, alpha 255) of the band `constants`: output pixel (x, y), which lies in
// the band's region. Outside the ruler and the corner it is the viewport background,
// BACKGROUND_DARK or BACKGROUND_LIGHT of composite_reference.h. With RULER_NUMBERS and `glyphs`
// the label band shows rulerLabels() of the band (core/ruler_labels.h) drawn from `glyphs`, whose
// cells are glyphs->advance wide; null `glyphs` draws no labels.
[[nodiscard]] std::array<std::uint8_t, 4> rulerPixel(const RulerConstants& constants,
                                                     const dvv::gpu::GlyphStrip* glyphs,
                                                     std::uint32_t x, std::uint32_t y);

// The rulerLabels() input of the band `constants` with glyph cells `advance` wide, as the shader
// reads it from the constants.
[[nodiscard]] RulerLabelInput rulerLabelInput(const RulerConstants& constants,
                                              std::uint32_t advance);

// A synthetic glyph strip for the label tests: ten cells `advance` wide, `height` rows, the
// coverage of digit `cell` at `column` (inside the cell) and `row` labelCoverage(cell, column,
// row) = (cell x 37 + column x 11 + row x 5) & 0xff, so a reversed column, a wrong cell or a
// shifted row changes the pixels.
[[nodiscard]] std::uint32_t labelCoverage(std::uint32_t cell, std::uint32_t column,
                                          std::uint32_t row);
[[nodiscard]] std::shared_ptr<const dvv::gpu::GlyphStrip> labelStrip(std::uint32_t advance,
                                                                     std::uint32_t height);

}  // namespace rawreel
