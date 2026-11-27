// rawreel's inspect picture pass: picture.slang's pass with the layers of the layer block
// (render/layer_block.h) on, run only when a layer is on, so the plain view keeps picture.slang.
// The same geometry, background, grid and encode as picture.slang (picture_common); a Planes panel
// (LAYER_PLANE_SOURCE) shows the codes of plane sourceComponent as grey, code / (2^bits - 1) taken
// as the encoded output value, nearest to each pixel's image coordinate at every zoom: exact in the
// Linear presentation (no decode/encode round trip). Under Srgb presentation (FLAG_SKIP_ENCODE)
// it is decoded to the linear light the presenter encodes again, as lossy as the rest of the
// picture there (8-bit linear output).
// The values layer (LAYER_VALUES) draws the codes of the plane bindings over the encoded picture
// and its grid at zoom >= 1, only where it draws. LAYER_TEST_CODES writes the code it looks up for
// sourceComponent into binding 8 at every image pixel shown (the exactness tests, §7).
// The padding flags (LAYER_CELL_FLAGS, with a Raw view at rawBase): at zoom >= 1 the cell of a
// sample whose own stored word, read through storedWordAddress, has padding bits set gets a 2 px
// warning-colour inner frame and a top-right corner flag, its number the warning colour and its
// label component an underline. The padding marks (LAYER_PADDING_MARKS) draw over the layers,
// from the tile map padding_check.slang filled: a disc, a dark rim and a 1 px ring per marked tile centre.
// LAYER_MARKERS draws the pin and the outline over all of it (markers.slang) but for the pixels of
// a corner flag, a value's text and a chroma label's box: the markers never hide them, but they
// draw over a flagged cell's inner frame.

import picture_common;
import layer_block;
import markers;
import dvv_gpu_text.gpu_text;
import dvv_media.sample_grid;
import dvv_media.view_lines;
import viewport_background;

using namespace dvv::media;
using namespace dvv::gpu;

// = rawreel::InspectConstants (render/picture_pass.h), 128 bytes: the picture's constants, then
// the layer block and the frame's addresses (0 while unused).
struct InspectConstants
{
    PushConstants picture;  // 0
    LayerBlock* layers;     // 96
    uint64_t rawBase;       // 104: the frame's stored bytes (the Raw view)
    uint64_t readback;      // 112: the probe's readback buffer
    uint* tiles;            // 120: the padding check's tile map (LAYER_PADDING_MARKS)
};

[[vk::push_constant]] ConstantBuffer<InspectConstants> ic;

// Set 0, pushed per dispatch. 0-2 as picture.slang; 3-6 the unpacked planes (R8_UINT or
// R16_UINT); 7 the value strip; 8 the test codes. Unused slots hold distinct 1x1 dummies.
[[vk::binding(0, 0)]] [format("rgba16f")] RWTexture2D<float4> display;
[[vk::binding(1, 0)]] [format("rgba8")] RWTexture2D<float4> output;
[[vk::binding(2, 0)]] [format("r32ui")] RWTexture2D<uint> lines;
[[vk::binding(3, 0)]] [format("unknown")] RWTexture2D<uint> plane0;
[[vk::binding(4, 0)]] [format("unknown")] RWTexture2D<uint> plane1;
[[vk::binding(5, 0)]] [format("unknown")] RWTexture2D<uint> plane2;
[[vk::binding(6, 0)]] [format("unknown")] RWTexture2D<uint> plane3;
[[vk::binding(7, 0)]] [format("rgba8")] RWTexture2D<float4> valueStrip;
[[vk::binding(8, 0)]] [format("r32ui")] RWTexture2D<uint> testCodes;

// The code of `sample` in plane `k` (its own grid).
uint planeCode(uint k, uint2 sample)
{
    switch (k)
    {
    case 0:
        return plane0[sample];
    case 1:
        return plane1[sample];
    case 2:
        return plane2[sample];
    default:
        return plane3[sample];
    }
}

// The grey of plane `k` at image coordinate (u, v): the code of the sample its pixel belongs to,
// code / (2^bits - 1), as the encoded (output) value.
float planeGrey(PushConstants pc, uint k, uint component, float u, float v)
{
    const uint2 pixel = uint2(clamp(int(floor(u)), 0, int(pc.imageW) - 1),
                              clamp(int(floor(v)), 0, int(pc.imageH) - 1));
    const uint log2SubX = component & 0xF;
    const uint log2SubY = (component >> LAYER_SUB_Y_SHIFT) & 0xF;
    const uint bits = (component >> LAYER_BITS_SHIFT) & 0xFF;
    const uint code = planeCode(k, sampleOf(pixel, log2SubX, log2SubY));
    const float top = float((1u << bits) - 1u);
    return float(code) / top;
}

// The values layer (LAYER_VALUES, zoom >= 1): the codes of the plane bindings at sampleOf, the
// glyphs from the value strip (VALUE_GLYPHS cells: digits 0-9, then the letters, a letter's cell
// from LayerBlock.component), laid out by LayerBlock.metrics {advance, lineHeight, padX, padY}.
// A glyph is the strip's own advance wide and its height tall, at the top of its line plus half
// the line's spare height. The number colour is black or white by the luma of the encoded colour
// at its cell's centre; the chroma label is the label text colour on the label box.
static const uint VALUE_GLYPH_CELLS = 17;  // rawreel::VALUE_GLYPHS
static const uint TEXT_SPACE = 0xFFFFFFFFu;  // a skipped cell
static const uint TEXT_MAX = 16;

struct Text
{
    uint cells[TEXT_MAX];
    uint length;
};

void appendCell(inout Text text, uint cell)
{
    if (text.length < TEXT_MAX)
    {
        text.cells[text.length] = cell;
        text.length = text.length + 1u;
    }
}

void appendCode(inout Text text, uint code)
{
    const uint digits = decimalDigits(code);
    for (uint i = 0u; i < digits; i = i + 1u)
    {
        appendCell(text, decimalDigitAt(code, digits, i));
    }
}

// "<letter> <code>", or the code only.
Text componentText(uint component, uint code, bool letter)
{
    Text text;
    text.length = 0u;
    if (letter)
    {
        appendCell(text, component >> LAYER_LETTER_SHIFT);
        appendCell(text, TEXT_SPACE);
    }
    appendCode(text, code);
    return text;
}

// The coverage of `text` with its first glyph's top-left at (left, top), at P pixel (px, py).
float textCoverage(Text text, GlyphCells cells, int left, int top, int px, int py)
{
    const int gx = px - left;
    const int gy = py - top;
    if (cells.advance == 0u || gx < 0 || gy < 0 || gy >= int(cells.height))
    {
        return 0.0;
    }
    const uint at = uint(gx) / cells.advance;
    if (at >= text.length || text.cells[at] == TEXT_SPACE)
    {
        return 0.0;
    }
    return cellCoverage(valueStrip, cells, text.cells[at], gx - int(at * cells.advance), gy);
}

uint subX(uint component) { return component & 0xF; }
uint subY(uint component) { return (component >> LAYER_SUB_Y_SHIFT) & 0xF; }

uint codeOf(uint k, uint2 pixel)
{
    const uint component = ic.layers->component[k];
    return planeCode(k, sampleOf(pixel, subX(component), subY(component)));
}

// The number colour over the encoded cell colour `cell`: black when its luma is above 0.5.
float3 numberColour(float3 cell)
{
    precise const float luma = 0.2126 * cell.r + 0.7152 * cell.g + 0.0722 * cell.b;
    return luma > 0.5 ? float3(0.0, 0.0, 0.0) : float3(1.0, 1.0, 1.0);
}

float3 blendText(float3 under, float coverage, float3 text)
{
    precise const float3 blended = under + coverage * (text - under);
    return blended;
}

// `lines` texts centred in the box [x0, x1) x [y0, y1), one per line, at P pixel (px, py).
float linesCoverage(Text first, Text second, Text third, Text fourth, uint lines, GlyphCells cells,
                    int4 box, int px, int py, out uint at_line)
{
    at_line = 0u;
    const int line_height = int(ic.layers->metrics.y);
    if (line_height <= 0 || lines == 0u)
    {
        return 0.0;
    }
    const int top = box.z + (box.w - box.z - int(lines) * line_height) / 2;
    if (py < top || py >= top + int(lines) * line_height)
    {
        return 0.0;
    }
    const uint line = uint(py - top) / uint(line_height);
    at_line = line;
    Text text = first;
    if (line == 1u)
    {
        text = second;
    }
    else if (line == 2u)
    {
        text = third;
    }
    else if (line == 3u)
    {
        text = fourth;
    }
    const int width = int(text.length * cells.advance);
    const int left = box.x + (box.y - box.x - width) / 2;
    const int glyph_top = top + int(line) * line_height + (line_height - int(cells.height)) / 2;
    return textCoverage(text, cells, left, glyph_top, px, py);
}

// The encoded colour main() shows at P pixel (px, py) inside the image span, before the grid and
// the values: a Planes panel's code grey, else the picture (over the checkerboard under
// FLAG_ALPHA).
float3 encodedAt(PushConstants pc, AxisView ax, AxisView ay, int px, int py)
{
    const float u = imageCoordinateAt(ax, px);
    const float v = imageCoordinateAt(ay, py);
    if ((ic.layers->flags & LAYER_PLANE_SOURCE) != 0)
    {
        const uint k = ic.layers->sourceComponent;
        const float code = planeGrey(pc, k, ic.layers->component[k], u, v);
        return float3(code, code, code);
    }
    if ((pc.flags & FLAG_ALPHA) != 0)
    {
        // The premultiplied picture over the checkerboard, in linear light.
        const float4 s = sampleDisplay4(display, pc, u, v);
        precise const float under = checker(pc, px, py, (pc.flags & FLAG_DARK) != 0) * (1.0 - s.a);
        precise const float3 blended = s.rgb + float3(under, under, under);
        return encode3(blended);
    }
    return encode3(sampleDisplay(display, pc, u, v));
}

// The number colour of the cell `cell` (x0, x1, y0, y1 in P pixels): numberColour of the encoded
// colour at its centre, one point per cell, so a glyph never changes colour inside itself.
float3 cellNumberColour(PushConstants pc, AxisView ax, AxisView ay, int4 cell)
{
    return numberColour(
        encodedAt(pc, ax, ay, cell.x + (cell.y - cell.x) / 2, cell.z + (cell.w - cell.z) / 2));
}

// Whether `sample` of component k has padding bits set in its own stored word, read from the Raw
// view through storedWordAddress (LAYER_CELL_FLAGS with a Raw view bound, else false).
bool paddingFlagged(uint k, uint2 sample)
{
    if ((ic.layers->flags & LAYER_CELL_FLAGS) == 0 || ic.rawBase == 0ull)
    {
        return false;
    }
    const StoredWordLayout layout = ic.layers->words[k];
    uint shift = 0u;
    uint padding = 0u;
    const uint64_t address = storedWordAddress(layout, sample, shift, padding);
    const uint word = readStoredWord((uint*)ic.rawBase, address, layout.wordBytes, layout.bigEndian);
    return (word & padding) != 0u;
}

// The components with a flagged sample at image pixel `pixel`, one bit each: a Planes panel's
// sourceComponent, else the unsubsampled components (a chroma sample flags its label).
uint cellFlags(uint2 pixel)
{
    uint flagged = 0u;
    const uint count = min(ic.layers->componentCount, 4u);
    const bool planes = (ic.layers->flags & LAYER_PLANE_SOURCE) != 0;
    for (uint k = 0u; k < count; k = k + 1u)
    {
        const uint component = ic.layers->component[k];
        const bool own = planes ? k == ic.layers->sourceComponent
                                : (subX(component) == 0u && subY(component) == 0u);
        if (own && paddingFlagged(k, sampleOf(pixel, subX(component), subY(component))))
        {
            flagged |= 1u << k;
        }
    }
    return flagged;
}

// The flagged cell `cell` (x0, x1, y0, y1 in P pixels) at P pixel (px, py): 0 outside its flag, 2
// on its corner flag (legs max(4, advance / 2), at its top-right corner), else 1 on its 2-pixel
// inner frame. The markers give way to the corner flag, not to the frame.
uint onCellFlag(int4 cell, int px, int py)
{
    if (px < cell.x || px >= cell.y || py < cell.z || py >= cell.w)
    {
        return 0u;
    }
    const int leg = max(4, int(ic.layers->metrics.x) / 2);
    if ((cell.y - 1 - px) + (py - cell.z) < leg)
    {
        return 2u;
    }
    return (px < cell.x + 2 || px >= cell.y - 2 || py < cell.z + 2 || py >= cell.w - 2) ? 1u : 0u;
}

// The cell of image pixel `pixel` in P pixels: a Planes panel's sample block, else the pixel.
int4 flagCell(PushConstants pc, AxisView ax, AxisView ay, uint2 pixel)
{
    uint4 block = uint4(pixel, 1u, 1u);
    if ((ic.layers->flags & LAYER_PLANE_SOURCE) != 0)
    {
        const uint component = ic.layers->component[ic.layers->sourceComponent];
        block = blockOf(sampleOf(pixel, subX(component), subY(component)), subX(component),
                        subY(component), uint2(pc.imageW, pc.imageH));
    }
    return int4(firstPixelOf(ax, block.x), firstPixelOf(ax, block.x + block.z),
                firstPixelOf(ay, block.y), firstPixelOf(ay, block.y + block.w));
}

// The padding marks (LAYER_PADDING_MARKS) at P pixel (px, py), from the 3 x 3 tiles around it:
// 0 none, else 1 the disc (markSize across), 2 its markRim dark rim, 3 the 1-pixel ring around
// it, by the nearest centre: 4 d^2 against markSize^2, (markSize + 2 markRim)^2 and
// (markSize + 2 markRim + 2)^2.
uint markAt(PushConstants pc, int px, int py)
{
    const int tile_size = int(ic.layers->tileSize);
    const int tiles_x = int(ic.layers->tilesX);
    if (tile_size <= 0 || tiles_x <= 0 || ic.tiles == nullptr)
    {
        return 0u;
    }
    const int tiles_y = int(ic.layers->tilesY);
    const int tx = px / tile_size;
    const int ty = py / tile_size;
    int nearest = -1;
    for (int dy = -1; dy <= 1; dy = dy + 1)
    {
        for (int dx = -1; dx <= 1; dx = dx + 1)
        {
            const int x = tx + dx;
            const int y = ty + dy;
            if (x < 0 || y < 0 || x >= tiles_x || y >= tiles_y)
            {
                continue;
            }
            const uint centre = ic.tiles[y * tiles_x + x];
            if (centre == 0xFFFFFFFFu)
            {
                continue;
            }
            const int ex = px - int(centre & 0xFFFFu);
            const int ey = py - int(centre >> 16u);
            const int d2 = ex * ex + ey * ey;
            nearest = nearest < 0 ? d2 : min(nearest, d2);
        }
    }
    if (nearest < 0)
    {
        return 0u;
    }
    const int size = int(ic.layers->markSize);
    const int rim = int(ic.layers->markRim);
    const int d4 = 4 * nearest;
    if (d4 <= size * size)
    {
        return 1u;
    }
    if (d4 <= (size + 2 * rim) * (size + 2 * rim))
    {
        return 2u;
    }
    return d4 <= (size + 2 * rim + 2) * (size + 2 * rim + 2) ? 3u : 0u;
}

// The values over the encoded colour `shown` at P pixel (px, py) of image pixel `pixel`; false
// when nothing is drawn there. A text wider (a Planes code also taller) than its box, clipped at
// the image's edge, is not drawn: a cut fragment could read as another value.
bool drawValues(PushConstants pc, AxisView ax, AxisView ay, uint2 pixel, int px, int py,
                inout float3 shown)
{
    const GlyphCells cells = glyphCells(valueStrip, VALUE_GLYPH_CELLS);
    if (cells.advance == 0u)
    {
        return false;
    }
    const uint4 metrics = ic.layers->metrics;
    const int pad_x = int(metrics.z);
    const int pad_y = int(metrics.w);
    const int line_height = int(metrics.y);
    const uint count = min(ic.layers->componentCount, 4u);
    const float3 warning = layerColour(ic.layers->warning);
    uint hit_line = 0u;
    Text texts[4];
    for (uint i = 0u; i < 4u; i = i + 1u)
    {
        texts[i].length = 0u;
    }
    if ((ic.layers->flags & LAYER_PLANE_SOURCE) != 0)
    {
        // The sample cell of plane k: its code, centred.
        const uint k = ic.layers->sourceComponent;
        const uint component = ic.layers->component[k];
        const uint2 sample = sampleOf(pixel, subX(component), subY(component));
        const uint bx0 = sample.x << subX(component);
        const uint by0 = sample.y << subY(component);
        const uint bx1 = min(bx0 + (1u << subX(component)), pc.imageW);
        const uint by1 = min(by0 + (1u << subY(component)), pc.imageH);
        const int4 cell = int4(firstPixelOf(ax, bx0), firstPixelOf(ax, bx1), firstPixelOf(ay, by0),
                               firstPixelOf(ay, by1));
        const int4 box = int4(cell.x + pad_x, cell.y - pad_x, cell.z + pad_y, cell.w - pad_y);
        texts[0] = componentText(component, planeCode(k, sample), false);
        if (int(texts[0].length * cells.advance) > box.y - box.x || line_height > box.w - box.z)
        {
            return false;
        }
        const float coverage =
            linesCoverage(texts[0], texts[1], texts[2], texts[3], 1u, cells, box, px, py, hit_line);
        if (coverage <= 0.0)
        {
            return false;
        }
        // A flagged sample's number in the warning colour (still its true code).
        const float3 text =
            paddingFlagged(k, sample) ? warning : cellNumberColour(pc, ax, ay, cell);
        shown = blendText(shown, coverage, text);
        return true;
    }
    // Structure: the unsubsampled components stack in the cell; the subsampled ones are the
    // chroma label of their block (the most subsampled component's), at its bottom.
    uint lines = 0u;
    uint label_x = 0u;
    uint label_y = 0u;
    bool labels = false;
    for (uint k = 0u; k < count; k = k + 1u)
    {
        const uint component = ic.layers->component[k];
        if (subX(component) != 0u || subY(component) != 0u)
        {
            labels = true;
            label_x = max(label_x, subX(component));
            label_y = max(label_y, subY(component));
        }
        else
        {
            lines = lines + 1u;
        }
    }
    const uint bx0 = (pixel.x >> label_x) << label_x;
    const uint by0 = (pixel.y >> label_y) << label_y;
    const uint bx1 = min(bx0 + (1u << label_x), pc.imageW);
    const uint by1 = min(by0 + (1u << label_y), pc.imageH);
    const bool bottom_row = labels && pixel.y + 1u == by1;
    uint line = 0u;
    uint line_component[4] = { 0u, 0u, 0u, 0u };
    for (uint k = 0u; k < count; k = k + 1u)
    {
        const uint component = ic.layers->component[k];
        if (subX(component) == 0u && subY(component) == 0u)
        {
            texts[line] = componentText(component, codeOf(k, pixel), lines > 1u);
            line_component[line] = k;
            line = line + 1u;
        }
    }
    const int4 cell = int4(firstPixelOf(ax, pixel.x), firstPixelOf(ax, pixel.x + 1u),
                           firstPixelOf(ay, pixel.y), firstPixelOf(ay, pixel.y + 1u));
    const int4 box = int4(cell.x + pad_x, cell.y - pad_x, cell.z + pad_y,
                          cell.w - pad_y - (bottom_row ? line_height + pad_y : 0));
    bool drawn = false;
    const float coverage =
        linesCoverage(texts[0], texts[1], texts[2], texts[3], lines, cells, box, px, py, hit_line);
    if (coverage > 0.0)
    {
        const float3 text = paddingFlagged(line_component[hit_line], pixel)
                                ? warning
                                : cellNumberColour(pc, ax, ay, cell);
        shown = blendText(shown, coverage, text);
        drawn = true;
    }
    if (!labels)
    {
        return drawn;
    }
    // The label "U 502 V 527" on its box, centred in the block, padY above its bottom.
    Text label;
    label.length = 0u;
    const uint2 block = uint2(bx0, by0);
    // The cells of the flagged components' texts, [first, end) each, underlined.
    uint2 underlined[4] = { uint2(0u, 0u), uint2(0u, 0u), uint2(0u, 0u), uint2(0u, 0u) };
    for (uint k = 0u; k < count; k = k + 1u)
    {
        const uint component = ic.layers->component[k];
        if (subX(component) != 0u || subY(component) != 0u)
        {
            if (label.length != 0u)
            {
                appendCell(label, TEXT_SPACE);
            }
            const uint first = label.length;
            appendCell(label, component >> LAYER_LETTER_SHIFT);
            appendCell(label, TEXT_SPACE);
            appendCode(label, codeOf(k, block));
            if (paddingFlagged(k, sampleOf(block, subX(component), subY(component))))
            {
                underlined[k] = uint2(first, label.length);
            }
        }
    }
    const int width = int(label.length * cells.advance);
    const int block_x0 = firstPixelOf(ax, bx0);
    const int block_x1 = firstPixelOf(ax, bx1);
    if (width + 2 * pad_x > block_x1 - block_x0)
    {
        return drawn;  // wider than its (clipped) block
    }
    const int box_left = block_x0 + (block_x1 - block_x0 - (width + 2 * pad_x)) / 2;
    const int box_bottom = firstPixelOf(ay, by1) - pad_y;
    const int box_top = box_bottom - line_height;
    if (px < box_left || px >= box_left + width + 2 * pad_x || py < box_top || py >= box_bottom)
    {
        return drawn;
    }
    const uint box_colour = ic.layers->colours0.w;
    shown = blendText(shown, layerOpacity(box_colour), layerColour(box_colour));
    const int glyph_top = box_top + (line_height - int(cells.height)) / 2;
    const float label_coverage = textCoverage(label, cells, box_left + pad_x, glyph_top, px, py);
    shown = blendText(shown, label_coverage, layerColour(ic.layers->colours0.z));
    // The underline: the row below the glyphs (the box's last row at most), in the warning colour.
    const int underline = min(glyph_top + int(cells.height), box_bottom - 1);
    const int at = (px - box_left - pad_x) / int(cells.advance);
    if (py == underline && px >= box_left + pad_x)
    {
        for (uint k = 0u; k < count; k = k + 1u)
        {
            if (uint(at) >= underlined[k].x && uint(at) < underlined[k].y)
            {
                shown = warning;
            }
        }
    }
    return true;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    const PushConstants pc = ic.picture;
    // Thread id draws P pixel id + draw: the bounds test, the line table and the write use it.
    const uint2 p = uint2(id.x + pc.drawX, id.y + pc.drawY);
    if (p.x >= pc.viewportW || p.y >= pc.viewportH)
    {
        return;
    }
    const uint flags = ic.layers->flags;
    const AxisView ax = axisOf(pc.offsetX, pc.zoom, pc.imageW, pc.viewportW);
    const AxisView ay = axisOf(pc.offsetY, pc.zoom, pc.imageH, pc.viewportH);
    const int px = int(p.x);
    const int py = int(p.y);
    const bool inside = inSpan(imageSpan(ax), px) && inSpan(imageSpan(ay), py);
    const bool dark = (pc.flags & FLAG_DARK) != 0;
    float grey = srgbDecode(viewportBackground(dark));
    float3 colour = float3(grey, grey, grey);
    bool encoded = false;  // `colour` holds the encoded value (a Planes panel), not linear light
    if (inside)
    {
        const float u = imageCoordinateAt(ax, px);
        const float v = imageCoordinateAt(ay, py);
        if ((flags & LAYER_PLANE_SOURCE) != 0)
        {
            const uint k = ic.layers->sourceComponent;
            const float code = planeGrey(pc, k, ic.layers->component[k], u, v);
            colour = float3(code, code, code);
            encoded = true;
        }
        else if ((pc.flags & FLAG_ALPHA) != 0)
        {
            // The premultiplied picture over the checkerboard, in linear light.
            const float4 s = sampleDisplay4(display, pc, u, v);
            precise const float under = checker(pc, px, py, dark) * (1.0 - s.a);
            precise const float3 blended = s.rgb + float3(under, under, under);
            colour = blended;
        }
        else
        {
            colour = sampleDisplay(display, pc, u, v);
        }
    }
    const bool skip = (pc.flags & FLAG_SKIP_ENCODE) != 0;
    // The encoded value, and what the output holds: linear light under FLAG_SKIP_ENCODE.
    const float3 shown = encoded ? colour : encode3(colour);
    float3 result = skip ? (encoded ? decode3(colour) : colour) : shown;
    float3 drawn = shown;  // the encoded value with the lines and the values on it
    bool lined = false;
    if (inside && cellLevelsDrawn(pc))
    {
        // The cell lines, then the chroma outline over them (the smaller level wins), under the
        // grid.
        const LineHit hit =
            winner(unpackHit(lines[uint2(p.x, 2)]), unpackHit(lines[uint2(p.y, 3)]));
        if (hit.hit)
        {
            const uint colour = hit.level == 0u ? ic.layers->colours0.y : ic.layers->colours0.x;
            const float3 line =
                float3(unpackByte(colour, 0), unpackByte(colour, 8), unpackByte(colour, 16));
            drawn = mix3(drawn, line, unpackByte(colour, 24));
            lined = true;
        }
    }
    if (inside && gridDrawn(pc))
    {
        // The prepass's lineAt of this column and of this row.
        const LineHit hit =
            winner(unpackHit(lines[uint2(p.x, 0)]), unpackHit(lines[uint2(p.y, 1)]));
        if (hit.hit)
        {
            drawn = blendLine(pc, drawn, hit);
            lined = true;
        }
    }
    bool kept = false;  // a corner flag, a value or a label: the markers give way to it
    if (inside && (flags & LAYER_CELL_FLAGS) != 0 && pc.zoom >= 1.0)
    {
        // Level 2: the cell of a flagged sample gets an inner frame and a corner flag.
        const uint2 pixel =
            uint2(clamp(int(floor(imageCoordinateAt(ax, px))), 0, int(pc.imageW) - 1),
                  clamp(int(floor(imageCoordinateAt(ay, py))), 0, int(pc.imageH) - 1));
        const uint flag =
            cellFlags(pixel) != 0u ? onCellFlag(flagCell(pc, ax, ay, pixel), px, py) : 0u;
        if (flag != 0u)
        {
            drawn = layerColour(ic.layers->warning);
            kept = flag == 2u;
            lined = true;
        }
    }
    if (lined)
    {
        result = skip ? decode3(drawn) : drawn;
    }
    if (inside && (flags & (LAYER_VALUES | LAYER_TEST_CODES)) != 0)
    {
        const uint2 pixel =
            uint2(clamp(int(floor(imageCoordinateAt(ax, px))), 0, int(pc.imageW) - 1),
                  clamp(int(floor(imageCoordinateAt(ay, py))), 0, int(pc.imageH) - 1));
        if ((flags & LAYER_TEST_CODES) != 0)
        {
            // §7: the code the values layer looks up for sourceComponent, at the image pixel.
            testCodes[pixel] = codeOf(ic.layers->sourceComponent, pixel);
        }
        if ((flags & LAYER_VALUES) != 0 && pc.zoom >= 1.0 &&
            drawValues(pc, ax, ay, pixel, px, py, drawn))
        {
            result = skip ? decode3(drawn) : drawn;
            kept = true;
        }
    }
    if ((flags & LAYER_PADDING_MARKS) != 0)
    {
        // Level 1: the marks of the padding check's tile map, over the layers.
        const uint mark = markAt(pc, px, py);
        if (mark != 0u)
        {
            const float3 colour =
                mark == 2u ? float3(0.0, 0.0, 0.0) : layerColour(ic.layers->warning);
            result = skip ? decode3(colour) : colour;
        }
    }
    float3 marker;
    if (!kept && (flags & LAYER_MARKERS) != 0 && markerAt(ic.layers, ax, ay, px, py, marker))
    {
        result = skip ? decode3(marker) : marker;
    }
    output[uint2(pc.regionX + p.x, pc.regionY + p.y)] = float4(result, 1.0);
}
