// rawreel's marker pass: the markers of the layer block (LAYER_MARKERS, markers.slang) over the
// output picture.slang drew, so a main view with only the markers on keeps picture.slang. One
// thread per pixel of a part of P from P pixel (drawX, drawY), dispatched over markerRects
// (render/picture_pass.h) only; it writes the marker pixels and leaves every other pixel as the
// picture pass drew it, so the output equals the inspect pass's, which draws the markers last.

import picture_common;
import layer_block;
import markers;
import dvv_media.view_lines;

using namespace dvv::media;

// = rawreel::InspectConstants (render/picture_pass.h), 128 bytes; this pass reads the picture's
// constants and the layer block only.
struct MarkerConstants
{
    PushConstants picture;  // 0
    LayerBlock* layers;     // 96
    uint64_t rawBase;       // 104: unused
    uint64_t readback;      // 112: unused
    uint64_t pad;           // 120: std430 rounds the block to 16 bytes
};

[[vk::push_constant]] ConstantBuffer<MarkerConstants> mc;

// Set 0, pushed per dispatch: the output only.
[[vk::binding(0, 0)]] [format("rgba8")] RWTexture2D<float4> output;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    const PushConstants pc = mc.picture;
    const uint2 p = uint2(id.x + pc.drawX, id.y + pc.drawY);
    if (p.x >= pc.viewportW || p.y >= pc.viewportH)
    {
        return;
    }
    const AxisView ax = axisOf(pc.offsetX, pc.zoom, pc.imageW, pc.viewportW);
    const AxisView ay = axisOf(pc.offsetY, pc.zoom, pc.imageH, pc.viewportH);
    float3 marker;
    if (markerAt(mc.layers, ax, ay, int(p.x), int(p.y), marker))
    {
        const bool skip = (pc.flags & FLAG_SKIP_ENCODE) != 0;
        output[uint2(pc.regionX + p.x, pc.regionY + p.y)] =
            float4(skip ? decode3(marker) : marker, 1.0);
    }
}
