// rawreel's ruler pass: one thread per pixel of a ruler band, the top band (the x axis, above the
// hugged rect H, with the corner left of its ruler) or the left band below it (the y axis, left of
// H). It reads no video data. Per pixel: the viewport background outside the ruler and the
// corner; on the ruler its background, a 1-pixel separator on the edge toward the picture, the
// ticks of the drawn levels in the inner tick band, the lit tick (the tick nearest the cursor, in
// the cursor colour, opaque, through the tick band into the label band), then the cursor marker in
// the cursor colour; the rest of the label band stays ruler background. Ticks are placed along the
// axis from P's origin and the view, not from the ruler's place. A tick and its grid line come from
// the same dvv-media rule (lineAt, with the tick width as every level's thickness), clipped to the
// image span inside P, so they land on the same pixel. The colours are sRGB-encoded and written
// encoded, or decoded when the swapchain encodes.
// rulerPixel() in tests/ruler_reference.cpp computes the same, in the same order.

import composite_common;
import dvv_media.view_lines;
import viewport_background;

using namespace dvv::media;

static const uint RULER_DARK = 1;
static const uint RULER_SKIP_ENCODE = 2;
static const uint RULER_TICKS = 4;
static const uint RULER_CURSOR = 8;
static const uint RULER_IMAGE = 16;
static const uint RULER_LIT = 32;
static const float MARKER_OPACITY = 0.9;

// = rawreel::RulerConstants, 128 bytes.
struct RulerConstants
{
    uint regionX;  // the dispatched band in the output
    uint regionY;
    uint regionW;
    uint regionH;
    uint axis;  // 0: the top band, 1: the left band
    uint flags;
    float zoom;
    int offset;          // the view's integer offset on the axis, from P's edge
    uint pictureStart;   // P's first output pixel on the axis
    uint pictureExtent;  // P's size on the axis
    uint imageSize;
    uint strip;  // the strip's thickness toward P, as pictureRegion clamps it
    uint tickBand;
    uint tickWidth;
    uint markerDepth;
    uint cursor;     // the cursor's image coordinate on the axis
    uint labelBand;  // strip - tickBand: the numbers' band; the lit tick reaches
                     // litDepth - tickBand into it
    uint rulerEdge;   // the first row (top band) or column (left band) past the ruler
    uint rulerBegin;  // the ruler along the axis: [rulerBegin, rulerEnd)
    uint rulerEnd;    // levelShape starts at 80
    uint4 levelShape;  // per level: stepLog2 | LEVEL_DRAWN (composite_common); [3] unused
    uint4 tickLength;  // per level, from the inner edge; [3] unused
    uint cursorColour;  // the lit tick and the marker: 0xRRGGBB, sRGB-encoded
    uint litTick;       // the lit tick's image coordinate on the axis (RULER_LIT)
    uint litDepth;  // the lit tick's depth from the inner edge, at most strip
    uint reserved1;
};

[[vk::push_constant]] ConstantBuffer<RulerConstants> pc;

[[vk::binding(0, 0)]] [format("rgba8")] RWTexture2D<float4> output;

// IEC 61966-2-1, as dvv::media::srgbDecode (input clamped).
float srgbDecode(float x)
{
    x = clamp(x, 0.0, 1.0);
    return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}

// 0xRRGGBB, sRGB-encoded.
float3 rgb(uint packed)
{
    return float3(float((packed >> 16) & 0xFF), float((packed >> 8) & 0xFF),
                  float(packed & 0xFF)) / 255.0;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    if (id.x >= pc.regionW || id.y >= pc.regionH)
    {
        return;
    }
    const uint x = pc.regionX + id.x;
    const uint y = pc.regionY + id.y;
    const bool top = pc.axis == 0;
    const uint across = top ? y : x;
    const uint along_pixel = top ? x : y;
    const int along = int(along_pixel) - int(pc.pictureStart);
    const bool dark = (pc.flags & RULER_DARK) != 0;
    // The ruler's rows (top band) or columns (left band), which never underflow: the hugged
    // rect lies in P. The corner sits left of the top ruler; everything else is background.
    const bool rows = across >= pc.rulerEdge - pc.strip && across < pc.rulerEdge;
    const bool ruler = rows && along_pixel >= pc.rulerBegin && along_pixel < pc.rulerEnd;
    const bool corner = top && rows && along_pixel >= pc.rulerBegin - pc.pictureStart &&
                        along_pixel < pc.rulerBegin;
    // 0 on the ruler's row or column next to the picture.
    const uint depth = pc.rulerEdge - 1 - across;
    float3 colour = float3(viewportBackground(dark));
    if (corner)
    {
        colour = rgb(dark ? 0x262626 : 0xcfcfcf);
    }
    else if (ruler && depth == 0)
    {
        colour = rgb(dark ? 0x3d3d3d : 0xb5b5b5);
    }
    else if (ruler)
    {
        colour = rgb(dark ? 0x323232 : 0xd9d9d9);
    }
    if (ruler && (pc.flags & RULER_IMAGE) != 0)
    {
        AxisView axis;
        axis.offset = pc.offset;
        axis.zoom = pc.zoom;
        axis.size = pc.imageSize;
        axis.extent = pc.pictureExtent;
        if ((pc.flags & RULER_TICKS) != 0 && depth < pc.tickBand)
        {
            LineLevel levels[LINE_LEVELS];
            for (uint l = 0; l < LINE_LEVELS; l = l + 1)
            {
                levels[l] = levelOf(pc.levelShape[l]);
                levels[l].thickness = pc.tickWidth;  // a tick has the tick width, never a halo
                levels[l].halo = false;
            }
            const LineHit hit = lineAt(axis, levels, along);
            if (hit.hit && depth < pc.tickLength[hit.level])
            {
                colour = hit.level == 0 ? rgb(dark ? 0xe6e6e6 : 0x333333)
                                        : rgb(dark ? 0x9a9a9a : 0x777777);
            }
        }
        // The lit tick: three tick widths, centred on the tick, litDepth deep (past the tick band,
        // so taller than every tick). RULER_LIT comes only with RULER_TICKS.
        if ((pc.flags & RULER_LIT) != 0 && depth < pc.litDepth)
        {
            const PixelSpan lit = lineSpan(axis, pc.litTick, 3 * pc.tickWidth);
            if (along >= lit.begin && along < lit.end)
            {
                colour = rgb(pc.cursorColour);
            }
        }
        if ((pc.flags & RULER_CURSOR) != 0 && depth < pc.markerDepth)
        {
            const PixelSpan span = cursorSpan(axis, pc.cursor);
            if (along >= span.begin && along < span.end)
            {
                precise const float3 marked = colour + MARKER_OPACITY * (rgb(pc.cursorColour) - colour);
                colour = marked;
            }
        }
    }
    if ((pc.flags & RULER_SKIP_ENCODE) != 0)
    {
        colour = float3(srgbDecode(colour.x), srgbDecode(colour.y), srgbDecode(colour.z));
    }
    output[uint2(x, y)] = float4(colour, 1.0);
}
