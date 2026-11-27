#pragma once

// The layer block of the inspect pipeline (shaders/layer_block.slang): the stored-word rule of
// each component, the layer flags, the component words, the value metrics, the cell shape, the
// colours and the rects of one panel. The layout is explicit and aligned: the same in std430 and
// scalar blocks. The block lives in an immutable HostUpload buffer, made only when its bytes
// change.

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "dvv/media/sample_grid.h"
#include "dvv/media/types.h"
#include "render/view_state.h"

namespace rawreel {

// LayerBlock::flags.
inline constexpr std::uint32_t LAYER_PLANE_SOURCE = 1;  // the panel shows plane sourceComponent
inline constexpr std::uint32_t LAYER_VALUES = 2;
inline constexpr std::uint32_t LAYER_CELL_LINES = 4;
inline constexpr std::uint32_t LAYER_CHROMA = 8;
inline constexpr std::uint32_t LAYER_CELL_FLAGS = 16;
inline constexpr std::uint32_t LAYER_PADDING_MARKS = 32;
inline constexpr std::uint32_t LAYER_MARKERS = 64;
inline constexpr std::uint32_t LAYER_TEST_CODES = 128;

// LayerBlock::component[k]: log2SubX in bits 0-3, log2SubY in 4-7, bits in 8-15, the letter's
// cell in 16-31.
inline constexpr std::uint32_t LAYER_SUB_Y_SHIFT = 4;
inline constexpr std::uint32_t LAYER_BITS_SHIFT = 8;
inline constexpr std::uint32_t LAYER_LETTER_SHIFT = 16;

// The layer colours: r | g << 8 | b << 16 | a << 24, sRGB-encoded; a = opacity.
inline constexpr std::uint32_t LAYER_CELL_LINE_COLOUR = 0x99808080U;   // #808080 at 60 %
inline constexpr std::uint32_t LAYER_CHROMA_COLOUR = 0xF2FFAA50U;      // #50AAFF at 95 %
inline constexpr std::uint32_t LAYER_LABEL_TEXT_COLOUR = 0xFFFFD09FU;  // #9FD0FF
inline constexpr std::uint32_t LAYER_LABEL_BOX_COLOUR = 0xB8000000U;   // black at 72 %
inline constexpr std::uint32_t LAYER_WARNING_COLOUR = 0xFF0A9FFFU;     // #FF9F0A
inline constexpr std::uint32_t LAYER_MARKER_RIM_COLOUR = 0xFF000000U;  // black: the markers' rim

struct LayerBlock {  // = LayerBlock of shaders/layer_block.slang, 432 bytes
    std::array<dvv::media::StoredWordLayout, 4> words{};  // per component; unused ones are 0
    std::uint32_t flags = 0;                              // the LAYER_* flags
    std::uint32_t sourceComponent = 0;                    // the plane of a Planes panel
    std::uint32_t componentCount = 0;
    std::uint32_t markSize = 0;
    std::array<std::uint32_t, 4> component{};  // per component: the LAYER_*_SHIFT words
    std::array<std::uint32_t, 4> metrics{};
    std::array<std::uint32_t, 4> reserved{};  // unused
    // cell line, chroma outline, label text, label box
    std::array<std::uint32_t, 4> colours0{};
    std::uint32_t warning = 0, marker = 0, markRim = 0, tileSize = 0;
    std::array<std::uint32_t, 4> pinRect{};
    std::array<std::uint32_t, 4> outlineRect{};
    std::uint32_t tilesX = 0, tilesY = 0;  // the tile map of the padding marks
    std::uint32_t markerRim = 0;  // the markers' rim colour (markRim is the padding marks' rim)
    std::uint32_t pad = 0;
};
static_assert(sizeof(LayerBlock) == 432 && alignof(LayerBlock) == 8);
// No padding bytes: two blocks are equal when their bytes are.
static_assert(std::has_unique_object_representations_v<LayerBlock>);
static_assert(offsetof(LayerBlock, words) == 0 && offsetof(LayerBlock, flags) == 288 &&
              offsetof(LayerBlock, sourceComponent) == 292 &&
              offsetof(LayerBlock, componentCount) == 296 &&
              offsetof(LayerBlock, markSize) == 300 && offsetof(LayerBlock, component) == 304 &&
              offsetof(LayerBlock, metrics) == 320 && offsetof(LayerBlock, reserved) == 336 &&
              offsetof(LayerBlock, colours0) == 352 && offsetof(LayerBlock, warning) == 368 &&
              offsetof(LayerBlock, marker) == 372 && offsetof(LayerBlock, markRim) == 376 &&
              offsetof(LayerBlock, tileSize) == 380 && offsetof(LayerBlock, pinRect) == 384 &&
              offsetof(LayerBlock, outlineRect) == 400 && offsetof(LayerBlock, tilesX) == 416 &&
              offsetof(LayerBlock, tilesY) == 420 && offsetof(LayerBlock, markerRim) == 424);

// Whether the values layer draws: inspect.on with its numbers and a value strip.
[[nodiscard]] inline bool valuesOn(const ViewParams& params) noexcept {
    return params.inspect.on && params.inspect.numbers && params.inspect.glyphs != nullptr;
}

// The layer block of panel `panel` of kind `kind` for a frame of `desc`: the stored-word layouts
// of the format's components (0 for an unknown format or a component without one), the component
// words with params.inspect.letterCells, the colours (the marker in params.overlay.cursorColour,
// opaque), LAYER_PLANE_SOURCE with sourceComponent = panel for a Planes panel, LAYER_VALUES when
// valuesOn(params), LAYER_CELL_FLAGS with params.inspect.on, LAYER_PADDING_MARKS with the overlay's
// mark metrics on a Picture panel with params.paddingMarks (tilesX and tilesY are the
// renderer's), the metrics from params.inspect.metrics, and with markersOn(params.markers)
// LAYER_MARKERS, the pin cell's pinRect and the outline's outlineRect of markerCells, each
// {x, y, x + width, y + height} (image pixels; all 0 where a marker is not set).
[[nodiscard]] LayerBlock layerBlock(const ViewParams& params, const dvv::media::FrameDesc& desc,
                                    std::uint32_t panel, PanelKind kind);

// The cells the markers of `markers` frame in panel `panel` of kind `kind` for a frame of
// `desc`: in a Planes panel of a subsampled plane the pin's sample block of that plane (clipped
// to the image), the cell the panel draws for that sample; else pixelCells(markers).
[[nodiscard]] MarkerCells markerCells(const MarkerParams& markers,
                                      const dvv::media::FrameDesc& desc, std::uint32_t panel,
                                      PanelKind kind);

// Whether the Raw view may back the padding reads of a layer block built from `desc` (the cell
// flags and the padding check read rawBase through desc's stored-word layouts): the Raw lease's
// own desc `raw` has desc's format and size, the display image is that size, and the 8-byte
// window the shaders read at the last sample of each of the first four components lies in
// `raw_bytes`. A false result clears LAYER_CELL_FLAGS and LAYER_PADDING_MARKS.
[[nodiscard]] bool rawReadsFit(const dvv::media::FrameDesc& raw, std::uint64_t raw_bytes,
                               const dvv::media::FrameDesc& desc, std::uint32_t display_width,
                               std::uint32_t display_height);

// The cell levels of a frame of `desc` for PictureConstants::cellShape: with valuesOn(params), the
// chroma outline (step log2SubX on x and log2SubY on y, the coarsest over the components, thickness
// 2, drawn only on an axis with subsampling) and the cell lines (step 0, thickness 1); else all 0.
[[nodiscard]] std::array<std::uint32_t, 3> cellShapes(const ViewParams& params,
                                                      const dvv::media::FrameDesc& desc);

}  // namespace rawreel
