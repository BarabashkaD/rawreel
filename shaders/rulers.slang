// rawreel's ruler pass: one thread per pixel of a ruler band, the top band (the x axis, above the
// hugged rect H, with the corner left of its ruler) or the left band below it (the y axis, left of
// H). It reads no video data. Per pixel: the viewport background outside the ruler and the
// corner; on the ruler its background, a 1-pixel separator on the edge toward the picture, the
// ticks of the drawn levels in the inner tick band, the lit tick (the tick nearest the cursor, in
// the cursor colour, opaque, through the tick band into the label band), then the cursor marker in
// the cursor colour; then, with RULER_NUMBERS, the labels of core/ruler_labels.h in the outer label
// band: the numbers, the end tags and the cursor tag, their glyphs from the strip, the coverage
// blended in encoded space and the rows clipped to the band. Ticks are placed along the
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
static const uint RULER_NUMBERS = 64;
static const uint RULER_EDGE = 128;
static const uint RULER_TAG_TEXT_BLACK = 256;
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
    uint labelSpacing;  // numberGap | tagPad << 8 | tagInset << 16 | tagMargin << 24
};

[[vk::push_constant]] ConstantBuffer<RulerConstants> pc;

[[vk::binding(0, 0)]] [format("rgba8")] RWTexture2D<float4> output;
// The glyph strip: ten digit cells, each width / 10 wide, the coverage in every channel; a 1 x 1
// dummy without RULER_NUMBERS.
[[vk::binding(1, 0)]] [format("rgba8")] RWTexture2D<float4> glyphs;

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

// The labels: core/ruler_labels.h is the rule, computed here per pixel. Pixels are saturated to
// [-2^30, 2^30] as there.
static const int LABEL_PIXEL_LIMIT = 0x40000000;

// A number or a tag: its value, its text's first pixel and its box along the axis, in output
// pixels; drawn false: none.
struct Label
{
    bool drawn;
    uint value;
    int textBegin;
    int boxBegin;
    int boxEnd;
};

uint decimalDigits(uint value)
{
    uint digits = 1u;
    for (; value >= 10u; value = value / 10u)
    {
        digits = digits + 1u;
    }
    return digits;
}

Label noLabel()
{
    Label none;
    none.drawn = false;
    none.value = 0u;
    none.textBegin = 0;
    none.boxBegin = 0;
    none.boxEnd = 0;
    return none;
}

int textLength(uint value, uint advance)
{
    return int(decimalDigits(value) * advance);
}

Label tagAt(uint value, int boxBegin, uint advance, int pad)
{
    Label tag;
    tag.drawn = true;
    tag.value = value;
    tag.textBegin = boxBegin + pad;
    tag.boxBegin = boxBegin;
    tag.boxEnd = boxBegin + textLength(value, advance) + 2 * pad;
    return tag;
}

// Whether the box [begin, end) touches the drawn `tag`'s box widened by `advance` on each side.
bool touches(int begin, int end, Label tag, uint advance)
{
    const int widen = int(advance);
    return tag.drawn && begin < tag.boxEnd + widen && tag.boxBegin - widen < end;
}

// a + b saturated, for b >= 0, without an int overflow.
int saturatedSum(int a, int b)
{
    return a > LABEL_PIXEL_LIMIT - b ? LABEL_PIXEL_LIMIT : max(a + b, -LABEL_PIXEL_LIMIT);
}

// The text of `label` at pixel `p` along and glyph row `gy`, in `text` over `colour`; the left
// ruler reads bottom to top, its text's last pixel is the first column.
float3 drawText(float3 colour, Label label, int p, int gy, bool top, uint advance, uint height,
                float3 text)
{
    const uint digits = decimalDigits(label.value);
    const int length = int(digits * advance);
    if (p < label.textBegin || p >= label.textBegin + length || gy < 0 || gy >= int(height))
    {
        return colour;
    }
    const int offset = top ? p - label.textBegin : label.textBegin + length - 1 - p;
    const uint cell = uint(offset) / advance;
    uint power = 1u;
    for (uint i = cell + 1u; i < digits; i = i + 1u)
    {
        power = power * 10u;
    }
    const uint digit = (label.value / power) % 10u;
    const uint gx = digit * advance + uint(offset) % advance;
    const float coverage = glyphs[uint2(gx, uint(gy))].x;
    precise const float3 blended = colour + coverage * (text - colour);
    return blended;
}

// The labels over `colour` at a label-band pixel of the ruler, `along_pixel` along and `across`
// the band, in drawing order (numbers, the first tag, the last tag, the cursor tag).
float3 drawLabels(float3 colour, AxisView axis, uint along_pixel, uint across, bool top, bool dark)
{
    uint width;
    uint height;
    glyphs.GetDimensions(width, height);
    const uint advance = width / 10u;
    if (advance == 0u)
    {
        return colour;
    }
    const PixelRange visible = visiblePixels(axis);
    if (!visible.valid)
    {
        return colour;
    }
    const int gap = int(pc.labelSpacing & 0xFFu);
    const int pad = int((pc.labelSpacing >> 8) & 0xFFu);
    const uint inset = (pc.labelSpacing >> 16) & 0xFFu;
    const int margin = int(pc.labelSpacing >> 24);
    const int left = int(pc.rulerBegin) + margin;
    const int right = int(pc.rulerEnd) - margin;
    const int p = int(along_pixel);

    // The end tags; the last one's fit is decided before the cursor tag drops what it touches.
    Label first_tag = noLabel();
    Label last_tag = noLabel();
    Label cursor_tag = noLabel();
    const uint first = visible.first;
    const uint last = (pc.flags & RULER_EDGE) != 0 ? visible.last + 1u : visible.last;
    const int first_length = textLength(first, advance) + 2 * pad;
    const int last_length = textLength(last, advance) + 2 * pad;
    if (left + first_length <= right)
    {
        first_tag = tagAt(first, left, advance, pad);
        if (first_length + int(advance) + last_length <= right - left)
        {
            last_tag = tagAt(last, right - last_length, advance, pad);
        }
    }
    if ((pc.flags & RULER_CURSOR) != 0 && pc.cursor < axis.size)
    {
        const int length = textLength(pc.cursor, advance) + 2 * pad;
        const float centre_at = floor(pixelCentreAt(axis, pc.cursor));
        const float raised = centre_at >= -float(LABEL_PIXEL_LIMIT) ? centre_at
                                                                    : -float(LABEL_PIXEL_LIMIT);
        const float kept = raised > float(LABEL_PIXEL_LIMIT) ? float(LABEL_PIXEL_LIMIT) : raised;
        const int centre = saturatedSum(int(kept), int(pc.pictureStart));
        const int begin = max(left, min(right - length, centre - length / 2));
        cursor_tag = tagAt(pc.cursor, begin, advance, pad);
        if (touches(first_tag.boxBegin, first_tag.boxEnd, cursor_tag, advance) && first_tag.drawn)
        {
            first_tag.drawn = false;
        }
        if (touches(last_tag.boxBegin, last_tag.boxEnd, cursor_tag, advance) && last_tag.drawn)
        {
            last_tag.drawn = false;
        }
    }

    // The one number that can hold p: the largest multiple k of the step in [0, size] whose text
    // begins at or before p, firstPixelOf(k) + gap <= p, which is k <= u(p - gap) as u is
    // monotonic. That needs firstPixelOf to be exact, which dvv-media gives while
    // |offset| + k x zoom < 2^22; past that its estimate may be two pixels low and the CPU walk
    // can draw another number. Number boxes do not overlap where the step reaches its spacing;
    // where it stops first (the step passes the size) only 0 is a multiple. Its colours follow
    // the theme.
    const uint band_start = pc.rulerEdge - pc.strip;
    const int text_top = int(band_start) + (int(pc.labelBand) - int(height)) / 2;
    const int gy = int(across) - text_top;
    if ((pc.flags & RULER_TICKS) != 0)
    {
        const float needed = float(decimalDigits(axis.size) * advance + advance);
        const uint s = numberStepLog2(pc.levelShape[0] & LEVEL_STEP_MASK, axis.zoom, needed,
                                      axis.size);
        const int q = int(along_pixel) - int(pc.pictureStart) - gap;
        const float u = floor(imageCoordinateAt(axis, q));
        if (u >= 0.0)
        {
            const uint below = u >= 4294967296.0 ? axis.size : min(uint(u), axis.size);
            const uint k = s >= 32u ? 0u : (below >> s) << s;
            Label number = noLabel();
            number.value = k;
            number.textBegin = saturatedSum(firstPixelOf(axis, k), int(pc.pictureStart) + gap);
            number.boxBegin = number.textBegin;
            number.boxEnd = number.textBegin + textLength(k, advance);
            number.drawn = number.boxBegin >= int(pc.rulerBegin) &&
                           number.boxEnd <= int(pc.rulerEnd) &&
                           !touches(number.boxBegin, number.boxEnd, first_tag, advance) &&
                           !touches(number.boxBegin, number.boxEnd, last_tag, advance) &&
                           !touches(number.boxBegin, number.boxEnd, cursor_tag, advance);
            if (number.drawn && p >= number.boxBegin && p < number.boxEnd)
            {
                colour = drawText(colour, number, p, gy, top, advance, height,
                                  rgb(dark ? 0xc9c9c9 : 0x333333));
            }
        }
    }

    // The tags: opaque boxes of the band less the inset across, square.
    const bool filled = across >= band_start + inset && across + inset < band_start + pc.labelBand;
    const float3 tag_fill = rgb(dark ? 0xe8e8e8 : 0x4d4d4d);
    const float3 tag_text = rgb(dark ? 0x1b1b1b : 0xf5f5f5);
    if (first_tag.drawn && p >= first_tag.boxBegin && p < first_tag.boxEnd)
    {
        colour = filled ? tag_fill : colour;
        colour = drawText(colour, first_tag, p, gy, top, advance, height, tag_text);
    }
    if (last_tag.drawn && p >= last_tag.boxBegin && p < last_tag.boxEnd)
    {
        colour = filled ? tag_fill : colour;
        colour = drawText(colour, last_tag, p, gy, top, advance, height, tag_text);
    }
    if (cursor_tag.drawn && p >= cursor_tag.boxBegin && p < cursor_tag.boxEnd)
    {
        colour = filled ? rgb(pc.cursorColour) : colour;
        colour = drawText(colour, cursor_tag, p, gy, top, advance, height,
                          rgb((pc.flags & RULER_TAG_TEXT_BLACK) != 0 ? 0x000000 : 0xffffff));
    }
    return colour;
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
        // The label band: RULER_NUMBERS comes only with RULER_IMAGE and a bound strip.
        if ((pc.flags & RULER_NUMBERS) != 0 && depth >= pc.tickBand)
        {
            colour = drawLabels(colour, axis, along_pixel, across, top, dark);
        }
    }
    if ((pc.flags & RULER_SKIP_ENCODE) != 0)
    {
        colour = float3(srgbDecode(colour.x), srgbDecode(colour.y), srgbDecode(colour.z));
    }
    output[uint2(x, y)] = float4(colour, 1.0);
}
