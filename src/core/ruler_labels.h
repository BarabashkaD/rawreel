#pragma once

// The ruler's labels on the CPU: which numbers and tags a ruler draws, their values and their
// boxes along the axis. The ruler shader draws the same labels per pixel; this is the one rule it
// mirrors, and the ruler reference and the unit tests use exactly this.
//
// Units: backing pixels of the output image, along the axis (x on the top ruler, y on the left
// ruler). A box is a half-open span [begin, end). Two boxes touch when one, widened by `advance` on
// each side, overlaps the other.
//
// The rule, with L = rulerBegin + tagMargin, R = rulerEnd - tagMargin and a tag of value v being
// decimalDigits(v) x advance + 2 x tagPad long:
// - The end values come from visiblePixels(axis): first, and last (LastValue::Pixel) or last + 1
//   (LastValue::Edge).
// - The first tag's box is [L, L + len) and is drawn when L + len <= R. The last tag's box is
//   [R - len', R) and is drawn when the first one is and len + advance + len' <= R - L.
// - The cursor tag (cursor set and below axis.size) is centred on
//   centre = pictureStart + floor(pixelCentreAt(axis, cursor)): begin = centre - len / 2, then
//   begin = max(L, min(R - len, begin)), so the start margin wins on a ruler shorter than the tag.
//   An end tag that touches it is dropped; the fit of the last tag is decided before that, so a
//   dropped first tag leaves the last one.
// - Numbers only when ticksShown: the step is 1 << s with s = numberStepLog2(majorStepLog2, zoom,
//   float(decimalDigits(size) x advance + advance), size). For each multiple k of the step in
//   [0, size] the text begins at pictureStart + firstPixelOf(axis, k) + numberGap and is
//   decimalDigits(k) x advance long; it is drawn when it lies in [rulerBegin, rulerEnd) and
//   touches no drawn tag.
// - Pixels are saturated to [-2^30, 2^30], as in dvv-media's line rules: the cursor tag's
//   floor(pixelCentreAt) and its centre, and each number's text begin, so no box end overflows.
// - A step log2 of 32 or more gives the single number 0.
// - On a ruler shorter than the cursor tag its box reaches past the ruler's end; drawing clips it
//   to [rulerBegin, rulerEnd). An end tag is drawn only where it fits.

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "core/overlay.h"
#include "dvv/media/view_lines.h"

namespace rawreel {

// The glyph strip's cells, in order; the shader's cell count is RULER_GLYPH_CELLS.
inline constexpr std::string_view RULER_DIGITS = "0123456789";

// The label spacing in device-independent pixels (pt): the number's gap after its tick, a tag's
// padding along, a tag's inset across from the label band, and the end tags' margin inside the
// ruler's ends.
inline constexpr double RULER_NUMBER_GAP_DIP = 2;
inline constexpr double RULER_TAG_PAD_DIP = 3;
inline constexpr double RULER_TAG_INSET_DIP = 1;
inline constexpr double RULER_TAG_MARGIN_DIP = 2;

// The label spacing in backing pixels, each the pt value above by the one pt rule, at most 255.
struct LabelSpacing {
    std::uint32_t numberGap = 0, tagPad = 0, tagInset = 0, tagMargin = 0;
    bool operator==(const LabelSpacing&) const = default;
};

enum class RulerLabelKind : std::uint8_t { Number, FirstTag, LastTag, CursorTag };

struct RulerLabel {
    RulerLabelKind kind{};
    std::uint32_t value = 0;
    std::int32_t textBegin = 0;  // along, output pixels; decimalDigits(value) x advance long
    // Along, output pixels: the text for a number, the text +- tagPad for a tag.
    std::int32_t boxBegin = 0, boxEnd = 0;
    bool operator==(const RulerLabel&) const = default;
};

struct RulerLabelInput {
    dvv::media::AxisView axis{};    // its pixels counted from the picture region's start
    std::int32_t pictureStart = 0;  // the picture region's first output pixel on the axis
    std::int32_t rulerBegin = 0, rulerEnd = 0;  // the ruler along the axis
    std::uint32_t advance = 0;  // a glyph cell's width; 0: no glyph strip, no labels
    LabelSpacing spacing{};
    std::uint32_t majorStepLog2 = 0;
    bool numbers = false;                   // the Ruler numbers setting
    bool ticksShown = false;                // the major level is visible by its spacing
    std::optional<std::uint32_t> cursor{};  // the cursor's image pixel on the axis
    LastValue lastValue = LastValue::Pixel;
};

// The number of decimal digits of `value`; 1 for 0.
[[nodiscard]] std::uint32_t decimalDigits(std::uint32_t value) noexcept;

// The labels drawn, in drawing order: the numbers ascending, the end tags (first, last), the
// cursor tag. Empty with numbers off, advance 0, or no image on the axis (visiblePixels nullopt).
[[nodiscard]] std::vector<RulerLabel> rulerLabels(const RulerLabelInput& input);

// Whether the cursor tag's text is black on a fill of `colour`: the colour's relative luminance
// (sRGB decoded, Rec. 709 weights) is above 0.5; else the text is white.
[[nodiscard]] bool cursorTagTextBlack(Rgb8 colour) noexcept;

}  // namespace rawreel
