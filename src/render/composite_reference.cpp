#include "render/composite_reference.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "core/view.h"
#include "dvv/media/display.h"

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

}  // namespace

CompositeConstants compositeConstants(const CompositeParams& params,
                                      dvv::platform::PixelSize viewport,
                                      dvv::platform::PixelSize image, bool skip_encode) {
    const ViewTransform view = ViewTransform::fit(viewport, image);
    const bool drawn = view.zoom() > 0.0;
    return CompositeConstants{
        .viewportW = viewport.width,
        .viewportH = viewport.height,
        .imageW = drawn ? image.width : 0,
        .imageH = drawn ? image.height : 0,
        .zoom = static_cast<float>(view.zoom()),
        .originX = static_cast<float>(view.origin().x),
        .originY = static_cast<float>(view.origin().y),
        .flags = (params.dark ? COMPOSITE_DARK : 0U) | (skip_encode ? COMPOSITE_SKIP_ENCODE : 0U)};
}

std::array<std::uint8_t, 4> compositePixel(const CompositeConstants& k, std::uint32_t px,
                                           std::uint32_t py,
                                           const std::optional<HostDisplayImage>& image) noexcept {
    const float grey = dvv::media::srgbDecode((k.flags & COMPOSITE_DARK) != 0 ? BACKGROUND_DARK
                                                                              : BACKGROUND_LIGHT);
    Rgb colour{grey, grey, grey};
    if (image && k.imageW != 0 && k.imageH != 0 && k.zoom > 0.0F) {
        const float u = k.originX + (static_cast<float>(px) + 0.5F) / k.zoom;
        const float v = k.originY + (static_cast<float>(py) + 0.5F) / k.zoom;
        if (u >= 0.0F && v >= 0.0F && u < static_cast<float>(k.imageW) &&
            v < static_cast<float>(k.imageH)) {
            colour = sample(k, *image, u, v);
        }
    }
    const bool skip = (k.flags & COMPOSITE_SKIP_ENCODE) != 0;
    return {skip ? unorm8(colour[0]) : dvv::media::encodeSrgb8(colour[0]),
            skip ? unorm8(colour[1]) : dvv::media::encodeSrgb8(colour[1]),
            skip ? unorm8(colour[2]) : dvv::media::encodeSrgb8(colour[2]), 255};
}

std::array<std::uint8_t, 4> compositeReference(std::uint32_t px, std::uint32_t py,
                                               dvv::platform::PixelSize viewport,
                                               const CompositeParams& params,
                                               std::optional<HostDisplayImage> image) {
    const dvv::platform::PixelSize size =
        image ? dvv::platform::PixelSize{image->width, image->height} : dvv::platform::PixelSize{};
    return compositePixel(compositeConstants(params, viewport, size, false), px, py, image);
}

}  // namespace rawreel
