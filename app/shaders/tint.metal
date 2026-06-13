// Tint: TiXL-ported color tint/remap filter, single pass.
// Faithful port of external/tixl Operators/Lib/Assets/shaders/img/fx/Tint.hlsl.
// Reads inputTexture, pre-multiplies rgb by Exposure, computes luminance-like t via a
// weighted dot with ChannelWeights, remaps t through GainAndBias, lerps MapBlackTo..MapWhiteTo
// to get `mapped`, then blends original <-> mapped by Amount.
//
// Fork (named, DX11->Metal): Tint.hlsl includes "shared/bias-functions.hlsl" for
// ApplyGainAndBias(float, float2). We inline that exact logic here so no HLSL include is
// needed. The function computes GetBias(b, GetSchlickBias(g, x)) or the swapped variant
// depending on g < 0.5 — reproduced verbatim from bias-functions.hlsl (no drift).
// Sampler: fixed linear+clamp (Tint.t3 host connects texSampler without an explicit address-
// mode selection; clamp is the Metal default and matches MirrorOnce for interior UVs).
#include <metal_stdlib>
#include "tint_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id (no vertex buffer): covers the viewport, texCoord 0..1.
vertex VSOut tint_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC up vs texture down
  return o;
}

// Inlined from bias-functions.hlsl: GetBias(bias, x).
static inline float _getBias(float bias, float x) {
  return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}
// Inlined from bias-functions.hlsl: GetSchlickBias(g, x).
static inline float _schlick(float g, float x) {
  if (x < 0.5f) {
    x *= 2.0f;
    return 0.5f * _getBias(g, x);
  } else {
    x = 2.0f * x - 1.0f;
    return 0.5f * _getBias(1.0f - g, x) + 0.5f;
  }
}
// Inlined from bias-functions.hlsl: ApplyGainAndBias(value, float2 gainBias).
// gainBias.x = gain (schlick), gainBias.y = bias.
static inline float _applyGainAndBias(float value, float2 gb) {
  float g = saturate(gb.x);
  float b = saturate(gb.y);
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) {
    value = _getBias(b, value);
    value = _schlick(g, value);
  } else {
    value = _schlick(g, value);
    value = _getBias(b, value);
  }
  return value;
}

// Mirror of Tint.hlsl psMain.
fragment float4 tint_fs(VSOut in [[stage_in]],
                        texture2d<float> inputTex [[texture(0)]],
                        sampler samLinear          [[sampler(0)]],
                        constant TintParams& P     [[buffer(TINT_Params)]]) {
  float2 uv = in.texCoord;
  float4 c = inputTex.sample(samLinear, uv);
  c.rgb *= P.Exposure;

  float4 chw = float4(P.ChannelR, P.ChannelG, P.ChannelB, P.ChannelA);
  // TiXL: normalize(ChannelWeights) — guard against zero length.
  float4 chLen = chw * chw;
  float l2 = chLen.x + chLen.y + chLen.z + chLen.w;
  float4 chNorm = (l2 > 1e-9f) ? chw * rsqrt(l2) : float4(0.577f, 0.577f, 0.577f, 0.0f);

  float t = length(c * chNorm) + 0.001f;  // TiXL: length(c * normalize(ChannelWeights)) + 0.001

  float2 gb = float2(P.GainX, P.GainY);
  t = _applyGainAndBias(saturate(t), gb);

  float4 mapBlack = float4(P.MapBlackR, P.MapBlackG, P.MapBlackB, P.MapBlackA);
  float4 mapWhite = float4(P.MapWhiteR, P.MapWhiteG, P.MapWhiteB, P.MapWhiteA);
  float4 mapped = mix(mapBlack, mapWhite, t);
  float4 cout = mix(c, mapped, P.Amount);
  cout.a = clamp(cout.a, 0.0f, 1.0f);
  return cout;
}
