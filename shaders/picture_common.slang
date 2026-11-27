// What rawreel's picture passes (picture.slang, picture_inspect.slang) and the grid prepass
// (picture_lines.slang) share: the push constants, the grid level words, the packed grid hit of one
// column or row, and the picture's sampling, background and grid blend over the push constants and
// the display image they are given.

module picture_common;

import dvv_media.view_lines;
import viewport_background;

using namespace dvv::media;

// levelShape, as render/picture_constants.h: stepLog2 in bits 0-3, the thickness in 4-11,
// adaptive 12, halo 13, drawn 14.
public static const uint LEVEL_STEP_MASK = 0xF;
public static const uint LEVEL_THICKNESS_SHIFT = 4;
public static const uint LEVEL_THICKNESS_MASK = 0xFF;
public static const uint LEVEL_ADAPTIVE = 1 << 12;
public static const uint LEVEL_HALO = 1 << 13;
public static const uint LEVEL_DRAWN = 1 << 14;

// = rawreel::PictureConstants, 96 bytes. The pads are scalar words: a uint3 would align to 16
// bytes and grow the block.
public struct PushConstants
{
    public uint viewportW;  // P's size
    public uint viewportH;
    public uint imageW;
    public uint imageH;
    public float zoom;
    public uint flags;
    public uint regionX;  // P's origin in the output
    public uint regionY;
    public int offsetX;   // the view's integer offset in P
    public int offsetY;
    public uint drawX;  // the first pixel of P drawn, from P's origin: id draws P pixel id + draw
    public uint drawY;  // levelColour starts at 48
    public uint4 levelColour;  // per grid level: r | g << 8 | b << 16 | a << 24, sRGB-encoded; [3] unused
    public uint4 levelShape;   // per grid level: the bits above; [3] unused
    public uint checkerSize;   // the alpha checkerboard's square, backing pixels, at least 1
    // The cell levels, read by the inspect pipeline and the grid prepass only (a LEVEL_* shape
    // word each, 0: not drawn): the chroma outline's step on x and on y, and the cell lines.
    public uint cellChromaX;
    public uint cellChromaY;
    public uint cellLine;
};

// Any cell level drawn (the inspect pipeline's): the same for every thread of a dispatch.
public bool cellLevelsDrawn(PushConstants constants)
{
    return ((constants.cellChromaX | constants.cellChromaY | constants.cellLine) & LEVEL_DRAWN) !=
           0;
}

// Any grid level drawn: the same for every thread of a dispatch.
public bool gridDrawn(PushConstants constants)
{
    return ((constants.levelShape.x | constants.levelShape.y | constants.levelShape.z) &
            LEVEL_DRAWN) != 0;
}

public LineLevel levelOf(uint shape)
{
    LineLevel level;
    level.stepLog2 = shape & LEVEL_STEP_MASK;
    level.thickness = (shape >> LEVEL_THICKNESS_SHIFT) & LEVEL_THICKNESS_MASK;
    level.drawn = (shape & LEVEL_DRAWN) != 0;
    level.halo = (shape & LEVEL_HALO) != 0;
    return level;
}

public AxisView axisOf(int offset, float zoom, uint size, uint extent)
{
    AxisView axis;
    axis.offset = offset;
    axis.zoom = zoom;
    axis.size = size;
    axis.extent = extent;
    return axis;
}

// The grid hit of a column (row 0 of the line image, at x) or a row (row 1, at y; rows 2 and 3 the
// cell levels', level 0 the chroma outline, level 1 the cell lines): 0 for none,
// else 1 | level << 1 | halo << 3. The picture pass needs no k.
public uint packHit(LineHit hit)
{
    return hit.hit ? 1u | (hit.level << 1u) | (hit.halo ? 8u : 0u) : 0u;
}

public LineHit unpackHit(uint packed)
{
    LineHit hit = { (packed & 1u) != 0u, (packed >> 1u) & 3u, 0u, (packed & 8u) != 0u };
    return hit;
}

public static const uint FLAG_DARK = 1;
public static const uint FLAG_SKIP_ENCODE = 2;
public static const uint FLAG_ALPHA = 4;
public static const uint MAX_TAPS = 4;

// The halo darkens the pixel by this share of the level's opacity.
public static const float HALO_STRENGTH = 0.6;

// IEC 61966-2-1, as dvv::media::srgbEncode/srgbDecode (input clamped).
public float srgbEncode(float x)
{
    x = clamp(x, 0.0, 1.0);
    return x <= 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

public float srgbDecode(float x)
{
    x = clamp(x, 0.0, 1.0);
    return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}

public float3 texel(RWTexture2D<float4> display, PushConstants pc, int x, int y)
{
    uint2 p = uint2(clamp(x, 0, int(pc.imageW) - 1), clamp(y, 0, int(pc.imageH) - 1));
    return display[p].rgb;
}

public float3 mix3(float3 a, float3 b, float t)
{
    precise const float3 mixed = a + t * (b - a);
    return mixed;
}
// The 4-texel bilinear around (u - 0.5, v - 0.5), edges clamped.
public float3 bilinear(RWTexture2D<float4> display, PushConstants pc, float u, float v)
{
    float x = u - 0.5;
    float y = v - 0.5;
    float x0 = floor(x);
    float y0 = floor(y);
    float fx = x - x0;
    float fy = y - y0;
    int ix = int(x0);
    int iy = int(y0);
    float3 top = mix3(texel(display, pc, ix, iy), texel(display, pc, ix + 1, iy), fx);
    float3 bottom = mix3(texel(display, pc, ix, iy + 1), texel(display, pc, ix + 1, iy + 1), fx);
    return mix3(top, bottom, fy);
}

public float3 sampleDisplay(RWTexture2D<float4> display, PushConstants pc, float u, float v)
{
    if (pc.zoom >= 1.0)
    {
        return texel(display, pc, int(floor(u)), int(floor(v)));
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
            sum = sum + bilinear(display, pc, ua, vb);
        }
    }
    return sum / float(n * n);
}

// The alpha pass's texel: premultiplied by its alpha clamped to [0, 1], edges clamped.
public float4 texel4(RWTexture2D<float4> display, PushConstants pc, int x, int y)
{
    uint2 p = uint2(clamp(x, 0, int(pc.imageW) - 1), clamp(y, 0, int(pc.imageH) - 1));
    const float4 t = display[p];
    const float a = clamp(t.a, 0.0, 1.0);
    precise const float3 rgb = t.rgb * a;
    return float4(rgb, a);
}

public float4 mix4(float4 a, float4 b, float t)
{
    precise const float4 mixed = a + t * (b - a);
    return mixed;
}

// bilinear over texel4.
public float4 bilinear4(RWTexture2D<float4> display, PushConstants pc, float u, float v)
{
    float x = u - 0.5;
    float y = v - 0.5;
    float x0 = floor(x);
    float y0 = floor(y);
    float fx = x - x0;
    float fy = y - y0;
    int ix = int(x0);
    int iy = int(y0);
    float4 top = mix4(texel4(display, pc, ix, iy), texel4(display, pc, ix + 1, iy), fx);
    float4 bottom = mix4(texel4(display, pc, ix, iy + 1), texel4(display, pc, ix + 1, iy + 1), fx);
    return mix4(top, bottom, fy);
}

// sampleDisplay over texel4: the same taps and weights.
public float4 sampleDisplay4(RWTexture2D<float4> display, PushConstants pc, float u, float v)
{
    if (pc.zoom >= 1.0)
    {
        return texel4(display, pc, int(floor(u)), int(floor(v)));
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
            sum = sum + bilinear4(display, pc, ua, vb);
        }
    }
    return sum / float(n * n);
}

// The checkerboard's linear grey at P pixel (px, py), inside the image span (px - offsetX >= 0):
// square (0, 0) at the image origin is even.
public float checker(PushConstants pc, int px, int py, bool dark)
{
    const uint sx = uint(px - pc.offsetX) / pc.checkerSize;
    const uint sy = uint(py - pc.offsetY) / pc.checkerSize;
    const bool odd = ((sx + sy) & 1) != 0;
    return srgbDecode(checkerGrey(dark, odd));
}

public float3 encode3(float3 c)
{
    return float3(srgbEncode(c.r), srgbEncode(c.g), srgbEncode(c.b));
}

public float3 decode3(float3 c)
{
    return float3(srgbDecode(c.r), srgbDecode(c.g), srgbDecode(c.b));
}

// A byte of a packed colour as a value in [0, 1].
public float unpackByte(uint packed, uint shift)
{
    return float((packed >> shift) & 0xFF) * (1.0 / 255.0);
}

public bool inSpan(PixelSpan span, int pixel)
{
    return pixel >= span.begin && pixel < span.end;
}

// The hit drawn where both axes hit: the smaller level; at the same level a line beats a halo (a
// horizontal line is not cut by the halo of a vertical one); else x.
public LineHit winner(LineHit x, LineHit y)
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
public float3 blendLine(PushConstants pc, float3 encoded, LineHit hit)
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
