#include <metal_stdlib>
using namespace metal;

struct PointLight
{
    packed_float3 position;
    float intensity;
    float4 color;
    float range;
    float decay;
    packed_float2 padding;
};

struct PointLightsB3
{
    PointLight Lights[8];
    int ActiveLightCount;
};

kernel void my_world_pointlights_b3_packing_probe(
    constant PointLightsB3& b3 [[buffer(5)]],
    device uint* out [[buffer(0)]])
{
    out[0] = uint(sizeof(PointLight));
    out[1] = uint(sizeof(PointLightsB3));
    out[2] = uint(b3.ActiveLightCount);

    out[10] = as_type<uint>(b3.Lights[0].position.x);
    out[11] = as_type<uint>(b3.Lights[0].position.y);
    out[12] = as_type<uint>(b3.Lights[0].position.z);
    out[13] = as_type<uint>(b3.Lights[0].intensity);
    out[14] = as_type<uint>(b3.Lights[0].color.x);
    out[15] = as_type<uint>(b3.Lights[0].color.y);
    out[16] = as_type<uint>(b3.Lights[0].color.z);
    out[17] = as_type<uint>(b3.Lights[0].color.w);
    out[18] = as_type<uint>(b3.Lights[0].range);
    out[19] = as_type<uint>(b3.Lights[0].decay);
    out[20] = as_type<uint>(b3.Lights[0].padding.x);
    out[21] = as_type<uint>(b3.Lights[0].padding.y);

    out[30] = as_type<uint>(b3.Lights[1].position.x);
    out[31] = as_type<uint>(b3.Lights[1].intensity);
    out[32] = as_type<uint>(b3.Lights[1].color.x);
    out[33] = as_type<uint>(b3.Lights[1].range);

    out[40] = as_type<uint>(b3.Lights[7].position.x);
    out[41] = as_type<uint>(b3.Lights[7].intensity);
    out[42] = as_type<uint>(b3.Lights[7].color.x);
    out[43] = as_type<uint>(b3.Lights[7].range);
}
