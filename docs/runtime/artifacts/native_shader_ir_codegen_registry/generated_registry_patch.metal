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

fragment float4 constant_bg_fragment(VertexOut in [[stage_in]])
{
    const float4 color = float4(0.020000, 0.040000, 0.090000, 1.000000);
    return color;
}

fragment float4 blob_fg_fragment(VertexOut in [[stage_in]])
{
    const float4 blobColor = float4(1.000000, 0.120000, 0.580000, 1.000000);
    const float4 background = float4(0.000000, 0.000000, 0.000000, 0.000000);
    float2 centered = (in.uv - float2(0.5, 0.5)) * 2.0;
    centered = float2(centered.x / float2(1.200000, 0.720000).x, centered.y / float2(1.200000, 0.720000).y);
    float distanceFromCenter = length(centered);
    float edge0 = max(0.0001, 0.580000 * (1.0 - 0.340000));
    float edge1 = max(edge0 + 0.0001, 0.580000);
    float alpha = 1.0 - smoothstep(edge0, edge1, distanceFromCenter);
    return mix(background, blobColor, alpha);
}

fragment float4 blend_1_fragment(
    VertexOut in [[stage_in]],
    texture2d<float> constantTexture [[texture(0)]],
    texture2d<float> blobTexture [[texture(1)]],
    sampler textureSampler [[sampler(0)]])
{
    float4 constantColor = constantTexture.sample(textureSampler, in.uv);
    float4 blobColor = blobTexture.sample(textureSampler, in.uv);
    return mix(constantColor, blobColor, 0.650000);
}

fragment float4 gradient_1_fragment(VertexOut in [[stage_in]])
{
    const float4 startColor = float4(0.040000, 0.100000, 0.800000, 1.000000);
    const float4 endColor = float4(1.000000, 0.200000, 0.480000, 1.000000);
    float diagonal = clamp((in.uv.x + in.uv.y) * 0.5, 0.0, 1.0);
    return mix(startColor, endColor, diagonal);
}

fragment float4 feedback_1_fragment(
    VertexOut in [[stage_in]],
    texture2d<float> gradientTexture [[texture(0)]],
    texture2d<float> feedbackHistory [[texture(1)]],
    sampler textureSampler [[sampler(0)]])
{
    float4 nowColor = gradientTexture.sample(textureSampler, in.uv);
    float4 historyColor = feedbackHistory.sample(textureSampler, clamp(in.uv + float2(0.012, -0.008), float2(0.0), float2(1.0)));
    return clamp(nowColor * 0.580000 + historyColor * 0.720000, 0.0, 1.0);
}

fragment float4 render_target_1_fragment(
    VertexOut in [[stage_in]],
    texture2d<float> feedbackTexture [[texture(0)]],
    sampler textureSampler [[sampler(0)]])
{
    float4 color = feedbackTexture.sample(textureSampler, in.uv);
    float vignette = smoothstep(0.9, 0.15, distance(in.uv, float2(0.5, 0.5)));
    color.rgb = color.rgb * (0.55 + 0.45 * vignette);
    color.a = 1.0;
    return color;
}
