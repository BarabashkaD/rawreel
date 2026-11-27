#pragma once

// The frames of the inspect GPU tests (gpu_test_inspect.cpp): seeded codes packed into a temporary
// raw file, bits planted into chosen stored words, the file opened through a GPU FrameStore with
// the Display, Planes and Raw leases of frame 0.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "core/frame_set.h"
#include "core/inspector.h"
#include "dvv/gpu/execution_context.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/media/frame_store.h"
#include "dvv/media/reference.h"
#include "dvv/media/sample_grid.h"
#include "dvv/media/types.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/pixel_size.h"
#include "gpu_support.h"
#include "inspect_reference.h"
#include "render/layer_block.h"
#include "render/view_renderer.h"
#include "render/view_state.h"
#include "temporary_directory.h"

namespace rawreel::testing {

// Bits ORed into the stored word of `sample` of `component` (word bits, e.g. padding bits).
struct PlantedBits {
    std::size_t component = 0;
    dvv::media::SamplePosition sample{};
    std::uint32_t bits = 0;
};

struct InspectFrame {  // members are released in reverse order: the frames first
    std::shared_ptr<dvv::gpu::ExecutionContext> context;  // on the best device
    std::shared_ptr<dvv::media::FrameStore> store;        // a GPU store
    std::unique_ptr<TemporaryDirectory> directory;        // holds the raw file
    std::vector<std::byte> bytes;                         // the file's bytes, planted bits in
    dvv::media::CodeValues reference;                     // unpack() of bytes
    FrameSet frames;  // frame 0: Display, Planes and Raw leases (gpuStackViews), epoch 1
};

// A `size` frame of `format`: pack() of seeded codes (the same for the same arguments), then
// `planted` ORed in at their storedWord() offsets, written to a temporary .raw file and opened as
// file:<path>?fmt=<name>&size=WxH (a custom format: fmt=@<its format file>) through a GPU
// FrameStore. REQUIREs every step.
[[nodiscard]] InspectFrame openInspectFrame(GpuTestEnvironment& env, dvv::media::FormatId format,
                                            dvv::platform::PixelSize size,
                                            std::span<const PlantedBits> planted = {});

// The custom 4:1:1 format "test-yuv411p" (planar 8-bit Y, U and V, U and V with log2SubX 2),
// interned on the first call (dvv-media's formatFromJson).
[[nodiscard]] dvv::media::FormatId testYuv411p();

// The custom 14-bit grey "test-gray14" (one little-endian 16-bit word per sample).
[[nodiscard]] dvv::media::FormatId testGray14();

// The custom 2-component "test-ya10" (planar 10-bit Y and A, 16-bit little-endian words).
[[nodiscard]] dvv::media::FormatId testYa10();

// The image size of most inspect cases.
inline constexpr dvv::platform::PixelSize INSPECT_IMAGE{.width = 37, .height = 23};

// The catalogue format `name` (REQUIREs it exists).
[[nodiscard]] dvv::media::FormatId formatNamed(std::string_view name);

// A ViewRenderer on `frame`'s context and epoch. REQUIREs the creation.
[[nodiscard]] std::unique_ptr<ViewRenderer> inspectRenderer(const InspectFrame& frame);

// render() of `params`, submitted and read back (RGBA8, row-major).
[[nodiscard]] std::vector<std::byte> renderInspect(GpuTestEnvironment& env,
                                                   const InspectFrame& frame,
                                                   ViewRenderer& renderer,
                                                   dvv::platform::PixelSize extent,
                                                   const ViewParams& params);

// A strip of ten 3-pixel digit cells, 5 rows, each cell's coverage a solid block.
[[nodiscard]] std::shared_ptr<const dvv::gpu::GlyphStrip> digitStrip();

// The RGBA8 pixel (x, y) of `bytes`, row-major at `extent`.
[[nodiscard]] std::array<std::uint8_t, 4> pixelOf(const std::vector<std::byte>& bytes,
                                                  dvv::platform::PixelSize extent, std::uint32_t x,
                                                  std::uint32_t y);

// An inspect view with the Planes panels.
[[nodiscard]] ViewParams planesParams();

// The zoom of the values cases: an 8x8 frame over a 768x768 Picture panel.
inline constexpr std::uint32_t VALUES_ZOOM = 96;

// A strip of the 17 value cells, `advance` x `height`, with `coverage(cell, x, y)`.
template <typename Coverage>
[[nodiscard]] std::shared_ptr<const dvv::gpu::GlyphStrip> valueStrip(std::uint32_t advance,
                                                                     std::uint32_t height,
                                                                     Coverage coverage) {
    auto strip = std::make_shared<dvv::gpu::GlyphStrip>();
    strip->width = VALUE_GLYPH_CELLS * advance;
    strip->height = height;
    strip->advance = advance;
    strip->ascent = height - 1;
    strip->descent = 1;
    strip->coverage.resize(std::size_t{strip->width} * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < strip->width; ++x) {
            strip->coverage.at((std::size_t{y} * strip->width) + x) =
                coverage(x / advance, x % advance, y);
        }
    }
    return strip;
}

// The values on at zoom 96: an 8x8 frame over a 768x768 Picture panel (Fit, no strips).
[[nodiscard]] ViewParams valuesParams(std::shared_ptr<const dvv::gpu::GlyphStrip> strip,
                                      ValueMetrics metrics);

inline constexpr std::uint32_t CELL_ZOOM = 12;

// A strip with no coverage and an advance that fits no cell at CELL_ZOOM: the values layer
// draws nothing, so only the lines differ from the plain view.
[[nodiscard]] ViewParams cellLineParams();

// The box of image pixel (x, y)'s cell at VALUES_ZOOM, inset by the pads, without a label row.
[[nodiscard]] TextBox cellBox(std::uint32_t x, std::uint32_t y, const ValueMetrics& metrics);

// The pixel as encoded colour channels in [0, 1].
[[nodiscard]] std::array<float, 3> encodedOf(const std::array<std::uint8_t, 4>& pixel);

// The hit of the cell levels `shapes` (PictureConstants::cellShape: level 0 the chroma outline,
// level 1 the cell lines) at P pixel (px, py), the CPU mirror of rows 2 and 3 of the line table
// and the inspect pipeline's winner: the smaller level, else the column.
[[nodiscard]] std::optional<dvv::media::LineHit> cellHit(const std::array<std::uint32_t, 3>& shapes,
                                                         const dvv::media::AxisView& ax,
                                                         const dvv::media::AxisView& ay,
                                                         std::int32_t px, std::int32_t py);

// The encoded `under` with the cell level line of `hit` over it, `colours` the layer block's
// colours0 (r | g << 8 | b << 16 | a << 24: [0] the cell line, [1] the chroma outline); `under`
// itself without a hit.
[[nodiscard]] std::array<float, 3> overCellLine(std::array<float, 3> under,
                                                const std::optional<dvv::media::LineHit>& hit,
                                                const std::array<std::uint32_t, 4>& colours);

struct CellCheck {
    std::size_t cells = 0;    // cells compared
    std::size_t inked = 0;    // P pixels where `on` differs from what the values go over
    std::size_t wrong = 0;    // channels off expectedValues by more than 1
    std::size_t flagged = 0;  // P pixels of a cell flag's frame or corner
};

// The cell (image pixels x0, x1, y0, y1) that draws a level 2 padding flag for image pixel (x, y):
// with LAYER_CELL_FLAGS, a flagged sample of a Planes panel's plane (its sample block the cell) or
// of an unsubsampled component (the pixel the cell); nullopt for none.
[[nodiscard]] std::optional<std::array<std::uint32_t, 4>> flaggedCell(const InspectFrame& frame,
                                                                      const LayerBlock& block,
                                                                      std::uint32_t x,
                                                                      std::uint32_t y);

// What the values layer draws, against `on`, for every P pixel of the image pixels [x0, x1) x
// [y0, y1) at `zoom`, skipping a cell whose centre luma is within 1/255 of the number colour's
// threshold (the GPU decides on the float, the reference on its 8-bit read); `off` is the same
// view without the values, `extent` big. `cell_shapes` are the cell levels drawn under the values
// (PictureConstants::cellShape; all 0 for none). `near_flags_only`: only the cells of a flagged
// sample and their 4 neighbours (the flag tests: the other cells are the values tests' and take
// most of the time).
[[nodiscard]] CellCheck checkCells(const InspectFrame& frame, const LayerBlock& block,
                                   const dvv::gpu::GlyphStrip& strip, std::uint32_t zoom,
                                   const std::vector<std::byte>& off,
                                   const std::vector<std::byte>& on,
                                   dvv::platform::PixelSize extent, std::array<std::uint32_t, 2> xs,
                                   std::array<std::uint32_t, 2> ys,
                                   const std::array<std::uint32_t, 3>& cell_shapes,
                                   bool near_flags_only = false);

}  // namespace rawreel::testing
