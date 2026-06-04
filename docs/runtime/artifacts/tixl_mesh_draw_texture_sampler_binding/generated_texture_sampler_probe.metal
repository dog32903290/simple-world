#include <metal_stdlib>
using namespace metal;

static uint pack_rgba8(float4 color)
{
    uint4 bytes = uint4(round(clamp(color, 0.0, 1.0) * 255.0));
    return (bytes.r << 24) | (bytes.g << 16) | (bytes.b << 8) | bytes.a;
}

kernel void texture_sampler_probe(
    texture2d<float, access::sample> BaseColorMap [[texture(2)]],
    texture2d<float, access::sample> BRDFLookup [[texture(7)]],
    sampler WrappedSampler [[sampler(0)]],
    sampler ClampedSampler [[sampler(1)]],
    device uint* outWords [[buffer(0)]])
{
    outWords[0] = pack_rgba8(BaseColorMap.sample(WrappedSampler, float2(0.25, 0.25)));
    outWords[1] = pack_rgba8(BRDFLookup.sample(ClampedSampler, float2(0.75, 0.25)));
    outWords[2] = pack_rgba8(BaseColorMap.sample(WrappedSampler, float2(-0.25, 0.25)));
    outWords[3] = pack_rgba8(BRDFLookup.sample(ClampedSampler, float2(-0.25, 0.25)));
}
