// rawreel's picture pass. One thread per pixel of the part of P it draws, from P pixel (drawX,
// drawY) (the part right of and below the hugged rect's top-left): the display image (RGBA16F,
// linear light, BT.709 primaries) shown with the background around it (with FLAG_ALPHA: the
// picture premultiplied per texel and blended over a checkerboard), the grid drawn over it, then
// the sRGB encode into the RGBA8 output, which the presenter blits 1:1. Viewport pixel p shows the
// image when it lies in dvv-media's imageSpan on both axes and samples at its pixel centre,
// imageCoordinateAt; the grid lines come from lineAt, run once per column and per row by the grid
// prepass (picture_lines.slang), so the picture and the lines share one rule. compositePixel() in
// tests/composite_reference.cpp computes the same, in the same order.

import picture_common;
import dvv_media.view_lines;
import viewport_background;

using namespace dvv::media;

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

// Set 0, pushed per dispatch (M2 push descriptors). Bound images are writable for dvv-gpu; this
// shader only reads bindings 0 and 2. `lines` holds the grid prepass's hits (packHit), written
// only when a grid level is drawn.
[[vk::binding(0, 0)]] [format("rgba16f")] RWTexture2D<float4> display;
[[vk::binding(1, 0)]] [format("rgba8")] RWTexture2D<float4> output;
[[vk::binding(2, 0)]] [format("r32ui")] RWTexture2D<uint> lines;

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
    float3 result = skip ? colour : encode3(colour);
    if (inside && gridDrawn(pc))
    {
        // The prepass's lineAt of this column and of this row.
        const LineHit hit =
            winner(unpackHit(lines[uint2(p.x, 0)]), unpackHit(lines[uint2(p.y, 1)]));
        if (hit.hit)
        {
            const float3 blended = blendLine(pc, skip ? encode3(colour) : result, hit);
            result = skip ? decode3(blended) : blended;
        }
    }
    output[uint2(pc.regionX + p.x, pc.regionY + p.y)] = float4(result, 1.0);
}
