#include <metal_stdlib>
using namespace metal;

kernel void my_world_texturecube_pbr_reference_probe(
    device uint* out [[buffer(0)]],
    texturecube<float, access::sample> prefilteredSpecular [[texture(3)]],
    sampler cubeSampler [[sampler(2)]])
{
    float4 sampled0 = prefilteredSpecular.sample(cubeSampler, float3(1.0, 0.0, 0.0), level(0.0));
    float4 sampled1 = prefilteredSpecular.sample(cubeSampler, float3(1.0, 0.0, 0.0), level(1.0));

    out[0] = prefilteredSpecular.get_width(0);
    out[1] = prefilteredSpecular.get_height(0);
    out[2] = prefilteredSpecular.get_num_mip_levels();
    out[3] = prefilteredSpecular.get_width(1);
    out[4] = prefilteredSpecular.get_height(1);
    out[5] = uint(round(clamp(sampled0.r, 0.0, 1.0) * 255.0));
    out[6] = uint(round(clamp(sampled0.g, 0.0, 1.0) * 255.0));
    out[7] = uint(round(clamp(sampled0.b, 0.0, 1.0) * 255.0));
    out[8] = uint(round(clamp(sampled0.a, 0.0, 1.0) * 255.0));
    out[9] = uint(round(clamp(sampled1.r, 0.0, 1.0) * 255.0));
    out[10] = uint(round(clamp(sampled1.g, 0.0, 1.0) * 255.0));
    out[11] = uint(round(clamp(sampled1.b, 0.0, 1.0) * 255.0));
    out[12] = uint(round(clamp(sampled1.a, 0.0, 1.0) * 255.0));
}
