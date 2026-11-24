// rawreel's grid prepass: whether a column or a row of the picture region P holds a grid line
// depends on x alone or on y alone, so dvv-media's lineAt runs once per column and once per row
// here, and the picture pass (composite.slang) reads two packed hits per pixel. Thread (i, 0) is
// column i, thread (i, 1) row i. Rebuilt only when a grid level is drawn and its inputs change:
// every pc field read here must be a field of ViewportRenderer::LineInputs.

import composite_common;
import dvv_media.view_lines;

using namespace dvv::media;

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

// Set 0, pushed per dispatch: row 0 the columns' hits, row 1 the rows' (packHit).
[[vk::binding(0, 0)]] [format("r32ui")] RWTexture2D<uint> lines;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    const bool column = id.y == 0;
    const uint extent = column ? pc.viewportW : pc.viewportH;
    if (id.x >= extent || id.y > 1)
    {
        return;
    }
    const AxisView axis = column ? axisOf(pc.offsetX, pc.zoom, pc.imageW, pc.viewportW)
                                 : axisOf(pc.offsetY, pc.zoom, pc.imageH, pc.viewportH);
    LineLevel levels[LINE_LEVELS];
    for (uint l = 0; l < LINE_LEVELS; l = l + 1)
    {
        levels[l] = levelOf(pc.levelShape[l]);
    }
    lines[id.xy] = packHit(lineAt(axis, levels, int(id.x)));
}
