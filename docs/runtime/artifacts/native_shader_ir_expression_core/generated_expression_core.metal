#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

struct ExpressionUniforms {
    float u_time;
    float u_intensity;
};

vertex VertexOut expression_core_vertex(uint vertexID [[vertex_id]])
{
    float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VertexOut out;
    out.position = float4(positions[vertexID], 0.0, 1.0);
    out.uv = positions[vertexID] * 0.5 + 0.5;
    return out;
}

fragment float4 expression_core_fragment(VertexOut in [[stage_in]], constant ExpressionUniforms& uniforms [[buffer(0)]])
{
    return float4(mix(float3(0.04, 0.1, 0.8), float3(1, 0.2, 0.48), smoothstep(0.15, 0.95, ((in.uv).x + (sin(uniforms.u_time) * uniforms.u_intensity)))), 1);
}
