// rawreel's grid prepass: whether a column or a row of the picture region P holds a grid line
// depends on x alone or on y alone, so dvv-media's lineAt runs once per column and once per row
// here, and the picture pass (picture.slang) reads two packed hits per pixel. Thread (i, 0) is
// column i, thread (i, 1) row i. Rebuilt only when a grid level is drawn and its inputs change:
// every pc field read here must be a field of LineInputs (render/picture_pass.h).

import picture_common;
import dvv_media.view_lines;

using namespace dvv::media;

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

// Set 0, pushed per dispatch: row 0 the columns' hits, row 1 the rows' (packHit), rows 2 and 3 the
// same for the cell levels (thread (i, 2) a column, (i, 3) a row).
[[vk::binding(0, 0)]] [format("r32ui")] RWTexture2D<uint> lines;

[shader("compute")]
[numthreads(64, 1, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    const bool column = (id.y & 1u) == 0u;
    const bool cells = id.y >= 2u;
    const uint extent = column ? pc.viewportW : pc.viewportH;
    if (id.x >= extent || id.y > 3)
    {
        return;
    }
    const AxisView axis = column ? axisOf(pc.offsetX, pc.zoom, pc.imageW, pc.viewportW)
                                 : axisOf(pc.offsetY, pc.zoom, pc.imageH, pc.viewportH);
    LineLevel levels[LINE_LEVELS];
    for (uint l = 0; l < LINE_LEVELS; l = l + 1)
    {
        levels[l] = levelOf(cells ? 0u : pc.levelShape[l]);
    }
    if (cells)
    {
        // Rows 2 and 3, read by the inspect pipeline only: level 0 the chroma outline, level 1
        // the cell lines.
        levels[0] = levelOf(column ? pc.cellChromaX : pc.cellChromaY);
        levels[1] = levelOf(pc.cellLine);
    }
    lines[id.xy] = packHit(lineAt(axis, levels, int(id.x)));
}
