// RemapColor: TiXL-ported per-channel gradient color-remap image filter.
// VERBATIM port of external/tixl Operators/Lib/Assets/shaders/img/fx/ColorRemap.hlsl psMain.
// Reads the input image, optionally converts to grayscale (Mode<0.5) or keeps per-channel (Mode>=0.5),
// applies Exposure + ApplyGainAndBias + Repeat to the channel value, then looks the value up in a
// rasterized gradient ROW (bound at t1) at u = channelValue + Offset, v = 0. DontColorAlpha optionally
// preserves the original alpha.
//
// ============================== HLSL→MSL NOTES (named forks) ==============================
// [fork-gain-bias-inline]  ColorRemap.hlsl calls ApplyGainAndBias from shared/bias-functions.hlsl on a
//   float4 in Mode>=0.5 (the .hlsl applies it to orgColor, a float4) and on a float (saturate(gray)) in
//   Mode<0.5 — BUT note in BOTH branches ColorRemap assigns the result back into `orgColor` (a float4),
//   so the Mode<0.5 path does `orgColor = ApplyGainAndBias(saturate(gray), GainAndBias) * Repeat;` where
//   saturate(gray) is a SCALAR auto-broadcast to float4 by HLSL, fed to the float4 ApplyGainAndBias
//   overload, then `* Repeat`. We replicate HLSL's implicit scalar→float4 broadcast explicitly here and
//   port the float4 ApplyGainAndBias overload VERBATIM from bias-functions.hlsl (lines 52-90). The
//   scalar GetBias is also inlined (used inside the float4 GetSchlickBias path via float4 GetBias).
// [fork-grad-sampler-clamp]  The gradient row is sampled at v=0 with the clampedSampler (ClampToEdge);
//   ImageA uses linearSampler (per _multiImageFxSetupStatic WrapMode=Clamp in RemapColor.t3, so both
//   are linear+ClampToEdge here — the cook sets ImageA's address mode to Clamp per the .t3).
// [fork-offset-from-cycle]  cbuffer "Offset" is fed by the op's "Cycle" input (see remapcolor_params.h
//   Cut55 trace) — the shader code below is byte-identical to ColorRemap.hlsl; the routing is host-side.
#include <metal_stdlib>
#include "remapcolor_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id — same convention as radialgradient_vs / bubblezoom_vs.
vertex VSOut remapcolor_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC top-left vs texture bottom-left
  return o;
}

// --- bias-functions.hlsl (the float4 overload psMain actually uses) — ported VERBATIM ---
// Scalar GetBias (bias-functions.hlsl lines 6-9) — needed by the float4 path's component algebra.
static inline float GetBias(float bias, float x) {
  return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}
// float4 GetBias (bias-functions.hlsl lines 52-55).
static inline float4 GetBias(float bias, float4 x) {
  return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}
// float4 GetSchlickBias (bias-functions.hlsl lines 57-61) — note the unusual arg order (x, gain).
static inline float4 GetSchlickBias(float4 x, float gain) {
  return select(GetBias(1.0f - gain, x * 2.0f - 1.0f) / 2.0f + 0.5f,   // x >= 0.5 branch
                GetBias(gain, x * 2.0f) / 2.0f,                        // x <  0.5 branch
                x < 0.5f);
}
// float4 ApplyGainAndBias (bias-functions.hlsl lines 63-90) — VERBATIM (incl. the hi/lo masks the .hlsl
// computes; note the .hlsl returns the un-masked v4 — `result` masking is dead — so we return v4 too).
static inline float4 ApplyGainAndBias(float4 v4, float2 gainBias) {
  float g = saturate(gainBias.x);
  float b = saturate(gainBias.y);
  if (g < 0.5f) {
    v4 = GetBias(b, v4);
    v4 = GetSchlickBias(v4, g);
  } else {
    v4 = GetSchlickBias(v4, g);
    v4 = GetBias(b, v4);
  }
  return v4;  // bias-functions.hlsl returns v4 (the result-mask lines are dead in the .hlsl)
}

// Mirror of ColorRemap.hlsl psMain (lines 25-49), line for line.
fragment float4 remapcolor_fs(VSOut input                   [[stage_in]],
                              texture2d<float> ImageA       [[texture(0)]],
                              texture2d<float> Gradient     [[texture(1)]],
                              sampler linearSampler         [[sampler(0)]],
                              sampler clampedSampler        [[sampler(1)]],
                              constant RemapColorParams& P  [[buffer(REMAPCOLOR_Params)]]) {
  float4 orgColor = ImageA.sample(linearSampler, input.texCoord);   // :27

  float4 gradient = float4(0.0f);                                   // :29
  if (P.Mode < 0.5f) {                                             // :30 (UseGrayScale)
    float gray = (orgColor.r + orgColor.g + orgColor.b) / 3.0f * P.Exposure;  // :32
    // :33 — HLSL broadcasts the scalar saturate(gray) to float4 for the float4 ApplyGainAndBias.
    orgColor = ApplyGainAndBias(float4(saturate(gray)),
                                float2(P.GainAndBiasX, P.GainAndBiasY)) * P.Repeat;  // :33
    gradient = Gradient.sample(clampedSampler, float2(orgColor.r + P.Offset, 0.0f));  // :34
  } else {                                                          // (IndividualChannels)
    orgColor.rgb *= P.Exposure;                                    // :38
    orgColor = ApplyGainAndBias(saturate(orgColor),
                                float2(P.GainAndBiasX, P.GainAndBiasY)) * P.Repeat;  // :39
    gradient = float4(                                             // :41-45
        Gradient.sample(clampedSampler, float2(orgColor.r + P.Offset, 0.0f)).r,
        Gradient.sample(clampedSampler, float2(orgColor.g + P.Offset, 0.0f)).g,
        Gradient.sample(clampedSampler, float2(orgColor.b + P.Offset, 0.0f)).b,
        Gradient.sample(clampedSampler, float2(orgColor.a + P.Offset, 0.0f)).a);
  }

  gradient.a = P.DontColorAlpha > 0.5f ? orgColor.a : gradient.a;  // :48
  return gradient;                                                 // :49
}
