#include <metal_stdlib>
using namespace metal;

static uint pack_rgba8(float4 color)
{
    uint4 bytes = uint4(round(clamp(color, 0.0, 1.0) * 255.0));
    return (bytes.r << 24) | (bytes.g << 16) | (bytes.b << 8) | bytes.a;
}

kernel void full_pbr_resource_binding_probe(
    device const uint* PbrVertices [[buffer(0)]],
    device const uint* FaceIndices [[buffer(1)]],
    constant uint& b0 [[buffer(2)]],
    constant uint& b1 [[buffer(3)]],
    constant uint& b2 [[buffer(4)]],
    constant uint& b3 [[buffer(5)]],
    constant uint& b4 [[buffer(6)]],
    constant uint& b5 [[buffer(7)]],
    texture2d<float, access::sample> BaseColorMap [[texture(2)]],
    texture2d<float, access::sample> EmissiveColorMap [[texture(3)]],
    texture2d<float, access::sample> RSMOMap [[texture(4)]],
    texture2d<float, access::sample> NormalMap [[texture(5)]],
    texturecube<float, access::sample> PrefilteredSpecular [[texture(6)]],
    texture2d<float, access::sample> BRDFLookup [[texture(7)]],
    sampler WrappedSampler [[sampler(0)]],
    sampler ClampedSampler [[sampler(1)]],
    device uint* out [[buffer(8)]])
{
    out[0] = PbrVertices[0];
    out[1] = FaceIndices[0];
    out[2] = b0;
    out[3] = b1;
    out[4] = b2;
    out[5] = b3;
    out[6] = b4;
    out[7] = b5;
    out[8] = pack_rgba8(BaseColorMap.sample(WrappedSampler, float2(0.25, 0.25)));
    out[9] = pack_rgba8(EmissiveColorMap.sample(WrappedSampler, float2(0.25, 0.25)));
    out[10] = pack_rgba8(RSMOMap.sample(WrappedSampler, float2(0.25, 0.25)));
    out[11] = pack_rgba8(NormalMap.sample(WrappedSampler, float2(0.25, 0.25)));
    out[12] = pack_rgba8(PrefilteredSpecular.sample(WrappedSampler, float3(1.0, 0.0, 0.0), level(0.0)));
    out[13] = pack_rgba8(BRDFLookup.sample(ClampedSampler, float2(0.25, 0.25)));
    out[14] = pack_rgba8(BaseColorMap.sample(WrappedSampler, float2(-0.25, 0.25)));
    out[15] = pack_rgba8(BRDFLookup.sample(ClampedSampler, float2(1.25, 0.25)));
}
