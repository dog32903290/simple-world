#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut my_world_vertex(uint vertexID [[vertex_id]])
{
    float2 positions[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
    VertexOut out;
    out.position = float4(positions[vertexID], 0.0, 1.0);
    out.uv = positions[vertexID] * 0.5 + 0.5;
    return out;
}

static float4 blobSample(float2 uv)
{
    const float4 blobColor = float4(1, 0.12, 0.58, 1);
    const float4 blobBackground = float4(0, 0, 0, 0);
    float2 centered = (uv - float2(0.5, 0.5)) * 2.0;
    centered = float2(centered.x / float2(1.2, 0.72).x, centered.y / float2(1.2, 0.72).y);
    float distanceFromCenter = length(centered);
    float edge0 = max(0.0001, 0.58 * (1.0 - 0.34));
    float edge1 = max(edge0 + 0.0001, 0.58);
    float alpha = 1.0 - smoothstep(edge0, edge1, distanceFromCenter);
    return mix(blobBackground, blobColor, alpha);
}

fragment float4 constant_bg_fragment(VertexOut in [[stage_in]])
{
    const float4 constant_bg_color = float4(0.02, 0.04, 0.09, 1);
    return constant_bg_color;
}

fragment float4 blob_fg_fragment(VertexOut in [[stage_in]])
{
    return blobSample(in.uv);
}

fragment float4 blend_1_fragment(
    VertexOut in [[stage_in]],
    texture2d<float> constantTexture [[texture(0)]],
    texture2d<float> blobTexture [[texture(1)]],
    sampler textureSampler [[sampler(0)]])
{
    float4 constantColor = constantTexture.sample(textureSampler, in.uv);
    float4 blobColor = blobTexture.sample(textureSampler, in.uv);
    return mix(constantColor, blobColor, 0.65);
}

fragment float4 my_world_fragment(VertexOut in [[stage_in]])
{
    const float4 constant_bg_color = float4(0.02, 0.04, 0.09, 1);
    float4 constantColor = constant_bg_color;
    float4 blobColor = blobSample(in.uv);
    return mix(constantColor, blobColor, 0.65);
}
