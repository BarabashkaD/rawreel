// rawreel's picture pass. One thread per pixel of the part of P it draws, from P pixel (drawX,
// drawY) (the part right of and below the hugged rect's top-left): the display image (RGBA16F,
// linear light, BT.709 primaries) shown with the background around it (with FLAG_ALPHA: the
// picture premultiplied per texel and blended over a checkerboard), the grid drawn over it, then
// the sRGB encode into the RGBA8 output, which the presenter blits 1:1. Viewport pixel p shows the
// image when it lies in dvv-media's imageSpan on both axes and samples at its pixel centre,
// imageCoordinateAt; the grid lines come from lineAt, run once per column and per row by the grid
// prepass (grid_lines.slang), so the picture and the lines share one rule. compositePixel() in
// tests/composite_reference.cpp computes the same, in the same order.

import composite_common;
import dvv_media.view_lines;
import viewport_background;

using namespace dvv::media;

static const uint FLAG_DARK = 1;
static const uint FLAG_SKIP_ENCODE = 2;
static const uint FLAG_ALPHA = 4;
static const uint MAX_TAPS = 4;

// The halo darkens the pixel by this share of the level's opacity.
static const float HALO_STRENGTH = 0.6;

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

// Set 0, pushed per dispatch (M2 push descriptors). Bound images are writable for dvv-gpu; this
// shader only reads bindings 0 and 2. `lines` holds the grid prepass's hits (packHit), written
// only when a grid level is drawn.
[[vk::binding(0, 0)]] [format("rgba16f")] RWTexture2D<float4> display;
[[vk::binding(1, 0)]] [format("rgba8")] RWTexture2D<float4> output;
[[vk::binding(2, 0)]] [format("r32ui")] RWTexture2D<uint> lines;

// IEC 61966-2-1, as dvv::media::srgbEncode/srgbDecode (input clamped).
float srgbEncode(float x)
{
    x = clamp(x, 0.0, 1.0);
    return x <= 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

float srgbDecode(float x)
{
    x = clamp(x, 0.0, 1.0);
    return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}

float3 texel(int x, int y)
{
    uint2 p = uint2(clamp(x, 0, int(pc.imageW) - 1), clamp(y, 0, int(pc.imageH) - 1));
    return display[p].rgb;
}

float3 mix3(float3 a, float3 b, float t)
{
    precise const float3 mixed = a + t * (b - a);
    return mixed;
}
// The 4-texel bilinear around (u - 0.5, v - 0.5), edges clamped.
float3 bilinear(float u, float v)
{
    float x = u - 0.5;
    float y = v - 0.5;
    float x0 = floor(x);
    float y0 = floor(y);
    float fx = x - x0;
    float fy = y - y0;
    int ix = int(x0);
    int iy = int(y0);
    float3 top = mix3(texel(ix, iy), texel(ix + 1, iy), fx);
    float3 bottom = mix3(texel(ix, iy + 1), texel(ix + 1, iy + 1), fx);
    return mix3(top, bottom, fy);
}

float3 sampleDisplay(float u, float v)
{
    if (pc.zoom >= 1.0)
    {
        return texel(int(floor(u)), int(floor(v)));
    }
    // n = min(MAX_TAPS, ceil(0.5 / zoom)), without a division (exact at zoom 0.5, 0.25, ...).
    uint n = 1;
    while (n < MAX_TAPS && float(n) * pc.zoom < 0.5)
    {
        n = n + 1;
    }
    float nf = float(n);
    float3 sum = float3(0.0, 0.0, 0.0);
    for (uint b = 0; b < n; b = b + 1)
    {
        float vb = v + ((float(b) + 0.5) / nf - 0.5) / pc.zoom;
        for (uint a = 0; a < n; a = a + 1)
        {
            float ua = u + ((float(a) + 0.5) / nf - 0.5) / pc.zoom;
            sum = sum + bilinear(ua, vb);
        }
    }
    return sum / float(n * n);
}

// The alpha pass's texel: premultiplied by its alpha clamped to [0, 1], edges clamped.
float4 texel4(int x, int y)
{
    uint2 p = uint2(clamp(x, 0, int(pc.imageW) - 1), clamp(y, 0, int(pc.imageH) - 1));
    const float4 t = display[p];
    const float a = clamp(t.a, 0.0, 1.0);
    precise const float3 rgb = t.rgb * a;
    return float4(rgb, a);
}

float4 mix4(float4 a, float4 b, float t)
{
    precise const float4 mixed = a + t * (b - a);
    return mixed;
}

// bilinear over texel4.
float4 bilinear4(float u, float v)
{
    float x = u - 0.5;
    float y = v - 0.5;
    float x0 = floor(x);
    float y0 = floor(y);
    float fx = x - x0;
    float fy = y - y0;
    int ix = int(x0);
    int iy = int(y0);
    float4 top = mix4(texel4(ix, iy), texel4(ix + 1, iy), fx);
    float4 bottom = mix4(texel4(ix, iy + 1), texel4(ix + 1, iy + 1), fx);
    return mix4(top, bottom, fy);
}

// sampleDisplay over texel4: the same taps and weights.
float4 sampleDisplay4(float u, float v)
{
    if (pc.zoom >= 1.0)
    {
        return texel4(int(floor(u)), int(floor(v)));
    }
    uint n = 1;
    while (n < MAX_TAPS && float(n) * pc.zoom < 0.5)
    {
        n = n + 1;
    }
    float nf = float(n);
    float4 sum = float4(0.0, 0.0, 0.0, 0.0);
    for (uint b = 0; b < n; b = b + 1)
    {
        float vb = v + ((float(b) + 0.5) / nf - 0.5) / pc.zoom;
        for (uint a = 0; a < n; a = a + 1)
        {
            float ua = u + ((float(a) + 0.5) / nf - 0.5) / pc.zoom;
            sum = sum + bilinear4(ua, vb);
        }
    }
    return sum / float(n * n);
}

// The checkerboard's linear grey at P pixel (px, py), inside the image span (px - offsetX >= 0):
// square (0, 0) at the image origin is even.
float checker(int px, int py, bool dark)
{
    const uint sx = uint(px - pc.offsetX) / pc.checkerSize;
    const uint sy = uint(py - pc.offsetY) / pc.checkerSize;
    const bool odd = ((sx + sy) & 1) != 0;
    return srgbDecode(checkerGrey(dark, odd));
}

float3 encode3(float3 c)
{
    return float3(srgbEncode(c.r), srgbEncode(c.g), srgbEncode(c.b));
}

float3 decode3(float3 c)
{
    return float3(srgbDecode(c.r), srgbDecode(c.g), srgbDecode(c.b));
}

// A byte of a packed colour as a value in [0, 1].
float unpackByte(uint packed, uint shift)
{
    return float((packed >> shift) & 0xFF) * (1.0 / 255.0);
}

bool inSpan(PixelSpan span, int pixel)
{
    return pixel >= span.begin && pixel < span.end;
}

// The hit drawn where both axes hit: the smaller level; at the same level a line beats a halo (a
// horizontal line is not cut by the halo of a vertical one); else x.
LineHit winner(LineHit x, LineHit y)
{
    if (!y.hit)
    {
        return x;
    }
    if (!x.hit || y.level < x.level || (y.level == x.level && x.halo && !y.halo))
    {
        return y;
    }
    return x;
}

// The encoded pixel `encoded` with the line of `hit` over it, in encoded space.
float3 blendLine(float3 encoded, LineHit hit)
{
    const uint colour = pc.levelColour[hit.level];
    const float alpha = unpackByte(colour, 24);
    if (hit.halo)
    {
        const float strength = HALO_STRENGTH * alpha;
        return mix3(encoded, float3(0.0, 0.0, 0.0), strength);
    }
    float3 line = float3(unpackByte(colour, 0), unpackByte(colour, 8), unpackByte(colour, 16));
    if ((pc.levelShape[hit.level] & LEVEL_ADAPTIVE) != 0)
    {
        precise const float luma = 0.2126 * encoded.r + 0.7152 * encoded.g + 0.0722 * encoded.b;
        const float shade = luma > 0.5 ? 0.0 : 1.0;
        line = float3(shade, shade, shade);
    }
    return mix3(encoded, line, alpha);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    // Thread id draws P pixel id + draw: the bounds test, the line table and the write use it.
    const uint2 p = uint2(id.x + pc.drawX, id.y + pc.drawY);
    if (p.x >= pc.viewportW || p.y >= pc.viewportH)
    {
        return;
    }
    const AxisView ax = axisOf(pc.offsetX, pc.zoom, pc.imageW, pc.viewportW);
    const AxisView ay = axisOf(pc.offsetY, pc.zoom, pc.imageH, pc.viewportH);
    const int px = int(p.x);
    const int py = int(p.y);
    const bool inside = inSpan(imageSpan(ax), px) && inSpan(imageSpan(ay), py);
    const bool dark = (pc.flags & FLAG_DARK) != 0;
    float grey = srgbDecode(viewportBackground(dark));
    float3 colour = float3(grey, grey, grey);
    if (inside)
    {
        const float u = imageCoordinateAt(ax, px);
        const float v = imageCoordinateAt(ay, py);
        if ((pc.flags & FLAG_ALPHA) != 0)
        {
            // The premultiplied picture over the checkerboard, in linear light.
            const float4 s = sampleDisplay4(u, v);
            precise const float under = checker(px, py, dark) * (1.0 - s.a);
            precise const float3 blended = s.rgb + float3(under, under, under);
            colour = blended;
        }
        else
        {
            colour = sampleDisplay(u, v);
        }
    }
    const bool skip = (pc.flags & FLAG_SKIP_ENCODE) != 0;
    float3 result = skip ? colour : encode3(colour);
    if (inside && gridDrawn(pc))
    {
        // The prepass's lineAt of this column and of this row.
        const LineHit hit =
            winner(unpackHit(lines[uint2(p.x, 0)]), unpackHit(lines[uint2(p.y, 1)]));
        if (hit.hit)
        {
            const float3 blended = blendLine(skip ? encode3(colour) : result, hit);
            result = skip ? decode3(blended) : blended;
        }
    }
    output[uint2(pc.regionX + p.x, pc.regionY + p.y)] = float4(result, 1.0);
}
