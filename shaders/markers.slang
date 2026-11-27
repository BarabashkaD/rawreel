// The markers of the layer block (LAYER_MARKERS: pinRect, outlineRect), shared by the inspect pass
// (picture_inspect.slang, which draws them last) and the marker pass (picture_markers.slang, which
// draws them over picture.slang's output).

import layer_block;
import dvv_media.view_lines;

using namespace dvv::media;

// The markers (LAYER_MARKERS), as render/picture_pass.cpp's markerRects: the pixels of image pixels
// [first, end) on `axis` (pinRect, outlineRect; end <= first: no marker), cursorSpan's rule
// unclipped, the ends kept in the image.
bool markerSpan(AxisView axis, uint first, uint end, out int begin, out int stop)
{
    begin = 0;
    stop = 0;
    if (end <= first || axis.size == 0u || !(axis.zoom > 0.0))
    {
        return false;
    }
    const uint from = min(first, axis.size - 1u);
    const uint to = max(from + 1u, min(end, axis.size));
    begin = min(firstPixelOf(axis, from), firstPixelOf(axis, axis.size) - 1);
    stop = max(begin + 1, firstPixelOf(axis, to));
    return true;
}

bool inRange(int p, int begin, int end) { return p >= begin && p < end; }

// The marker colour of P pixel (px, py) in block `layers`, encoded; false where no marker draws.
// The pin: a span of at least 6 px on both axes gets a 2 px box outside it with a 1 px dark rim
// outside the box, a smaller one a 6 x 6 dot around its centre with a 1 px rim. The outline: a
// 2 px line outside its region, dashed 4 / 4 from the line's corner along each edge. The pin
// draws over the outline.
public bool markerAt(LayerBlock* layers, AxisView ax, AxisView ay, int px, int py,
                     out float3 colour)
{
    colour = float3(0.0, 0.0, 0.0);
    const uint4 pin = layers->pinRect;
    int x0, x1, y0, y1;
    if (markerSpan(ax, pin.x, pin.z, x0, x1) && markerSpan(ay, pin.y, pin.w, y0, y1))
    {
        int inner = 0;  // the span itself (box) or the dot, then the marker and the rim around it
        int mark = 2;
        if (min(x1 - x0, y1 - y0) < 6)
        {
            const int cx = x0 + (x1 - x0) / 2;
            const int cy = y0 + (y1 - y0) / 2;
            x0 = cx;
            x1 = cx;
            y0 = cy;
            y1 = cy;
            mark = 3;
            inner = -1;  // the dot has no hole
        }
        if (inRange(px, x0 - mark - 1, x1 + mark + 1) && inRange(py, y0 - mark - 1, y1 + mark + 1))
        {
            const bool hole = inner == 0 && inRange(px, x0, x1) && inRange(py, y0, y1);
            if (!hole)
            {
                const bool marked =
                    inRange(px, x0 - mark, x1 + mark) && inRange(py, y0 - mark, y1 + mark);
                colour = layerColour(marked ? layers->marker : layers->markerRim);
                return true;
            }
        }
    }
    const uint4 outline = layers->outlineRect;
    if (markerSpan(ax, outline.x, outline.z, x0, x1) &&
        markerSpan(ay, outline.y, outline.w, y0, y1))
    {
        const int line = 2;
        if (inRange(px, x0 - line, x1 + line) && inRange(py, y0 - line, y1 + line) &&
            !(inRange(px, x0, x1) && inRange(py, y0, y1)))
        {
            // Along a horizontal edge (the corners included) or a vertical one.
            const bool across = py < y0 || py >= y1;
            const int t = across ? px - (x0 - line) : py - (y0 - line);
            if (((t / 4) & 1) == 0)
            {
                colour = layerColour(layers->marker);
                return true;
            }
        }
    }
    return false;
}
