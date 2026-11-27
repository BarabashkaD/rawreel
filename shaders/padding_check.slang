// rawreel's padding check (level 1, the main view's padding marks): one thread per sample per
// component (z = the component). A sample whose own stored word, read from the Raw view through
// storedWordAddress, has bits in the word's padding mask counts once in the counter, and marks the
// tile of the P pixel at its block's centre with atomicMin(cy << 16 | cx) when that pixel lies in
// the image span of P. The tile map (the LayerBlock's tilesX x tilesY tiles of tileSize P
// pixels) holds 0xFFFFFFFF for none; the picture pass (picture_inspect.slang) draws the marks
// from it. A tile outside the map is never written.

import picture_common;
import layer_block;
import dvv_media.sample_grid;
import dvv_media.view_lines;

using namespace dvv::media;

// = rawreel::PaddingConstants (render/picture_pass.h), 64 bytes.
struct PaddingConstants
{
    LayerBlock* layers;  // 0: the stored-word layouts, tileSize and tilesX
    uint* rawBase;       // 8: the frame's stored bytes (the Raw view)
    uint* counter;       // 16: the readback slot's counter (its low 32 bits)
    uint* tiles;         // 24: the tile map
    int offsetX;         // 32: the view of P, as PushConstants
    int offsetY;
    float zoom;
    uint imageW;
    uint imageH;         // 48
    uint viewportW;
    uint viewportH;
    uint pad;
};

[[vk::push_constant]] ConstantBuffer<PaddingConstants> pc;

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    const uint k = id.z;
    if (k >= min(pc.layers->componentCount, 4u))
    {
        return;
    }
    const uint component = pc.layers->component[k];
    const uint log2SubX = component & 0xF;
    const uint log2SubY = (component >> LAYER_SUB_Y_SHIFT) & 0xF;
    // The component's grid: ceil(size / 2^sub) samples per axis.
    const uint samplesX = (pc.imageW + (1u << log2SubX) - 1u) >> log2SubX;
    const uint samplesY = (pc.imageH + (1u << log2SubY) - 1u) >> log2SubY;
    if (id.x >= samplesX || id.y >= samplesY)
    {
        return;
    }
    const StoredWordLayout layout = pc.layers->words[k];
    uint shift = 0u;
    uint padding = 0u;
    const uint64_t address = storedWordAddress(layout, id.xy, shift, padding);
    const uint word = readStoredWord(pc.rawBase, address, layout.wordBytes, layout.bigEndian);
    if ((word & padding) == 0u)
    {
        return;
    }
    uint before = 0u;
    InterlockedAdd(*pc.counter, 1u, before);
    // The block's centre in P: halfway between its first P pixel and the next block's.
    const uint4 block = blockOf(id.xy, log2SubX, log2SubY, uint2(pc.imageW, pc.imageH));
    const AxisView ax = axisOf(pc.offsetX, pc.zoom, pc.imageW, pc.viewportW);
    const AxisView ay = axisOf(pc.offsetY, pc.zoom, pc.imageH, pc.viewportH);
    const int cx = (firstPixelOf(ax, block.x) + firstPixelOf(ax, block.x + block.z)) / 2;
    const int cy = (firstPixelOf(ay, block.y) + firstPixelOf(ay, block.y + block.w)) / 2;
    const uint tileSize = pc.layers->tileSize;
    if (tileSize == 0u || !inSpan(imageSpan(ax), cx) || !inSpan(imageSpan(ay), cy))
    {
        return;
    }
    const uint tx = uint(cx) / tileSize;
    const uint ty = uint(cy) / tileSize;
    if (tx >= pc.layers->tilesX || ty >= pc.layers->tilesY)
    {
        return;  // never outside the tile map
    }
    const uint tile = ty * pc.layers->tilesX + tx;
    uint old = 0u;
    InterlockedMin(pc.tiles[tile], (uint(cy) << 16u) | uint(cx), old);
}
