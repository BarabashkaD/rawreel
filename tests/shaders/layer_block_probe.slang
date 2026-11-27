// Copies every field of the layer block, read by name, into word (offset / 4) of an R32_UINT
// image: the GPU half of "layer block: the shader reads every field at the C++ offsets"
// (gpu_test_inspect.cpp).
import rawreel.layer_block;
import dvv_media.sample_grid;

using namespace dvv::media;

struct ProbePush
{
    LayerBlock* block;
};

[[vk::push_constant]] ConstantBuffer<ProbePush> pc;

// 108 x 1: word i is the uint at byte 4 * i of the block.
[[vk::binding(0, 0)]] [format("r32ui")] RWTexture2D<uint> words;

void put(uint index, uint value)
{
    words[uint2(index, 0u)] = value;
}

void put4(uint index, uint4 value)
{
    put(index, value.x);
    put(index + 1u, value.y);
    put(index + 2u, value.z);
    put(index + 3u, value.w);
}

[shader("compute")]
[numthreads(1, 1, 1)]
void main()
{
    const LayerBlock block = *pc.block;
    for (uint k = 0u; k < 4u; ++k)
    {
        const StoredWordLayout layout = block.words[k];
        const uint at = 18u * k;  // 72 bytes per layout
        put(at, uint(layout.planeOffset & 0xFFFFFFFFull));
        put(at + 1u, uint(layout.planeOffset >> 32ull));
        put(at + 2u, layout.stride);
        put(at + 3u, layout.groupBytes);
        put(at + 4u, layout.wordBytes);
        put(at + 5u, layout.bigEndian);
        put(at + 6u, layout.bits);
        put(at + 7u, layout.samplesPerGroup);
        for (uint i = 0u; i < 3u; ++i)
        {
            put(at + 8u + i, layout.slots[i]);
        }
        for (uint i = 0u; i < 6u; ++i)
        {
            put(at + 11u + i, layout.paddingMasks[i]);
        }
        put(at + 17u, layout.pad);
    }
    put4(72u, uint4(block.flags, block.sourceComponent, block.componentCount, block.markSize));
    put4(76u, block.component);
    put4(80u, block.metrics);
    put4(84u, block.reserved);
    put4(88u, block.colours0);
    put4(92u, uint4(block.warning, block.marker, block.markRim, block.tileSize));
    put4(96u, block.pinRect);
    put4(100u, block.outlineRect);
    put4(104u, uint4(block.tilesX, block.tilesY, block.markerRim, block.pad0));
}
