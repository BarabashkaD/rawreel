#pragma once

// The host side of picture.slang's push constants: the flag bits, the packed grid levels and
// the constants of one picture pass.

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/overlay.h"
#include "dvv/media/view.h"
#include "render/pass_regions.h"
#include "render/view_state.h"

namespace rawreel {

inline constexpr std::uint32_t PICTURE_DARK = 1;         // flags bit0: dark background
inline constexpr std::uint32_t PICTURE_SKIP_ENCODE = 2;  // flags bit1: the swapchain is _SRGB
// flags bit2: the picture blended over the checkerboard by its alpha
inline constexpr std::uint32_t PICTURE_ALPHA = 4;

// A grid level's levelShape word: stepLog2 in bits 0-3 (clamped to LEVEL_STEP_MASK, 15), the
// thickness in backing pixels in bits 4-11 (clamped to LEVEL_THICKNESS_MAX), adaptive, halo and
// drawn in bits 12, 13 and 14.
inline constexpr std::uint32_t LEVEL_STEP_MASK = 0xFU;
inline constexpr std::uint32_t LEVEL_THICKNESS_SHIFT = 4;
inline constexpr std::uint32_t LEVEL_THICKNESS_MAX = 255;
inline constexpr std::uint32_t LEVEL_ADAPTIVE = 1U << 12U;
inline constexpr std::uint32_t LEVEL_HALO = 1U << 13U;
inline constexpr std::uint32_t LEVEL_DRAWN = 1U << 14U;
// A halo darkens the pixel underneath by this share of its level's opacity.
inline constexpr float GRID_HALO_STRENGTH = 0.6F;

// = PushConstants of shaders/picture_common.slang (the picture pass and its grid prepass), 96
// bytes; the three words after checkerSize pad it to a multiple of 16.
struct PictureConstants {
    std::uint32_t viewportW = 0, viewportH = 0;  // the picture region P's size
    std::uint32_t imageW = 0, imageH = 0;
    float zoom = 0.0F;
    std::uint32_t flags = 0;
    std::uint32_t regionX = 0, regionY = 0;  // P's origin in the output
    std::int32_t offsetX = 0, offsetY = 0;   // the view's integer offset in P
    // The first pixel of P the pass draws, from P's origin (pictureDrawn - picture): thread id
    // draws P pixel id + draw. The shaders' uint4 below starts at 48.
    std::uint32_t drawX = 0, drawY = 0;
    // The shaders declare the two as uint4; [3] is unused.
    std::array<std::uint32_t, 4> levelColour{};  // GridLevelParams::colour
    std::array<std::uint32_t, 4> levelShape{};   // the LEVEL_* bits
    std::uint32_t checkerSize = 1;  // the checkerboard's square, backing pixels, at least 1
    // The cell levels (the inspect pipeline and the grid prepass only), a LEVEL_* word each, 0:
    // not drawn: the chroma outline's step on x and on y (thickness 2), the cell lines (step 0,
    // thickness 1).
    std::array<std::uint32_t, 3> cellShape{};
    bool operator==(const PictureConstants&) const = default;
};
static_assert(sizeof(PictureConstants) == 96);
static_assert(offsetof(PictureConstants, zoom) == 16 && offsetof(PictureConstants, flags) == 20 &&
              offsetof(PictureConstants, regionX) == 24 &&
              offsetof(PictureConstants, regionY) == 28 &&
              offsetof(PictureConstants, offsetX) == 32 &&
              offsetof(PictureConstants, offsetY) == 36 &&
              offsetof(PictureConstants, drawX) == 40 && offsetof(PictureConstants, drawY) == 44 &&
              offsetof(PictureConstants, levelColour) == 48 &&
              offsetof(PictureConstants, levelShape) == 64 &&
              offsetof(PictureConstants, checkerSize) == 80);

// The step the shaders draw: min(level.stepLog2, LEVEL_STEP_MASK), since levelShape holds 4 bits
// of it.
[[nodiscard]] std::uint32_t levelStepLog2(const GridLevelParams& level) noexcept;

// The grid levels drawn at `view`'s zoom: level l when levels[0].shown, levels[l].shown and
// dvv::media::levelVisible(min(stepLog2, LEVEL_STEP_MASK), float(view.zoom()), minimumSpacing);
// none when level 0 is not visible (the grid follows the ruler's major level). A stepLog2 above
// 15 is drawn as 15, here and in pictureConstants.
[[nodiscard]] std::array<bool, GRID_LEVELS> gridLevelsDrawn(
    const OverlayParams& overlay, const dvv::media::ViewTransform& view) noexcept;

// The constants of the picture pass of `regions` for `view`, resolved against P's size:
// viewportW/H = view.viewport(); zoom and offsetX/Y as dvv::media::axisX/axisY(view); imageW/H =
// view.image() when view.zoom() > 0, else 0 (no frame or an empty size: background only); flags =
// PICTURE_DARK when params.overlay.dark, | PICTURE_SKIP_ENCODE when skip_encode, |
// PICTURE_ALPHA when params.alpha; regionX/Y = regions.picture.x/y; drawX/Y =
// regions.pictureDrawn.x/y - regions.picture.x/y; per level, its colour and its shape with
// LEVEL_DRAWN by gridLevelsDrawn; checkerSize = max(params.overlay.checkerSize, 1). params.view
// and params.cursor are ignored: `view` is already resolved, and the cursor is the ruler pass's.
[[nodiscard]] PictureConstants pictureConstants(const ViewParams& params,
                                                const dvv::media::ViewTransform& view,
                                                const PassRegions& regions, bool skip_encode);

}  // namespace rawreel
