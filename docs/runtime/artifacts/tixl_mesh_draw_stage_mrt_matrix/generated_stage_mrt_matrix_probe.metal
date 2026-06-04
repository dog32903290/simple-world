#include <metal_stdlib>
using namespace metal;

struct VertexOut
{
    float4 position [[position]];
    float stageSentinel;
    float matrixSentinel;
};

struct FragmentOut
{
    uint4 color [[color(0)]];
    uint4 normal [[color(1)]];
};

static float4 my_world_mul_vector_matrix(float4 v, float4x4 m)
{
    return float4(
        dot(v, float4(m[0].x, m[1].x, m[2].x, m[3].x)),
        dot(v, float4(m[0].y, m[1].y, m[2].y, m[3].y)),
        dot(v, float4(m[0].z, m[1].z, m[2].z, m[3].z)),
        dot(v, float4(m[0].w, m[1].w, m[2].w, m[3].w)));
}

vertex VertexOut my_world_stage_mrt_matrix_vertex(uint vertexId [[vertex_id]])
{
    const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0)
    };
    float4x4 matrix = float4x4(
        float4( 1.0,  2.0,  3.0,  4.0),
        float4( 5.0,  6.0,  7.0,  8.0),
        float4( 9.0, 10.0, 11.0, 12.0),
        float4(13.0, 14.0, 15.0, 16.0));
    float4 product = my_world_mul_vector_matrix(float4(1.0, 2.0, 3.0, 4.0), matrix);

    VertexOut out;
    out.position = float4(positions[vertexId], 0.0, 1.0);
    out.stageSentinel = 13.0;
    out.matrixSentinel = product.x; // 90 for row-vector * matrix convention.
    return out;
}

fragment FragmentOut my_world_stage_mrt_matrix_fragment(VertexOut in [[stage_in]])
{
    FragmentOut out;
    out.color = uint4(uint(round(in.stageSentinel)), uint(round(in.matrixSentinel)), 111u, 255u);
    out.normal = uint4(31u, 37u, 41u, 255u);
    return out;
}
