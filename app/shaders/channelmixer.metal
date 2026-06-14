// ChannelMixer: TiXL-ported channel matrix mix filter, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/MixChannels.hlsl.
//
// MixChannels.hlsl logic (verbatim port to Metal):
//   float4 col = InputTexture.Sample(texSampler, uv);
//   r = dot(clamp(col, 0, 99999999), float4(MR.r, MG.r, MB.r, MA.r)) + Add.r
//   g = dot(clamp(col, 0, 99999999), float4(MR.g, MG.g, MB.g, MA.g)) + Add.g
//   b = dot(clamp(col, 0, 99999999), float4(MR.b, MG.b, MB.b, MA.b)) + Add.b
//   a = dot(clamp(col, 0, 99999999), float4(MR.a, MG.a, MB.a, MA.a)) + Add.a
//   ClampResult > 0.5 ? float4(clamp(rgb,0,10000), clamp(a,0.0001,1)) : float4(r,g,b,a)
// Note: TiXL clamps input to [0, 99999999] before the dot (prevents negative contribution
// from HDR negative channels while allowing large positive values). We replicate this.
//
// Fork (named, DX11->Metal):
//   - DX11 uses pixel shader (VS+PS pipeline); Metal uses same fullscreen-triangle approach.
//   - No sampler mode exposed (fixed linear+clamp, same as Tint/AdjustColors fork class).
//   - GenerateMipmaps: TiXL host post-process. We skip mip generation (simple fork, noted).
//   - TiXL's upper clamp in ClampResult path is 10000 for RGB, 1 for alpha (alpha range differs
//     from HLSL 0.0001 lower-bound — replicated verbatim).
#include <metal_stdlib>
#include "channelmixer_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut channelmixer_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

// Mirror of MixChannels.hlsl psMain.
fragment float4 channelmixer_fs(VSOut in [[stage_in]],
                                texture2d<float> inputTex [[texture(0)]],
                                sampler samLinear          [[sampler(0)]],
                                constant ChannelMixerParams& P [[buffer(CHANNELMIXER_Params)]]) {
  float2 uv = in.texCoord;
  float4 col = inputTex.sample(samLinear, uv);

  // TiXL: clamp(col, 0, 99999999) before the matrix dot (prevents negative input contribution).
  float4 c = clamp(col, 0.0f, 99999999.0f);

  // TiXL MixChannels.hlsl: each output channel = dot of col vs the column of the 4x4 matrix.
  // MultiplyR row -> contributions of src R/G/B/A to output R; etc.
  float r = dot(c, float4(P.MultiplyRr, P.MultiplyGr, P.MultiplyBr, P.MultiplyAr)) + P.AddR;
  float g = dot(c, float4(P.MultiplyRg, P.MultiplyGg, P.MultiplyBg, P.MultiplyAg)) + P.AddG;
  float b = dot(c, float4(P.MultiplyRb, P.MultiplyGb, P.MultiplyBb, P.MultiplyAb)) + P.AddB;
  float a = dot(c, float4(P.MultiplyRa, P.MultiplyGa, P.MultiplyBa, P.MultiplyAa)) + P.AddA;

  // TiXL ClampResult: clamp rgb to [0,10000], alpha to [0.0001,1]; else pass through.
  if (P.ClampResult > 0.5f) {
    return float4(clamp(float3(r, g, b), 0.0f, 10000.0f), clamp(a, 0.0001f, 1.0f));
  }
  return float4(r, g, b, a);
}
