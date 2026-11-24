#include "composite_reference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "dvv/media/display.h"
#include "dvv/media/view.h"
#include "dvv/media/view_lines.h"
#include "render/composite_constants.h"
#include "render/display_state.h"
#include "render/pass_regions.h"

using dvv::media::AxisView;
using dvv::media::LineHit;
using dvv::media::LineLevel;
using dvv::media::PixelSpan;
using dvv::media::resolveView;

namespace rawreel {

namespace {

using Rgb = std::array<float, 3>;

float mix(float a, float b, float t) {
    return a + t * (b - a);  // the shader's mix3, per channel
}

Rgb texel(const HostDisplayImage& image, int x, int y) {
    const int cx = std::clamp(x, 0, static_cast<int>(image.width) - 1);
    const int cy = std::clamp(y, 0, static_cast<int>(image.height) - 1);
    const std::size_t base =
        (static_cast<std::size_t>(cy) * image.width + static_cast<std::size_t>(cx)) * 4;
    return {dvv::media::halfToFloat(image.rgba16f[base]),
            dvv::media::halfToFloat(image.rgba16f[base + 1]),
            dvv::media::halfToFloat(image.rgba16f[base + 2])};
}

Rgb bilinear(const HostDisplayImage& image, float u, float v) {
    const float x = u - 0.5F;
    const float y = v - 0.5F;
    const float x0 = std::floor(x);
    const float y0 = std::floor(y);
    const float fx = x - x0;
    const float fy = y - y0;
    const int ix = static_cast<int>(x0);
    const int iy = static_cast<int>(y0);
    const Rgb t00 = texel(image, ix, iy);
    const Rgb t10 = texel(image, ix + 1, iy);
    const Rgb t01 = texel(image, ix, iy + 1);
    const Rgb t11 = texel(image, ix + 1, iy + 1);
    Rgb out{};
    for (std::size_t c = 0; c < 3; ++c) {
        const float top = mix(t00[c], t10[c], fx);
        const float bottom = mix(t01[c], t11[c], fx);
        out[c] = mix(top, bottom, fy);
    }
    return out;
}

Rgb sample(const CompositeConstants& k, const HostDisplayImage& image, float u, float v) {
    if (k.zoom >= 1.0F) {
        return texel(image, static_cast<int>(std::floor(u)), static_cast<int>(std::floor(v)));
    }
    std::uint32_t n = 1;  // min(COMPOSITE_MAX_TAPS, ceil(0.5 / zoom)), as the shader
    while (n < COMPOSITE_MAX_TAPS && static_cast<float>(n) * k.zoom < 0.5F) {
        ++n;
    }
    const float nf = static_cast<float>(n);
    Rgb sum{0.0F, 0.0F, 0.0F};
    for (std::uint32_t b = 0; b < n; ++b) {
        const float vb = v + ((static_cast<float>(b) + 0.5F) / nf - 0.5F) / k.zoom;
        for (std::uint32_t a = 0; a < n; ++a) {
            const float ua = u + ((static_cast<float>(a) + 0.5F) / nf - 0.5F) / k.zoom;
            const Rgb tap = bilinear(image, ua, vb);
            for (std::size_t c = 0; c < 3; ++c) {
                sum[c] = sum[c] + tap[c];
            }
        }
    }
    const float count = static_cast<float>(n * n);
    return {sum[0] / count, sum[1] / count, sum[2] / count};
}

std::uint8_t unorm8(float x) {
    return static_cast<std::uint8_t>(std::lround(255.0F * std::clamp(x, 0.0F, 1.0F)));
}

Rgb encode3(const Rgb& c) {
    return {dvv::media::srgbEncode(c[0]), dvv::media::srgbEncode(c[1]),
            dvv::media::srgbEncode(c[2])};
}

Rgb decode3(const Rgb& c) {
    return {dvv::media::srgbDecode(c[0]), dvv::media::srgbDecode(c[1]),
            dvv::media::srgbDecode(c[2])};
}

Rgb mix3(const Rgb& a, const Rgb& b, float t) {
    return {mix(a[0], b[0], t), mix(a[1], b[1], t), mix(a[2], b[2], t)};
}

float unpackByte(std::uint32_t packed, std::uint32_t shift) {
    return static_cast<float>((packed >> shift) & 0xFFU) * (1.0F / 255.0F);
}

LineLevel levelOf(std::uint32_t shape) {
    return LineLevel{.stepLog2 = shape & LEVEL_STEP_MASK,
                     .thickness = (shape >> LEVEL_THICKNESS_SHIFT) & LEVEL_THICKNESS_MAX,
                     .drawn = (shape & LEVEL_DRAWN) != 0,
                     .halo = (shape & LEVEL_HALO) != 0};
}

bool inSpan(PixelSpan span, std::int32_t pixel) {
    return pixel >= span.begin && pixel < span.end;
}

// The shader's winner(): the smaller level; at the same level a line beats a halo; else x.
std::optional<LineHit> winner(const std::optional<LineHit>& x, const std::optional<LineHit>& y) {
    if (!y) {
        return x;
    }
    if (!x || y->level < x->level || (y->level == x->level && x->halo && !y->halo)) {
        return y;
    }
    return x;
}

Rgb blendLine(const CompositeConstants& k, const Rgb& encoded, const LineHit& hit) {
    const std::uint32_t colour = k.levelColour[hit.level];
    const float alpha = unpackByte(colour, 24);
    if (hit.halo) {
        const float strength = GRID_HALO_STRENGTH * alpha;
        return mix3(encoded, Rgb{0.0F, 0.0F, 0.0F}, strength);
    }
    Rgb line{unpackByte(colour, 0), unpackByte(colour, 8), unpackByte(colour, 16)};
    if ((k.levelShape[hit.level] & LEVEL_ADAPTIVE) != 0) {
        const float luma = (0.2126F * encoded[0]) + (0.7152F * encoded[1]) + (0.0722F * encoded[2]);
        const float shade = luma > 0.5F ? 0.0F : 1.0F;
        line = Rgb{shade, shade, shade};
    }
    return mix3(encoded, line, alpha);
}

}  // namespace

std::array<std::uint8_t, 4> compositePixel(const CompositeConstants& k, std::uint32_t px,
                                           std::uint32_t py,
                                           const std::optional<HostDisplayImage>& image) noexcept {
    const AxisView ax{.offset = k.offsetX, .zoom = k.zoom, .size = k.imageW, .extent = k.viewportW};
    const AxisView ay{.offset = k.offsetY, .zoom = k.zoom, .size = k.imageH, .extent = k.viewportH};
    // Thread (px, py) draws P pixel (px + drawX, py + drawY).
    const auto x = static_cast<std::int32_t>(px + k.drawX);
    const auto y = static_cast<std::int32_t>(py + k.drawY);
    const bool inside = image.has_value() && inSpan(dvv::media::imageSpan(ax), x) &&
                        inSpan(dvv::media::imageSpan(ay), y);
    const float grey = dvv::media::srgbDecode((k.flags & COMPOSITE_DARK) != 0 ? BACKGROUND_DARK
                                                                              : BACKGROUND_LIGHT);
    Rgb colour{grey, grey, grey};
    if (inside) {
        colour = sample(k, *image, dvv::media::imageCoordinateAt(ax, x),
                        dvv::media::imageCoordinateAt(ay, y));
    }
    const bool skip = (k.flags & COMPOSITE_SKIP_ENCODE) != 0;
    const Rgb encoded = encode3(colour);
    Rgb result = skip ? colour : encoded;
    const std::uint32_t drawn = (k.levelShape[0] | k.levelShape[1] | k.levelShape[2]) & LEVEL_DRAWN;
    if (inside && drawn != 0) {
        std::array<LineLevel, dvv::media::LINE_LEVELS> levels{};
        for (std::size_t l = 0; l < dvv::media::LINE_LEVELS; ++l) {
            levels[l] = levelOf(k.levelShape[l]);
        }
        const std::optional<LineHit> hit =
            winner(dvv::media::lineAt(ax, levels, x), dvv::media::lineAt(ay, levels, y));
        if (hit) {
            const Rgb blended = blendLine(k, encoded, *hit);
            result = skip ? decode3(blended) : blended;
        }
    }
    return {unorm8(result[0]), unorm8(result[1]), unorm8(result[2]), 255};
}

std::array<std::uint8_t, 4> compositeReference(std::uint32_t px, std::uint32_t py,
                                               dvv::platform::PixelSize viewport,
                                               const DisplayParams& params,
                                               std::optional<HostDisplayImage> image) {
    const dvv::platform::PixelSize size =
        image ? dvv::platform::PixelSize{image->width, image->height} : dvv::platform::PixelSize{};
    const dvv::media::ViewTransform view = resolveView(params.view, viewport, size);
    return compositePixel(
        compositeConstants(params.overlay, view, passRegions(viewport, {}, view), false), px, py,
        image);
}

}  // namespace rawreel
