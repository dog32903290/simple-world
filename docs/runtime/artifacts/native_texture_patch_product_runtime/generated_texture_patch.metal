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
    float2 driftUv = clamp(in.uv + float2(0.012, -0.008), float2(0.0), float2(1.0));
    float4 historyColor = feedbackHistory.sample(textureSampler, driftUv);
    return clamp(nowColor * 0.58 + historyColor * 0.72, 0.0, 1.0);
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
