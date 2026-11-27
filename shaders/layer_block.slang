// = rawreel::LayerBlock (render/layer_block.h), 432 bytes: the inspect pipeline's layer block,
// read by device address. Every uint4 starts at a multiple of 16 and StoredWordLayout's arrays are
// uint[], so the layout is the same in std430 and scalar blocks. The rawreel_render target exports
// this file as `rawreel.layer_block` (tests/shaders/layer_block_probe.slang reads every field).

import dvv_media.sample_grid;

using namespace dvv::media;

// LayerBlock.flags, as render/layer_block.h.
public static const uint LAYER_PLANE_SOURCE = 1;
public static const uint LAYER_VALUES = 2;
public static const uint LAYER_CELL_LINES = 4;
public static const uint LAYER_CHROMA = 8;
public static const uint LAYER_CELL_FLAGS = 16;
public static const uint LAYER_PADDING_MARKS = 32;
public static const uint LAYER_MARKERS = 64;
public static const uint LAYER_TEST_CODES = 128;

// LayerBlock.component[k]: log2SubX in bits 0-3, log2SubY in 4-7, bits in 8-15, the letter's
// cell in 16-31.
public static const uint LAYER_SUB_Y_SHIFT = 4;
public static const uint LAYER_BITS_SHIFT = 8;
public static const uint LAYER_LETTER_SHIFT = 16;

public struct LayerBlock
{
    public StoredWordLayout words[4];  // per component
    public uint flags;                 // 288
    public uint sourceComponent;       // the plane of a Planes panel
    public uint componentCount;
    public uint markSize;
    public uint4 component;    // 304
    public uint4 metrics;      // 320
    public uint4 reserved;     // 336: unused
    public uint4 colours0;     // 352: cell line, chroma outline, label text, label box
    public uint warning;       // 368
    public uint marker;
    public uint markRim;
    public uint tileSize;
    public uint4 pinRect;      // 384
    public uint4 outlineRect;  // 400
    public uint tilesX;        // 416: the tile map of the padding marks
    public uint tilesY;
    public uint markerRim;     // 424: the markers' rim colour (markRim: the padding marks' rim)
    public uint pad0;
};

// A layer colour word (render/layer_block.h: r | g << 8 | b << 16 | a << 24, sRGB-encoded): its
// encoded colour and its opacity.
public float3 layerColour(uint rgba)
{
    return float3(float(rgba & 0xFF), float((rgba >> 8) & 0xFF), float((rgba >> 16) & 0xFF)) /
           255.0;
}
public float layerOpacity(uint rgba) { return float(rgba >> 24) / 255.0; }
