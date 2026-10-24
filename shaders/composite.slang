// rawreel's viewport compositor (design M3 v2 D3, D23, §4.4). One thread per viewport pixel: the
// display image (RGBA16F, linear light, BT.709 primaries) fitted into the viewport with the
// background around it, then the sRGB encode into the RGBA8 output, which the presenter blits 1:1.
// compositePixel() in src/render/composite_reference.cpp computes the same, in the same order.

static const uint FLAG_DARK = 1;
static const uint FLAG_SKIP_ENCODE = 2;
static const uint MAX_TAPS = 4;

struct PushConstants
{
    uint viewportW;
    uint viewportH;
    uint imageW;
    uint imageH;
    float zoom;
    float originX;
    float originY;
    uint flags;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

// Set 0, pushed per dispatch (M2 push descriptors). Bound images are writable for dvv-gpu; this
// shader only reads binding 0.
[[vk::binding(0, 0)]] [format("rgba16f")] RWTexture2D<float4> display;
[[vk::binding(1, 0)]] [format("rgba8")] RWTexture2D<float4> output;

// IEC 61966-2-1, as dvv::media::srgbEncode/srgbDecode (input clamped).
float srgbEncode(float x)
{
    x = clamp(x, 0.0, 1.0);
    return x <= 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}

float srgbDecode(float x)
{
    x = clamp(x, 0.0, 1.0);
    return x <= 0.04045 ? x / 12.92 : pow((x + 0.055) / 1.055, 2.4);
}

float3 texel(int x, int y)
{
    uint2 p = uint2(clamp(x, 0, int(pc.imageW) - 1), clamp(y, 0, int(pc.imageH) - 1));
    return display[p].rgb;
}

float3 mix3(float3 a, float3 b, float t)
{
    return a + t * (b - a);
}

// The 4-texel bilinear around (u - 0.5, v - 0.5), edges clamped.
float3 bilinear(float u, float v)
{
    float x = u - 0.5;
    float y = v - 0.5;
    float x0 = floor(x);
    float y0 = floor(y);
    float fx = x - x0;
    float fy = y - y0;
    int ix = int(x0);
    int iy = int(y0);
    float3 top = mix3(texel(ix, iy), texel(ix + 1, iy), fx);
    float3 bottom = mix3(texel(ix, iy + 1), texel(ix + 1, iy + 1), fx);
    return mix3(top, bottom, fy);
}

float3 sampleDisplay(float u, float v)
{
    if (pc.zoom >= 1.0)
    {
        return texel(int(floor(u)), int(floor(v)));
    }
    // n = min(MAX_TAPS, ceil(0.5 / zoom)), without a division (exact at zoom 0.5, 0.25, ...).
    uint n = 1;
    while (n < MAX_TAPS && float(n) * pc.zoom < 0.5)
    {
        n = n + 1;
    }
    float nf = float(n);
    float3 sum = float3(0.0, 0.0, 0.0);
    for (uint b = 0; b < n; b = b + 1)
    {
        float vb = v + ((float(b) + 0.5) / nf - 0.5) / pc.zoom;
        for (uint a = 0; a < n; a = a + 1)
        {
            float ua = u + ((float(a) + 0.5) / nf - 0.5) / pc.zoom;
            sum = sum + bilinear(ua, vb);
        }
    }
    return sum / float(n * n);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 id: SV_DispatchThreadID)
{
    if (id.x >= pc.viewportW || id.y >= pc.viewportH)
    {
        return;
    }
    float grey = srgbDecode((pc.flags & FLAG_DARK) != 0 ? 0.12 : 0.85);
    float3 colour = float3(grey, grey, grey);
    if (pc.imageW != 0 && pc.imageH != 0 && pc.zoom > 0.0)
    {
        float u = pc.originX + (float(id.x) + 0.5) / pc.zoom;
        float v = pc.originY + (float(id.y) + 0.5) / pc.zoom;
        if (u >= 0.0 && v >= 0.0 && u < float(pc.imageW) && v < float(pc.imageH))
        {
            colour = sampleDisplay(u, v);
        }
    }
    if ((pc.flags & FLAG_SKIP_ENCODE) == 0)
    {
        colour = float3(srgbEncode(colour.r), srgbEncode(colour.g), srgbEncode(colour.b));
    }
    output[id.xy] = float4(colour, 1.0);
}
