// RadialGradient: TiXL-ported radial/polar gradient image generator, single pass.
// VERBATIM port of external/tixl Operators/Lib/Assets/shaders/img/generate/RadialGradient.hlsl psMain.
// Computes a radial distance (or polar angle) from Center through Stretch, maps it through
// PingPongRepeat + ApplyGainAndBias (+ optional hash noise), and samples a rasterized gradient ROW
// (bound at t1) at (dBiased, 0). Optionally composites over an upstream image.
//
// ============================== HLSL→MSL NOTES (named forks) ==============================
// [fork-gain-bias-inline]  ApplyGainAndBias + GetBias + GetSchlickBias inlined verbatim from
//   shared/bias-functions.hlsl (scalar overload only — psMain calls the scalar form). Same inline
//   pattern as lineargradient.metal / rings.metal pulling shared helpers in.
// [fork-blend-inline]  BlendColors inlined verbatim from shared/blend-functions.hlsl (identical to
//   lineargradient.metal / rings.metal). Used only when IsTextureValid >= 0.5 (Image wired).
// [fork-pi-literal]  RadialGradient.hlsl uses the literal 3.141578 for π (NOT M_PI_F) in the polar
//   atan2 normalization. Preserved verbatim so the angle math is byte-identical to TiXL.
// [fork-hash12-inline]  RadialGradient.hlsl calls hash12(float2) from shared/hash-functions.hlsl
//   (lines 20-26). The engine's shared/hash.metal.h ships hash11/hash22/hash31/hash41u/hash42 but NOT
//   hash12, so hash12 is inlined here VERBATIM (frac->fract; same algebra). Reached only when Noise>0;
//   the golden pins Noise=0 (closed-form), so the noise branch is exercised by the live UI param only.
// [fork-grad-sampler-clamp]  The gradient row is sampled at v=0 with the clampedSampler (ClampToEdge);
//   ImageA uses texSampler (Wrap). A Wrap sampler on the 1-row gradient would corrupt the v edge —
//   clamp is MANDATORY (mirrors lineargradient.metal).
// [fork-dead-fmod]  RadialGradient.hlsl declares a local `float fmod` (floor-based) but psMain never
//   calls it (PingPongRepeat uses frac, not fmod). Dead in psMain → not ported.
#include <metal_stdlib>
#include "radialgradient_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id — same convention as lineargradient_vs / rings_vs.
vertex VSOut radialgradient_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC top-left vs texture bottom-left
  return o;
}

// --- ApplyGainAndBias (scalar) — verbatim from shared/bias-functions.hlsl ---
static inline float GetBias(float bias, float x) {
  return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}
static inline float GetSchlickBias(float g, float x) {
  if (x < 0.5f) {
    x *= 2.0f;
    x = 0.5f * GetBias(g, x);
  } else {
    x = 2.0f * x - 1.0f;
    x = 0.5f * GetBias(1.0f - g, x) + 0.5f;
  }
  return x;
}
static inline float ApplyGainAndBias(float value, float2 gainBias) {
  float g = saturate(gainBias.x);
  float b = saturate(gainBias.y);
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) {
    value = GetBias(b, value);
    value = GetSchlickBias(g, value);
  } else {
    value = GetSchlickBias(g, value);
    value = GetBias(b, value);
  }
  return value;
}

// --- hash12 — verbatim from shared/hash-functions.hlsl lines 20-26 (frac->fract). [fork-hash12-inline]
static inline float hash12(float2 p) {
  float3 p3 = fract(float3(p.xyx) * 0.1031f);
  p3 += dot(p3, p3.yzx + 33.33f);
  return fract((p3.x + p3.y) * p3.z);
}

// --- BlendColors — verbatim from shared/blend-functions.hlsl (same as lineargradient.metal) ---
static inline float4 BlendColors(float4 tA, float4 tB, int blendMode) {
  tA.a = saturate(tA.a);
  tB.a = saturate(tB.a);
  float a = tA.a + tB.a - tA.a * tB.a;
  float3 rgbNormalBlended = (1.0f - tB.a) * tA.rgb + tB.a * tB.rgb;
  float3 rgb = float3(1.0f);
  switch (blendMode) {
    case 0:  rgb = rgbNormalBlended; break;
    case 1:  rgb = 1.0f - (1.0f - tA.rgb) * (1.0f - tB.rgb * tB.a); break;
    case 2:  rgb = mix(tA.rgb, tA.rgb * tB.rgb, tB.a); break;
    case 3:
      rgb = float3(
        tA.r < 0.5f ? (2.0f*tA.r*tB.r) : (1.0f - 2.0f*(1.0f-tA.r)*(1.0f-tB.r)),
        tA.g < 0.5f ? (2.0f*tA.g*tB.g) : (1.0f - 2.0f*(1.0f-tA.g)*(1.0f-tB.g)),
        tA.b < 0.5f ? (2.0f*tA.b*tB.b) : (1.0f - 2.0f*(1.0f-tA.b)*(1.0f-tB.b)));
      rgb = mix(tA.rgb, rgb, tB.a); break;
    case 4:  rgb = abs(tA.rgb - tB.rgb) * tB.a + tB.rgb * (1.0f - tB.a); break;
    case 5:  rgb = tA.rgb; break;
    case 6:  rgb = tB.rgb; break;
    case 7:  rgb = tA.rgb / (1.0001f - saturate(tB.rgb)); break;
    case 8:  rgb = tA.rgb + tB.rgb; break;
    case 9:  a = tA.a * tB.a; break;
  }
  return float4(rgb, a);
}

// PingPongRepeat — verbatim RadialGradient.hlsl lines 46-60 (= lineargradient.metal's, float-arg form).
static inline float PingPongRepeat(float x, float pingPong, float repeat) {
  float baseValue = x + 0.5f;
  float repeatValue = fract(baseValue);
  float pingPongValue = 1.0f - abs(fract(x * 0.5f) * 2.0f - 1.0f);
  float singlePingPong = abs(x);

  float pingPongOutput = mix(singlePingPong, pingPongValue, step(0.5f, repeat));

  float value = mix(baseValue, repeatValue, step(0.5f, repeat));      // if repeat, use repeatValue
  value = mix(value, pingPongOutput, step(0.5f, pingPong));            // pingpong overrides
  value = mix(saturate(value), value, step(0.5f, repeat));            // clamp [0..1] if NOT repeating
  return value;
}

// Mirror of RadialGradient.hlsl psMain (lines 61-114), line for line.
fragment float4 radialgradient_fs(VSOut input                            [[stage_in]],
                                  texture2d<float> ImageA                [[texture(0)]],
                                  texture2d<float> Gradient              [[texture(1)]],
                                  sampler texSampler                     [[sampler(0)]],
                                  sampler clampedSampler                 [[sampler(1)]],
                                  constant RadialGradientParams& P       [[buffer(RADIALGRADIENT_Params)]],
                                  constant RadialGradientResolution& Res [[buffer(RADIALGRADIENT_Resolution)]]) {
  float2 uv = input.texCoord;                                          // :63

  float aspectRatio = Res.TargetWidth / Res.TargetHeight;            // :65
  float2 p = uv - 0.5f;                                                // :66
  p.x *= aspectRatio;                                                  // :67

  float w = max(abs(P.Width), 1e-6f);                                 // :68
  float dir = sign(P.Width);                                          // :69

  float c;                                                             // :71

  float2 Center = float2(P.CenterX, P.CenterY);
  float2 Stretch = float2(P.StretchX, P.StretchY);

  if (P.PolarOrientation < 0.5f) {                                     // :73
    float2 d = (p - Center * float2(1.0f, -1.0f)) / Stretch;          // :75
    float r = length(d);                                              // :76 radial distance
    c = r * 2.0f / w - mix(0.5f, 1.0f, P.PingPong);                   // :77
  } else {
    p += Center * float2(-1.0f, 1.0f);                                // :81
    p /= Stretch;                                                     // :82
    float angle = atan2(p.x, p.y) / 3.141578f;                       // :83 [-1 .. 1] [fork-pi-literal]
    c = angle / w;                                                    // :84
  }

  c -= P.Offset;                                                       // :87
  c = PingPongRepeat(c, P.PingPong, P.Repeat);                        // :88

  // Flip gradient direction (center <-> edge)
  if (dir <= 0.0f)                                                     // :91
    c = 1.0f - c;                                                      // :92

  c = clamp(c, 0.001f, 0.999f);                                        // :99
  float dBiased = ApplyGainAndBias(c, float2(P.GainAndBiasX, P.GainAndBiasY));  // :100

  if (P.Noise > 0.0f) {                                                // :102
    dBiased = saturate(dBiased + hash12(p * 1330.1f) * P.Noise);      // :104
  }

  float4 gradient = Gradient.sample(clampedSampler, float2(dBiased, 0.0f));  // :107

  if (P.IsTextureValid < 0.5f)                                        // :109
    return gradient;                                                  // :110

  float4 orgColor = ImageA.sample(texSampler, input.texCoord);        // :112
  return BlendColors(orgColor, gradient, (int)P.BlendMode);           // :113
}
