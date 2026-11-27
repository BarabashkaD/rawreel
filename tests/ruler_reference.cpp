#include "ruler_reference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "composite_reference.h"
#include "core/overlay.h"
#include "core/ruler_labels.h"
#include "dvv/gpu/glyph_strip.h"
#include "dvv/media/display.h"
#include "dvv/media/view_lines.h"
#include "render/picture_constants.h"
#include "render/ruler_constants.h"

namespace rawreel {

namespace {

using Rgb = std::array<float, 3>;

Rgb rgb(std::uint32_t packed) {
    return {static_cast<float>((packed >> 16U) & 0xFFU) / 255.0F,
            static_cast<float>((packed >> 8U) & 0xFFU) / 255.0F,
            static_cast<float>(packed & 0xFFU) / 255.0F};
}

std::uint8_t unorm8(float x) {
    return static_cast<std::uint8_t>(std::lround(255.0F * std::clamp(x, 0.0F, 1.0F)));
}

// Draws the text of `label` at pixel `along_pixel`, row `across` of the label band whose text
// rows begin at `text_top`, in `text` over `colour`.
void drawText(Rgb& colour, const RulerLabel& label, std::uint32_t along_pixel, std::uint32_t across,
              std::int32_t text_top, bool top, const dvv::gpu::GlyphStrip& glyphs,
              const Rgb& text) {
    const auto advance = static_cast<std::int32_t>(glyphs.advance);
    const std::uint32_t digits = decimalDigits(label.value);
    const auto length = static_cast<std::int32_t>(digits) * advance;
    const auto p = static_cast<std::int32_t>(along_pixel);
    if (p < label.textBegin || p >= label.textBegin + length) {
        return;
    }
    // The left ruler reads bottom to top: its first column is the text's last pixel.
    const std::int32_t offset = top ? p - label.textBegin : label.textBegin + length - 1 - p;
    const auto cell = static_cast<std::uint32_t>(offset / advance);
    std::uint32_t power = 1;
    for (std::uint32_t i = cell + 1; i < digits; ++i) {
        power *= 10;
    }
    const std::uint32_t digit = (label.value / power) % 10;
    const std::int32_t gy = static_cast<std::int32_t>(across) - text_top;
    if (gy < 0 || gy >= static_cast<std::int32_t>(glyphs.height)) {
        return;
    }
    const std::uint32_t gx =
        (digit * glyphs.advance) + static_cast<std::uint32_t>(offset % advance);
    const float coverage =
        static_cast<float>(glyphs.coverage[(static_cast<std::size_t>(gy) * glyphs.width) + gx]) /
        255.0F;
    for (std::size_t c = 0; c < colour.size(); ++c) {
        colour[c] = colour[c] + (coverage * (text[c] - colour[c]));
    }
}

// The labels over `colour` at a label-band pixel of the ruler, in drawing order.
void drawLabels(Rgb& colour, const RulerConstants& k, const dvv::gpu::GlyphStrip& glyphs,
                std::uint32_t along_pixel, std::uint32_t across) {
    const bool top = k.axis == 0;
    const bool dark = (k.flags & RULER_DARK) != 0;
    const std::uint32_t band_start = k.rulerEdge - k.strip;
    const std::uint32_t inset = (k.labelSpacing >> 16U) & 0xFFU;
    const std::int32_t text_top =
        static_cast<std::int32_t>(band_start) +
        ((static_cast<std::int32_t>(labelBand(k)) - static_cast<std::int32_t>(glyphs.height)) / 2);
    const bool filled = across >= band_start + inset && across + inset < band_start + labelBand(k);
    const auto p = static_cast<std::int32_t>(along_pixel);
    for (const RulerLabel& label : rulerLabels(rulerLabelInput(k, glyphs.advance))) {
        if (p < label.boxBegin || p >= label.boxEnd) {
            continue;
        }
        Rgb text = rgb(dark ? RULER_TEXT_DARK : RULER_TEXT_LIGHT);
        if (label.kind == RulerLabelKind::CursorTag) {
            if (filled) {
                colour = rgb(k.cursorColour);
            }
            text = rgb((k.flags & RULER_TAG_TEXT_BLACK) != 0 ? RULER_CURSOR_TAG_BLACK
                                                             : RULER_CURSOR_TAG_WHITE);
        } else if (label.kind != RulerLabelKind::Number) {
            if (filled) {
                colour = rgb(dark ? RULER_TAG_FILL_DARK : RULER_TAG_FILL_LIGHT);
            }
            text = rgb(dark ? RULER_TAG_TEXT_DARK : RULER_TAG_TEXT_LIGHT);
        }
        drawText(colour, label, along_pixel, across, text_top, top, glyphs, text);
    }
}

}  // namespace

// The label input of a band, as the shader reads it from the constants.
RulerLabelInput rulerLabelInput(const RulerConstants& k, std::uint32_t advance) {
    return {.axis = {.offset = k.offset,
                     .zoom = k.zoom,
                     .size = k.imageSize,
                     .extent = k.pictureExtent},
            .pictureStart = static_cast<std::int32_t>(k.pictureStart),
            .rulerBegin = static_cast<std::int32_t>(k.rulerBegin),
            .rulerEnd = static_cast<std::int32_t>(k.rulerEnd),
            .advance = advance,
            .spacing = {.numberGap = k.labelSpacing & 0xFFU,
                        .tagPad = (k.labelSpacing >> 8U) & 0xFFU,
                        .tagInset = (k.labelSpacing >> 16U) & 0xFFU,
                        .tagMargin = k.labelSpacing >> 24U},
            .majorStepLog2 = k.levelShape[0] & LEVEL_STEP_MASK,
            .numbers = (k.flags & RULER_NUMBERS) != 0,
            .ticksShown = (k.flags & RULER_TICKS) != 0,
            .cursor = (k.flags & RULER_CURSOR) != 0 ? std::optional<std::uint32_t>(k.cursor)
                                                    : std::nullopt,
            .lastValue = (k.flags & RULER_EDGE) != 0 ? LastValue::Edge : LastValue::Pixel};
}

std::array<std::uint8_t, 4> rulerPixel(const RulerConstants& k, const dvv::gpu::GlyphStrip* glyphs,
                                       std::uint32_t x, std::uint32_t y) {
    const bool top = k.axis == 0;
    const std::uint32_t across = top ? y : x;
    const std::uint32_t along_pixel = top ? x : y;
    const std::int32_t along =
        static_cast<std::int32_t>(along_pixel) - static_cast<std::int32_t>(k.pictureStart);
    const bool dark = (k.flags & RULER_DARK) != 0;
    const bool rows = across >= k.rulerEdge - k.strip && across < k.rulerEdge;
    const bool ruler = rows && along_pixel >= k.rulerBegin && along_pixel < k.rulerEnd;
    const bool corner = top && rows && along_pixel >= k.cornerBegin && along_pixel < k.rulerBegin;
    const std::uint32_t depth = k.rulerEdge - 1 - across;
    const float grey = dark ? BACKGROUND_DARK : BACKGROUND_LIGHT;
    Rgb colour{grey, grey, grey};
    if (corner) {
        colour = rgb(dark ? RULER_CORNER_DARK : RULER_CORNER_LIGHT);
    } else if (ruler && depth == 0) {
        colour = rgb(dark ? RULER_SEPARATOR_DARK : RULER_SEPARATOR_LIGHT);
    } else if (ruler) {
        colour = rgb(dark ? RULER_BACKGROUND_DARK : RULER_BACKGROUND_LIGHT);
    }
    if (ruler && (k.flags & RULER_IMAGE) != 0) {
        const dvv::media::AxisView axis{
            .offset = k.offset, .zoom = k.zoom, .size = k.imageSize, .extent = k.pictureExtent};
        if ((k.flags & RULER_TICKS) != 0 && depth < k.tickBand) {
            std::array<dvv::media::LineLevel, dvv::media::LINE_LEVELS> levels{};
            for (std::size_t l = 0; l < levels.size(); ++l) {
                levels[l] = {.stepLog2 = k.levelShape[l] & LEVEL_STEP_MASK,
                             .thickness = k.tickWidth,
                             .drawn = (k.levelShape[l] & LEVEL_DRAWN) != 0,
                             .halo = false};
            }
            const std::optional<dvv::media::LineHit> hit = dvv::media::lineAt(axis, levels, along);
            if (hit && depth < k.tickLength[hit->level]) {
                colour = hit->level == 0 ? rgb(dark ? RULER_MAJOR_DARK : RULER_MAJOR_LIGHT)
                                         : rgb(dark ? RULER_MINOR_DARK : RULER_MINOR_LIGHT);
            }
        }
        if ((k.flags & RULER_LIT) != 0 && depth < k.litDepth) {
            const dvv::media::PixelSpan lit =
                dvv::media::lineSpan(axis, k.litTick, 3 * k.tickWidth);
            if (along >= lit.begin && along < lit.end) {
                colour = rgb(k.cursorColour);
            }
        }
        if ((k.flags & RULER_CURSOR) != 0 && depth < k.markerDepth) {
            const dvv::media::PixelSpan span = dvv::media::cursorSpan(axis, k.cursor);
            if (along >= span.begin && along < span.end) {
                const Rgb marker = rgb(k.cursorColour);
                for (std::size_t c = 0; c < colour.size(); ++c) {
                    colour[c] = colour[c] + (RULER_MARKER_OPACITY * (marker[c] - colour[c]));
                }
            }
        }
        // The label band: numbers, end tags and the cursor tag (RULER_NUMBERS needs RULER_IMAGE).
        if (glyphs != nullptr && (k.flags & RULER_NUMBERS) != 0 && depth >= k.tickBand) {
            drawLabels(colour, k, *glyphs, along_pixel, across);
        }
    }
    if ((k.flags & RULER_SKIP_ENCODE) != 0) {
        for (float& c : colour) {
            c = dvv::media::srgbDecode(c);
        }
    }
    return {unorm8(colour[0]), unorm8(colour[1]), unorm8(colour[2]), 255};
}

std::uint32_t labelCoverage(std::uint32_t cell, std::uint32_t column, std::uint32_t row) {
    return ((cell * 37) + (column * 11) + (row * 5)) & 0xFFU;
}

std::shared_ptr<const dvv::gpu::GlyphStrip> labelStrip(std::uint32_t advance,
                                                       std::uint32_t height) {
    auto strip = std::make_shared<dvv::gpu::GlyphStrip>();
    strip->width = 10 * advance;
    strip->height = height;
    strip->advance = advance;
    strip->ascent = height;
    strip->coverage.resize(std::size_t{strip->width} * height);
    for (std::uint32_t row = 0; row < height; ++row) {
        for (std::uint32_t x = 0; x < strip->width; ++x) {
            strip->coverage[(std::size_t{row} * strip->width) + x] =
                static_cast<std::uint8_t>(labelCoverage(x / advance, x % advance, row));
        }
    }
    return strip;
}

}  // namespace rawreel
