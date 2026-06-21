// LinearGradient: TiXL-ported linear-gradient image generator, single pass.
// VERBATIM port of external/tixl Operators/Lib/Assets/shaders/img/generate/LinearGradient.hlsl psMain.
// Projects each pixel onto a rotated/offset axis, maps the projection through GainAndBias, and samples
// a rasterized gradient ROW (bound at t1) at (dBiased, 0). Optionally composites over an upstream image.
//
// ============================== HLSL→MSL NOTES (named forks) ==============================
// [fork-gain-bias-inline]  ApplyGainAndBias + GetBias + GetSchlickBias inlined verbatim from
//   shared/bias-functions.hlsl (scalar overload only — psMain calls the scalar form). Same inline
//   pattern as ngon.metal / rings.metal pulling shared helpers in.
// [fork-blend-inline]  BlendColors inlined verbatim from shared/blend-functions.hlsl (identical to
//   rings.metal / ngon.metal). Used only when IsTextureValid >= 0.5 (Image wired).
// [fork-pi-literal]  LinearGradient.hlsl uses the literal 3.141578 for π (NOT M_PI_F). Preserved
//   verbatim so radians math is byte-identical to TiXL (same as rings.metal's 3.141578).
// [fork-grad-sampler-clamp]  The gradient row is sampled at v=0 with the clampedSampler (ClampToEdge);
//   ImageA uses texSampler (Wrap, per _multiImageFxSetupStatic.t3 WrapMode=Wrap). The two samplers are
//   distinct: a Wrap sampler on the 1-row gradient would corrupt the v edge — clamp is MANDATORY.
// [fork-dead-fmod]  LinearGradient.hlsl declares a local `inline float fmod` (floor-based) but psMain
//   never calls it (PingPongRepeat uses frac, not fmod). Dead in psMain → not ported.
#include <metal_stdlib>
#include "lineargradient_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id — same convention as rings_vs / ngon's caller.
vertex VSOut lineargradient_vs(uint vid [[vertex_id]]) {
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

// --- BlendColors — verbatim from shared/blend-functions.hlsl (same as rings.metal) ---
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

// PingPongRepeat — verbatim LinearGradient.hlsl lines 43-61.
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

// Mirror of LinearGradient.hlsl psMain (lines 63-97), line for line.
fragment float4 lineargradient_fs(VSOut input                            [[stage_in]],
                                  texture2d<float> ImageA                [[texture(0)]],
                                  texture2d<float> Gradient              [[texture(1)]],
                                  sampler texSampler                     [[sampler(0)]],
                                  sampler clampedSampler                 [[sampler(1)]],
                                  constant LinearGradientParams& P       [[buffer(LINEARGRADIENT_Params)]],
                                  constant LinearGradientResolution& Res [[buffer(LINEARGRADIENT_Resolution)]]) {
  float2 uv = input.texCoord;                                          // :65

  float aspectRation = Res.TargetWidth / Res.TargetHeight;            // :67 (HLSL typo "aspectRation" kept)
  float2 p = uv;                                                       // :68
  p -= 0.5f;                                                           // :69

  if (P.SizeMode < 0.5f) {                                             // :71
    p.x *= aspectRation;                                              // :73
  } else {
    p.y /= aspectRation;                                              // :77
  }

  float radians = P.Rotation / 180.0f * 3.141578f;                    // :80 [fork-pi-literal]
  float2 angle = float2(sin(radians), cos(radians));                  // :81

  float c = dot(p - float2(P.CenterX, P.CenterY), angle);            // :83
  c += P.Offset;                                                      // :84 (P.Offset already routed host-side)
  c = PingPongRepeat(c / P.Width, P.PingPong > 0.5f, P.Repeat > 0.5f);  // :85

  float dBiased = ApplyGainAndBias(saturate(c), float2(P.GainAndBiasX, P.GainAndBiasY));  // :87
  dBiased = clamp(dBiased, 0.000001f, 0.99999f);                      // :88

  float4 gradient = Gradient.sample(clampedSampler, float2(dBiased, 0.0f));  // :90

  if (P.IsTextureValid < 0.5f)                                        // :92
    return gradient;                                                  // :93

  float4 orgColor = ImageA.sample(texSampler, input.texCoord);        // :95
  return (P.IsTextureValid < 0.5f)                                    // :96
         ? gradient
         : BlendColors(orgColor, gradient, (int)P.BlendMode);
}
