// BoxGradient: TiXL-ported rounded-box gradient image generator, single pass.
// VERBATIM port of external/tixl Operators/Lib/Assets/shaders/img/generate/BoxGradient.hlsl psMain.
// Computes an iquilezles rounded-box SDF for each pixel (rotated/centered/scaled), maps the signed
// distance through PingPongRepeat + ApplyGainAndBias, and samples a rasterized gradient ROW (bound at
// t1) at (dBiased, 0). Optionally composites over an upstream image.
//
// ============================== HLSL->MSL NOTES (named forks) ==============================
// [fork-gain-bias-inline]  ApplyGainAndBias + GetBias + GetSchlickBias inlined verbatim from
//   shared/bias-functions.hlsl (scalar overload only — psMain calls the scalar form). Identical to
//   lineargradient.metal's inlined helpers.
// [fork-blend-inline]  BlendColors inlined verbatim from shared/blend-functions.hlsl (identical to
//   lineargradient.metal / rings.metal). Used only when IsTextureValid >= 0.5 (Image wired).
// [fork-pingpong-base]  BoxGradient.hlsl PingPongRepeat uses baseValue = x (NOT x + 0.5 like
//   LinearGradient). Preserved verbatim — this is a per-op difference in the two PingPongRepeat
//   variants, so the helper is NOT shared with lineargradient.metal.
// [fork-grad-sampler-clamp]  The gradient row is sampled at v=0 with the clammpedSampler (ClampToEdge;
//   note the TiXL typo "clammpedSampler" preserved as the s1 binding name); ImageA uses texSampler
//   (Wrap). A Wrap sampler on the 1-row gradient would corrupt the v edge — clamp is MANDATORY.
// [fork-clamp-range]  BoxGradient clamps dBiased to [0.001, 0.999] (BoxGradient.hlsl line 106), NOT
//   LinearGradient's [0.000001, 0.99999]. Preserved verbatim.
// [fork-no-saturate]  BoxGradient passes the raw PingPongRepeat result into ApplyGainAndBias (no
//   saturate(c) wrap, unlike LinearGradient). Preserved verbatim.
// [fork-dead-fmod]  BoxGradient.hlsl declares a local `float fmod` (floor-based) but psMain never
//   calls it (PingPongRepeat uses frac). Dead in psMain -> not ported.
#include <metal_stdlib>
#include "boxgradient_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id — same convention as lineargradient_vs / rings_vs.
vertex VSOut boxgradient_vs(uint vid [[vertex_id]]) {
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

// PingPongRepeat — verbatim BoxGradient.hlsl lines 33-51 (baseValue = x, NOT x + 0.5). [fork-pingpong-base]
static inline float PingPongRepeat(float x, float pingPong, float repeat) {
  float baseValue = x;                                                 // :35
  float repeatValue = fract(baseValue);                               // :36
  float pingPongValue = 1.0f - abs(fract(x * 0.5f) * 2.0f - 1.0f);    // :37
  float singlePingPong = abs(x);                                       // :38

  float pingPongOutput = mix(singlePingPong, pingPongValue, step(0.5f, repeat));  // :41

  float value = mix(baseValue, repeatValue, step(0.5f, repeat));      // :44 if repeat, use repeatValue
  value = mix(value, pingPongOutput, step(0.5f, pingPong));            // :45 pingpong overrides
  value = mix(saturate(value), value, step(0.5f, repeat));            // :48 clamp [0..1] if NOT repeating
  return value;                                                       // :50
}

// sdRoundedBox — verbatim BoxGradient.hlsl lines 64-70 (iquilezles sdRoundBox). r = CornersRadius.
static inline float sdRoundedBox(float2 p, float2 b, float4 r) {
  r.xy = (p.x > 0.0f) ? r.xy : r.zw;                                  // :66
  r.x = (p.y > 0.0f) ? r.x : r.y;                                    // :67
  float2 q = abs(p) - b + r.x;                                        // :68
  return min(max(q.x, q.y), 0.0f) + length(max(q, 0.0f)) - r.x;      // :69
}

// rotatePoint — verbatim BoxGradient.hlsl lines 73-81. Note the SECOND row is (p.x*sin - p.y*cos),
// NOT a standard rotation matrix (TiXL's own form) — preserved verbatim.
static inline float2 rotatePoint(float2 p, float angle) {
  angle = angle * (3.14159265358979323846f / 180.0f);  // radians(angle)  :75
  float cosAngle = cos(angle);                                         // :76
  float sinAngle = sin(angle);                                         // :77
  return float2(p.x * cosAngle + p.y * sinAngle,                       // :79
                p.x * sinAngle - p.y * cosAngle);                      // :80
}

// Mirror of BoxGradient.hlsl psMain (lines 83-110), line for line.
fragment float4 boxgradient_fs(VSOut input                           [[stage_in]],
                               texture2d<float> ImageA               [[texture(0)]],
                               texture2d<float> Gradient             [[texture(1)]],
                               sampler texSampler                    [[sampler(0)]],
                               sampler clammpedSampler               [[sampler(1)]],
                               constant BoxGradientParams& P          [[buffer(BOXGRADIENT_Params)]],
                               constant BoxGradientResolution& Res    [[buffer(BOXGRADIENT_Resolution)]]) {
  float2 uv = input.texCoord;                                          // :85

  float aspectRation = Res.TargetWidth / Res.TargetHeight;            // :87 (HLSL typo "aspectRation" kept)
  float2 p = uv;                                                       // :88
  p -= 0.5f;                                                           // :89
  p.x *= aspectRation;                                                 // :90
  p += float2(P.CenterX, P.CenterY) * float2(-1.0f, 1.0f);            // :91

  // Apply the rotation to the point.
  p = rotatePoint(p, P.Rotation);                                     // :94

  float c = 0.0f;                                                      // :96

  c = sdRoundedBox(p, float2(P.SizeX, P.SizeY) * P.UniformScale,
                   float4(P.CornersRadiusX, P.CornersRadiusY, P.CornersRadiusZ, P.CornersRadiusW)
                       * P.UniformScale) * 2.0f - P.Offset * P.Width;  // :98

  float4 orgColor = ImageA.sample(texSampler, input.texCoord);        // :100

  c = PingPongRepeat(c / P.Width, P.PingPong, P.Repeat);             // :102

  float dBiased = ApplyGainAndBias(c, float2(P.GainAndBiasX, P.GainAndBiasY));  // :104 [fork-no-saturate]

  dBiased = clamp(dBiased, 0.001f, 0.999f);                          // :106 [fork-clamp-range]
  float4 gradient = Gradient.sample(clammpedSampler, float2(dBiased, 0.0f));  // :107

  return (P.IsTextureValid < 0.5f)                                    // :109
             ? gradient
             : BlendColors(orgColor, gradient, (int)P.BlendMode);
}
