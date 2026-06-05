#include <metal_stdlib>
using namespace metal;

struct Out {
    float4 position [[position]];
};

vertex Out my_world_native_backend_vertex(uint vertexId [[vertex_id]])
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };
    Out out;
    out.position = float4(positions[vertexId], 0.0, 1.0);
    return out;
}

fragment uint4 my_world_native_backend_fragment(Out in [[stage_in]])
{
    uint x = uint(clamp(in.position.x, 0.0, 15.0));
    uint y = uint(clamp(in.position.y, 0.0, 15.0));
    return uint4(68u + x, 62u + y, 54u + x + y, 255u);
}
