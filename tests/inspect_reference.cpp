#include "inspect_reference.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/inspector.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/media/pixel_format.h"
#include "dvv/media/reference.h"
#include "dvv/media/sample_grid.h"
#include "dvv/media/types.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "dvv/platform/pixel_size.h"

namespace rawreel::testing {

float planeGrey(std::uint32_t code, std::uint32_t bits) noexcept {
    const std::uint32_t maximum = (1U << bits) - 1U;
    return static_cast<float>(code) / static_cast<float>(maximum);
}

dvv::media::SamplePosition sampleAt(dvv::media::FormatId format, std::size_t component,
                                    dvv::media::ImagePixel pixel) {
    return dvv::media::sampleOf(dvv::media::formatDesc(format).components.at(component), pixel);
}

dvv::media::PixelBlock cellOf(dvv::media::FormatId format, std::size_t component,
                              dvv::platform::PixelSize image, dvv::media::ImagePixel pixel) {
    const dvv::media::ComponentDesc& desc = dvv::media::formatDesc(format).components.at(component);
    return dvv::media::blockOf(desc, dvv::media::sampleOf(desc, pixel), image);
}

std::uint32_t codeAt(const dvv::media::CodeValues& codes, std::size_t component,
                     dvv::media::ImagePixel pixel) {
    const dvv::media::SamplePosition sample = sampleAt(codes.format, component, pixel);
    const std::pair<std::uint32_t, std::uint32_t> size =
        dvv::media::componentSize(codes.format, component, codes.width, codes.height);
    const std::size_t index = (static_cast<std::size_t>(sample.y) * size.first) + sample.x;
    return codes.components.at(component).at(index);
}

std::vector<std::uint32_t> codeCells(std::uint32_t code) {
    std::vector<std::uint32_t> cells;
    for (const char digit : std::to_string(code)) {
        cells.push_back(static_cast<std::uint32_t>(digit - '0'));
    }
    return cells;
}

TextOrigin lineOrigin(const dvv::gpu::GlyphStrip& strip, const rawreel::ValueMetrics& metrics,
                      TextBox box, std::size_t lines, std::size_t line, std::size_t length) {
    const int advance = static_cast<int>(strip.width / VALUE_GLYPH_CELLS);
    const int line_height = static_cast<int>(metrics.lineHeight);
    const int top = box.y0 + ((box.y1 - box.y0 - (static_cast<int>(lines) * line_height)) / 2);
    const int width = static_cast<int>(length) * advance;
    return {.left = box.x0 + ((box.x1 - box.x0 - width) / 2),
            .top = top + (static_cast<int>(line) * line_height) +
                   ((line_height - static_cast<int>(strip.height)) / 2)};
}

float textCoverage(const dvv::gpu::GlyphStrip& strip, std::span<const std::uint32_t> cells,
                   TextOrigin origin, int px, int py) {
    const int advance = static_cast<int>(strip.width / VALUE_GLYPH_CELLS);
    const int gx = px - origin.left;
    const int gy = py - origin.top;
    if (advance == 0 || gx < 0 || gy < 0 || gy >= static_cast<int>(strip.height)) {
        return 0.0F;
    }
    const auto at = static_cast<std::size_t>(gx / advance);
    if (at >= cells.size() || cells[at] == TEXT_SPACE || cells[at] >= VALUE_GLYPH_CELLS) {
        return 0.0F;
    }
    const std::size_t x = (std::size_t{cells[at]} * static_cast<std::size_t>(advance)) +
                          static_cast<std::size_t>(gx % advance);
    const std::uint8_t coverage =
        strip.coverage.at((static_cast<std::size_t>(gy) * strip.width) + x);
    return static_cast<float>(coverage) / 255.0F;
}

float numberText(std::array<float, 3> cell) noexcept {
    const float luma = (0.2126F * cell[0]) + (0.7152F * cell[1]) + (0.0722F * cell[2]);
    return luma > 0.5F ? 0.0F : 1.0F;
}

std::array<float, 3> linesPixel(const dvv::gpu::GlyphStrip& strip,
                                const rawreel::ValueMetrics& metrics, TextBox box,
                                std::span<const std::vector<std::uint32_t>> lines,
                                std::span<const std::array<float, 3>> texts,
                                std::array<float, 3> under, int px, int py) {
    const int line_height = static_cast<int>(metrics.lineHeight);
    const int count = static_cast<int>(lines.size());
    const int top = box.y0 + ((box.y1 - box.y0 - (count * line_height)) / 2);
    if (line_height <= 0 || count == 0 || py < top || py >= top + (count * line_height)) {
        return under;
    }
    const auto line = static_cast<std::size_t>((py - top) / line_height);
    const TextOrigin origin =
        lineOrigin(strip, metrics, box, lines.size(), line, lines[line].size());
    const float coverage = textCoverage(strip, lines[line], origin, px, py);
    std::array<float, 3> result{};
    for (std::size_t c = 0; c < result.size(); ++c) {
        result.at(c) = under.at(c) + (coverage * (texts[line].at(c) - under.at(c)));
    }
    return result;
}

std::array<float, 3> labelPixel(const dvv::gpu::GlyphStrip& strip,
                                const rawreel::ValueMetrics& metrics, int x0, int x1, int bottom,
                                std::span<const std::uint32_t> cells, LabelColours colours,
                                std::array<float, 3> under, int px, int py,
                                std::span<const std::array<std::size_t, 2>> underlined) {
    const int advance = static_cast<int>(strip.width / VALUE_GLYPH_CELLS);
    const int width = static_cast<int>(cells.size()) * advance;
    const int pad_x = static_cast<int>(metrics.padX);
    const int line_height = static_cast<int>(metrics.lineHeight);
    if (width + (2 * pad_x) > x1 - x0) {
        return under;
    }
    const int box_left = x0 + ((x1 - x0 - (width + (2 * pad_x))) / 2);
    const int box_bottom = bottom - static_cast<int>(metrics.padY);
    const int box_top = box_bottom - line_height;
    if (px < box_left || px >= box_left + width + (2 * pad_x) || py < box_top || py >= box_bottom) {
        return under;
    }
    const auto channel = [](std::uint32_t rgba, std::size_t c) {
        return static_cast<float>((rgba >> (8 * c)) & 0xFFU) / 255.0F;
    };
    const float opacity = channel(colours.box, 3);
    const int glyph_top = box_top + ((line_height - static_cast<int>(strip.height)) / 2);
    const float coverage =
        textCoverage(strip, cells, {.left = box_left + pad_x, .top = glyph_top}, px, py);
    std::array<float, 3> result{};
    for (std::size_t c = 0; c < result.size(); ++c) {
        const float boxed = under.at(c) + (opacity * (channel(colours.box, c) - under.at(c)));
        result.at(c) = boxed + (coverage * (channel(colours.text, c) - boxed));
    }
    const int underline = std::min(glyph_top + static_cast<int>(strip.height), box_bottom - 1);
    if (py == underline && px >= box_left + pad_x && advance > 0) {
        const auto at = static_cast<std::size_t>((px - box_left - pad_x) / advance);
        for (const std::array<std::size_t, 2>& range : underlined) {
            if (at >= range[0] && at < range[1]) {
                for (std::size_t c = 0; c < result.size(); ++c) {
                    result.at(c) = channel(colours.warning, c);
                }
            }
        }
    }
    return result;
}

bool paddingFlagged(dvv::media::FormatId format, dvv::platform::PixelSize image,
                    std::span<const std::byte> bytes, std::size_t component,
                    dvv::media::SamplePosition sample) {
    const auto word = dvv::media::storedWord(format, image.width, image.height, component, sample);
    return word.has_value() &&
           (dvv::media::readStoredWord(bytes, 0, *word) & word->paddingMask) != 0;
}

namespace {

// Calls `flagged(component, sample)` for every flagged sample of the first four components.
template <typename Visit>
void forFlagged(dvv::media::FormatId format, dvv::platform::PixelSize image,
                std::span<const std::byte> bytes, const Visit& flagged) {
    const dvv::media::PixelFormatDesc& desc = dvv::media::formatDesc(format);
    for (std::size_t k = 0; k < std::min<std::size_t>(desc.componentCount, 4); ++k) {
        const dvv::media::ComponentDesc& component = desc.components.at(k);
        const std::uint32_t samples_x =
            (image.width + (1U << component.log2SubX) - 1U) >> component.log2SubX;
        const std::uint32_t samples_y =
            (image.height + (1U << component.log2SubY) - 1U) >> component.log2SubY;
        for (std::uint32_t y = 0; y < samples_y; ++y) {
            for (std::uint32_t x = 0; x < samples_x; ++x) {
                if (paddingFlagged(format, image, bytes, k, {.x = x, .y = y})) {
                    flagged(component, dvv::media::SamplePosition{.x = x, .y = y});
                }
            }
        }
    }
}

}  // namespace

std::uint64_t paddingCount(dvv::media::FormatId format, dvv::platform::PixelSize image,
                           std::span<const std::byte> bytes) {
    std::uint64_t count = 0;
    forFlagged(format, image, bytes,
               [&count](const dvv::media::ComponentDesc& /*component*/,
                        dvv::media::SamplePosition /*sample*/) { ++count; });
    return count;
}

PaddingTiles paddingTiles(dvv::media::FormatId format, dvv::platform::PixelSize image,
                          std::span<const std::byte> bytes, const dvv::media::ViewTransform& view,
                          dvv::platform::PixelSize picture, std::uint32_t tile_size) {
    PaddingTiles tiles{.tileSize = tile_size,
                       .tilesX = (picture.width + tile_size - 1) / tile_size,
                       .tilesY = (picture.height + tile_size - 1) / tile_size,
                       .centres = {}};
    tiles.centres.assign(std::size_t{tiles.tilesX} * tiles.tilesY, 0xFFFFFFFFU);
    const dvv::media::AxisView ax = dvv::media::axisX(view);
    const dvv::media::AxisView ay = dvv::media::axisY(view);
    const dvv::media::PixelSpan xs = dvv::media::imageSpan(ax);
    const dvv::media::PixelSpan ys = dvv::media::imageSpan(ay);
    forFlagged(format, image, bytes,
               [&](const dvv::media::ComponentDesc& component, dvv::media::SamplePosition sample) {
                   const dvv::media::PixelBlock block =
                       dvv::media::blockOf(component, sample, image);
                   const std::int32_t cx = (dvv::media::firstPixelOf(ax, block.x) +
                                            dvv::media::firstPixelOf(ax, block.x + block.width)) /
                                           2;
                   const std::int32_t cy = (dvv::media::firstPixelOf(ay, block.y) +
                                            dvv::media::firstPixelOf(ay, block.y + block.height)) /
                                           2;
                   if (cx < xs.begin || cx >= xs.end || cy < ys.begin || cy >= ys.end) {
                       return;
                   }
                   const std::size_t tile =
                       (std::size_t{static_cast<std::uint32_t>(cy) / tile_size} * tiles.tilesX) +
                       (static_cast<std::uint32_t>(cx) / tile_size);
                   const std::uint32_t centre =
                       (static_cast<std::uint32_t>(cy) << 16U) | static_cast<std::uint32_t>(cx);
                   tiles.centres.at(tile) = std::min(tiles.centres.at(tile), centre);
               });
    return tiles;
}

std::uint32_t paddingMarkAt(const PaddingTiles& tiles, std::uint32_t mark_size,
                            std::uint32_t mark_rim, int px, int py) {
    const int tile_size = static_cast<int>(tiles.tileSize);
    const int tx = px / tile_size;
    const int ty = py / tile_size;
    int nearest = -1;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int x = tx + dx;
            const int y = ty + dy;
            if (x < 0 || y < 0 || x >= static_cast<int>(tiles.tilesX) ||
                y >= static_cast<int>(tiles.tilesY)) {
                continue;
            }
            const std::uint32_t centre = tiles.centres.at(
                (static_cast<std::size_t>(y) * tiles.tilesX) + static_cast<std::size_t>(x));
            if (centre == 0xFFFFFFFFU) {
                continue;
            }
            const int ex = px - static_cast<int>(centre & 0xFFFFU);
            const int ey = py - static_cast<int>(centre >> 16U);
            const int d2 = (ex * ex) + (ey * ey);
            nearest = nearest < 0 ? d2 : std::min(nearest, d2);
        }
    }
    if (nearest < 0) {
        return 0;
    }
    const int size = static_cast<int>(mark_size);
    const int rim = static_cast<int>(mark_rim);
    const int d4 = 4 * nearest;
    if (d4 <= size * size) {
        return 1;
    }
    if (d4 <= (size + (2 * rim)) * (size + (2 * rim))) {
        return 2;
    }
    return d4 <= (size + (2 * rim) + 2) * (size + (2 * rim) + 2) ? 3 : 0;
}

}  // namespace rawreel::testing
