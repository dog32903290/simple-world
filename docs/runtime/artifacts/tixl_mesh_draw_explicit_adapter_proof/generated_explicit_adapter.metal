#include <metal_stdlib>
using namespace metal;

struct AdapterVertexOut
{
    float4 position [[position]];
    float2 adapterUv;
};

vertex AdapterVertexOut my_world_explicit_adapter_vertex(uint vertexId [[vertex_id]])
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };

    AdapterVertexOut out;
    out.position = float4(positions[vertexId], 0.0, 1.0);
    out.adapterUv = positions[vertexId] * 0.5 + 0.5;
    return out;
}

fragment uint4 my_world_explicit_adapter_fragment(AdapterVertexOut in [[stage_in]])
{
    const uint x = uint(clamp(in.position.x, 0.0, 255.0));
    const uint y = uint(clamp(in.position.y, 0.0, 255.0));
    return uint4(17u + x, 113u + y, 191u + x + y, 255u);
}
