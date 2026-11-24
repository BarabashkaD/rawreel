#pragma once

// The host side of rulers.slang's push constants: the flag bits and the constants of the two
// ruler bands (the top band with the corner, and the left band below it), each drawing its ruler
// and the viewport background around it.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "dvv/media/view.h"
#include "render/display_state.h"
#include "render/pass_regions.h"

namespace rawreel {

inline constexpr std::uint32_t RULER_DARK = 1;         // the dark theme's colours
inline constexpr std::uint32_t RULER_SKIP_ENCODE = 2;  // the swapchain is _SRGB: write decoded
inline constexpr std::uint32_t RULER_TICKS = 4;        // the ticks of the drawn levels
inline constexpr std::uint32_t RULER_CURSOR = 8;       // the cursor marker at `cursor`
inline constexpr std::uint32_t RULER_IMAGE = 16;       // an image is shown
inline constexpr std::uint32_t RULER_LIT = 32;         // the lit tick at `litTick`

// = RulerConstants of shaders/rulers.slang, 128 bytes (the Vulkan guarantee) with no padding.
struct RulerConstants {
    // The dispatched band in the output, clipped to the extent.
    std::uint32_t regionX = 0, regionY = 0, regionW = 0, regionH = 0;
    std::uint32_t axis = 0;   // 0: the top band (x axis, the corner included); 1: the left band
    std::uint32_t flags = 0;  // the RULER_* bits
    float zoom = 0;
    std::int32_t offset = 0;  // the view's integer offset on the axis, from P's edge
    // P's first output pixel and its size on the axis; the image's size on the axis.
    std::uint32_t pictureStart = 0, pictureExtent = 0, imageSize = 0;
    // The strip's thickness toward P as pictureRegion clamps it to the extent (P.y on the top
    // band, P.x on the left band, so rulerEdge - strip never underflows), its inner tick band, the
    // tick width and the marker's depth from the inner edge, all in backing pixels.
    std::uint32_t strip = 0, tickBand = 0, tickWidth = 1, markerDepth = 0;
    std::uint32_t cursor = 0;  // the cursor's image coordinate on the axis (RULER_CURSOR)
    // strip - tickBand: the numbers' band; the lit tick reaches litDepth - tickBand into it.
    std::uint32_t labelBand = 0;
    // The first row (top band) or column (left band) past the ruler: H.y or H.x of the hugged
    // rect H. The ruler lies in [rulerEdge - strip, rulerEdge) across the axis and in
    // [rulerBegin, rulerEnd) along it (H's span); the corner (top band only) in
    // [rulerBegin - pictureStart, rulerBegin) along it. The shader's uint4 starts at 80.
    std::uint32_t rulerEdge = 0, rulerBegin = 0, rulerEnd = 0;
    // The shader declares the two as uint4; [3] is unused.
    std::array<std::uint32_t, 4> levelShape{};  // stepLog2 | LEVEL_DRAWN, as the picture pass
    std::array<std::uint32_t, 4> tickLength{};  // per level, from the inner edge
    std::uint32_t cursorColour = 0;  // the lit tick and the marker: 0xRRGGBB, sRGB-encoded
    std::uint32_t litTick = 0;       // the lit tick's image coordinate on the axis (RULER_LIT)
    // The lit tick's depth from the inner edge: overlay.litDepth, at most `strip`.
    std::uint32_t litDepth = 0;
    std::uint32_t reserved1 = 0;
    bool operator==(const RulerConstants&) const = default;
};
static_assert(sizeof(RulerConstants) == 128);
static_assert(
    offsetof(RulerConstants, regionW) == 8 && offsetof(RulerConstants, axis) == 16 &&
    offsetof(RulerConstants, flags) == 20 && offsetof(RulerConstants, zoom) == 24 &&
    offsetof(RulerConstants, offset) == 28 && offsetof(RulerConstants, pictureStart) == 32 &&
    offsetof(RulerConstants, imageSize) == 40 && offsetof(RulerConstants, strip) == 44 &&
    offsetof(RulerConstants, tickBand) == 48 && offsetof(RulerConstants, tickWidth) == 52 &&
    offsetof(RulerConstants, markerDepth) == 56 && offsetof(RulerConstants, cursor) == 60 &&
    offsetof(RulerConstants, labelBand) == 64 && offsetof(RulerConstants, rulerEdge) == 68 &&
    offsetof(RulerConstants, rulerBegin) == 72 && offsetof(RulerConstants, rulerEnd) == 76 &&
    offsetof(RulerConstants, levelShape) == 80 && offsetof(RulerConstants, tickLength) == 96 &&
    offsetof(RulerConstants, cursorColour) == 112 && offsetof(RulerConstants, litTick) == 116 &&
    offsetof(RulerConstants, litDepth) == 120);

// The two bands of `regions` = passRegions(extent, overlay.strips, view), with `view` resolved
// against P's size (view.viewport() == regions.picture.size()): each band takes the zoom, offset,
// image size and P's extent on its axis from axisX(view) or axisY(view), as compositeConstants
// does, so the ticks match the grid lines; P's origin, the strips and the ruler's place come from
// `regions`. [0] the top band regions.bands[0] on the x axis around regions.rulers.top and the
// corner, [1] the left band regions.bands[1] on the y axis around regions.rulers.left; an empty
// band is not dispatched. RULER_IMAGE when view.zoom() > 0 (without an image the rulers show only
// their background and separator). Then RULER_TICKS on an axis when
// rulerTicksShown({levelStepLog2(levels[0]), zoom, levels[0].minimumSpacing, numbers ?
// labelWidth(imageSize, digitAdvance) : nullopt}), and per level LEVEL_DRAWN when it is visible
// by spacing (the level checkboxes affect the grid only); RULER_CURSOR when `cursor` (the image
// pixel under the cursor) is set, and with RULER_TICKS also RULER_LIT when nearestTick(the
// cursor on the axis, imageSize, the levels with LEVEL_DRAWN) has a value, litTick that value.
// litDepth is min(strip, overlay.litDepth) on every band.
// cursorColour is overlay.cursorColour on both bands.
[[nodiscard]] std::array<RulerConstants, 2> rulerConstants(
    const OverlayParams& overlay, const dvv::media::ViewTransform& view, const PassRegions& regions,
    std::optional<dvv::media::ImagePixel> cursor, bool skip_encode);

}  // namespace rawreel
