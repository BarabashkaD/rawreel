// rawreel's test pattern (design M1b §4.6): one thread per pixel writes sRGB-encoded RGBA8 code
// values (R in the low byte) to a buffer passed by device address. It must match
// rawreel::testPatternPixel() exactly.
struct PushConstants
{
    uint* pixels;  // width * height values, row by row (buffer device address)
    uint width;
    uint height;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

static const uint BAR = 191;  // 75 % of 255, rounded down

uint rgba(uint r, uint g, uint b)
{
    return r | (g << 8) | (b << 16) | (255u << 24);
}

uint patternPixel(uint x, uint y, uint width, uint height)
{
    if (x == 0 || y == 0 || x == width - 1 || y == height - 1)
    {
        return rgba(255, 255, 255);
    }
    if (y * 3 < height * 2)
    {
        switch (x * 7 / width)
        {
        case 0: return rgba(BAR, BAR, BAR);
        case 1: return rgba(BAR, BAR, 0);
        case 2: return rgba(0, BAR, BAR);
        case 3: return rgba(0, BAR, 0);
        case 4: return rgba(BAR, 0, BAR);
        case 5: return rgba(BAR, 0, 0);
        default: return rgba(0, 0, BAR);
        }
    }
    uint v = x * 255 / max(width - 1, 1u);
    return rgba(v, v, v);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    if (id.x < pc.width && id.y < pc.height)
    {
        pc.pixels[id.y * pc.width + id.x] = patternPixel(id.x, id.y, pc.width, pc.height);
    }
}
