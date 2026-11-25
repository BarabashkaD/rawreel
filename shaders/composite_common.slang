// What rawreel's picture pass (composite.slang) and its grid prepass (grid_lines.slang) share: the
// push constants, the grid level words and the packed grid hit of one column or row.

module composite_common;

import dvv_media.view_lines;

using namespace dvv::media;

// levelShape, as render/composite_constants.h: stepLog2 in bits 0-3, the thickness in 4-11,
// adaptive 12, halo 13, drawn 14.
public static const uint LEVEL_STEP_MASK = 0xF;
public static const uint LEVEL_THICKNESS_SHIFT = 4;
public static const uint LEVEL_THICKNESS_MASK = 0xFF;
public static const uint LEVEL_ADAPTIVE = 1 << 12;
public static const uint LEVEL_HALO = 1 << 13;
public static const uint LEVEL_DRAWN = 1 << 14;

// = rawreel::CompositeConstants, 96 bytes. The pads are scalar words: a uint3 would align to 16
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
    public uint pad0;
    public uint pad1;
    public uint pad2;
};

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

// The grid hit of a column (row 0 of the line image, at x) or a row (row 1, at y): 0 for none,
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
