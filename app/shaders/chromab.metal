// ChromaticAbberation: TiXL-ported radial chromatic fringe filter, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/ChromaticAbberation.hlsl.
// Samples the input texture N times in a loop, splitting R and B channels in opposite radial
// directions to simulate lens chromatic aberration. G and A channels are split at half-offset.
// Also supports a barrel lens distortion pre-warp (Distort).
//
// Fork (named, DX11->Metal): HLSL `int samples = clamp(SampleCount, 3, 20)` kept as float
// in the params struct and cast in the shader; HLSL SV_POSITION-based screen position unused
// (we compute from texCoord directly). Sampler: fixed linear+clamp (TiXL host provides
// texSampler without an explicit wrap override; clamp matches fringe edge behavior).
#include <metal_stdlib>
#include "chromab_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut chromab_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);
  return o;
}

// Mirror of ChromaticAbberation.hlsl psMain.
fragment float4 chromab_fs(VSOut in [[stage_in]],
                           texture2d<float> Image    [[texture(0)]],
                           sampler texSampler        [[sampler(0)]],
                           constant ChromaBAParams& P [[buffer(CHROMAB_Params)]],
                           constant ChromaBAResolution& R [[buffer(CHROMAB_Resolution)]]) {
  float2 uv = in.texCoord;

  // TiXL: slight inward scale + lens distortion barrel warp.
  uv -= 0.5f;
  uv *= 0.95f;
  uv += 0.5f;

  float2 dir = uv - 0.5f;
  uv += dir * dot(dir, dir) * P.Distort;

  float4 col = float4(0.0f);
  int samples = clamp((int)P.SampleCount, 3, 20);

  float4 centerColor = Image.sample(texSampler, uv);
  float2 offset = float2(1.0f, 1.0f) * 0.01f * dir * P.Size;

  float x = 0.0f;
  for (int i = 1; i < samples; i++) {
    float f = ((float)i - 0.5f) / (float)samples;
    x += 0.5f;

    float4 left  = Image.sample(texSampler, uv - offset * f);
    float4 right = Image.sample(texSampler, uv + offset * f);
    col.r += left.r  * 0.5f;
    col.b += right.b * 0.5f;

    float4 left2  = Image.sample(texSampler, uv - offset * f / 2.0f);
    float4 right2 = Image.sample(texSampler, uv + offset * f / 2.0f);
    col.ga += left2.ga  * 0.25f;
    col.ga += right2.ga * 0.25f;
  }
  if (x > 0.0f) col.rgb /= x;

  float3 blended = mix(centerColor.rgb, col.rgb, P.Strength);
  return clamp(float4(blended, col.a), 0.0f, float4(1000.0f, 1000.0f, 1000.0f, 1.0f));
}
