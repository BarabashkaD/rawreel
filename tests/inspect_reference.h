#pragma once

// The CPU reference of the inspect layers (gpu_test_inspect.cpp): what a plane panel shows and
// which sample and cell an image pixel belongs to. Built without FP contraction, as the shaders'
// arithmetic is precise.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/inspector.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/media/reference.h"
#include "dvv/media/sample_grid.h"
#include "dvv/media/types.h"
#include "dvv/media/view.h"
#include "dvv/platform/pixel_size.h"

namespace rawreel::testing {

// The grey a plane panel shows for `code` of a `bits`-bit component: code / (2^bits - 1), in
// [0, 1] (e.g. code / 1023 for 10 bits).
[[nodiscard]] float planeGrey(std::uint32_t code, std::uint32_t bits) noexcept;

// The sample of `component` of `format` that image pixel `pixel` belongs to (dvv-media sampleOf).
[[nodiscard]] dvv::media::SamplePosition sampleAt(dvv::media::FormatId format,
                                                  std::size_t component,
                                                  dvv::media::ImagePixel pixel);

// The cell of `pixel` for `component`: the image pixels of its sample (dvv-media blockOf).
[[nodiscard]] dvv::media::PixelBlock cellOf(dvv::media::FormatId format, std::size_t component,
                                            dvv::platform::PixelSize image,
                                            dvv::media::ImagePixel pixel);

// The code of `component` shown at image pixel `pixel`: `codes` (unpack()) at sampleAt(pixel).
[[nodiscard]] std::uint32_t codeAt(const dvv::media::CodeValues& codes, std::size_t component,
                                   dvv::media::ImagePixel pixel);

// The values layer (shaders/picture_inspect.slang), mirrored: the VALUE_GLYPHS cells of a strip,
// stacked text lines centred in a box of P pixels, the chroma label on its box, and the colours
// they draw over a background.
inline constexpr std::uint32_t TEXT_SPACE = 0xFFFFFFFFU;  // a skipped cell
inline constexpr std::uint32_t VALUE_GLYPH_CELLS = 17;    // rawreel::VALUE_GLYPHS

struct TextBox {  // [x0, x1) x [y0, y1), P pixels
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
};

struct TextOrigin {  // the top-left P pixel of a line's first glyph
    int left = 0, top = 0;
};

// The cells of the decimal digits of `code`, most significant first.
[[nodiscard]] std::vector<std::uint32_t> codeCells(std::uint32_t code);

// Line `line` of `lines` lines of `length` cells, centred in `box` (C-style truncating halves): a
// cell is the strip's width / VALUE_GLYPH_CELLS wide, a glyph is the strip's height tall, in the
// middle of its metrics.lineHeight line.
[[nodiscard]] TextOrigin lineOrigin(const dvv::gpu::GlyphStrip& strip,
                                    const rawreel::ValueMetrics& metrics, TextBox box,
                                    std::size_t lines, std::size_t line, std::size_t length);

// The coverage, 0..1, of `cells` drawn from `origin` at P pixel (px, py).
[[nodiscard]] float textCoverage(const dvv::gpu::GlyphStrip& strip,
                                 std::span<const std::uint32_t> cells, TextOrigin origin, int px,
                                 int py);

// The number colour (each channel) over the encoded cell colour `cell`: 0 when its luma (709
// weights) is above 0.5, else 1.
[[nodiscard]] float numberText(std::array<float, 3> cell) noexcept;

// The encoded colour of P pixel (px, py) with the lines `lines` (each its cells) stacked and
// centred in `box`, line i drawn in `texts[i]` (encoded) over the encoded colour `under`.
[[nodiscard]] std::array<float, 3> linesPixel(const dvv::gpu::GlyphStrip& strip,
                                              const rawreel::ValueMetrics& metrics, TextBox box,
                                              std::span<const std::vector<std::uint32_t>> lines,
                                              std::span<const std::array<float, 3>> texts,
                                              std::array<float, 3> under, int px, int py);

// LayerBlock::colours0 .z (the text) and .w (the box), and the underline colour (warning),
// 0xAABBGGRR.
struct LabelColours {
    std::uint32_t text = 0, box = 0, warning = 0;
};

// The chroma label `cells` of the block [x0, x1) whose bottom is `bottom` (P pixels), over the
// encoded colour `under` at P pixel (px, py): its box, lineHeight tall and the text plus 2 padX
// wide, centred in the block and padY above its bottom, blended at the box colour's opacity, then
// the text in the text colour; the cells [first, end) of each `underlined` range (a flagged
// component's text) underlined in the warning colour on the row below the glyphs (the box's last
// row at most). Nothing (`under`) when the box is wider than the block.
[[nodiscard]] std::array<float, 3> labelPixel(
    const dvv::gpu::GlyphStrip& strip, const rawreel::ValueMetrics& metrics, int x0, int x1,
    int bottom, std::span<const std::uint32_t> cells, LabelColours colours,
    std::array<float, 3> under, int px, int py,
    std::span<const std::array<std::size_t, 2>> underlined = {});

// The padding check (shaders/padding_check.slang and the marks of picture_inspect.slang),
// mirrored. A sample is flagged when its own stored word in `bytes` has bits in the word's
// paddingMask.
[[nodiscard]] bool paddingFlagged(dvv::media::FormatId format, dvv::platform::PixelSize image,
                                  std::span<const std::byte> bytes, std::size_t component,
                                  dvv::media::SamplePosition sample);

// The flagged samples of every component (ViewReadback::paddingSamples).
[[nodiscard]] std::uint64_t paddingCount(dvv::media::FormatId format,
                                         dvv::platform::PixelSize image,
                                         std::span<const std::byte> bytes);

struct PaddingTiles {  // tilesX x tilesY tiles of tileSize P pixels, row-major
    std::uint32_t tileSize = 0, tilesX = 0, tilesY = 0;
    std::vector<std::uint32_t> centres;  // the least cy << 16 | cx per tile, 0xFFFFFFFF: none
};

// The tile map of P (`picture`, shown through `view`): every flagged sample's block centre,
// halfway between its first P pixel and the next block's per axis, in the tile it lies in, when
// it is in the image span.
[[nodiscard]] PaddingTiles paddingTiles(dvv::media::FormatId format, dvv::platform::PixelSize image,
                                        std::span<const std::byte> bytes,
                                        const dvv::media::ViewTransform& view,
                                        dvv::platform::PixelSize picture, std::uint32_t tile_size);

// The mark at P pixel (px, py) from the 3 x 3 tiles around it: 0 none, 1 the disc (mark_size
// across), 2 its mark_rim dark rim, 3 the 1-pixel ring, by the nearest centre's 4 d^2.
[[nodiscard]] std::uint32_t paddingMarkAt(const PaddingTiles& tiles, std::uint32_t mark_size,
                                          std::uint32_t mark_rim, int px, int py);

}  // namespace rawreel::testing
