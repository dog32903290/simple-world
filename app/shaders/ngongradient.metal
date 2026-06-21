// NGonGradient: TiXL-ported gradient-mapped N-gon image generator, single pass.
// VERBATIM port of external/tixl Operators/Lib/Assets/shaders/img/generate/NGonGradient.hlsl psMain.
// Builds an SDF regular polygon (sdRegularPolygon, Blades/Curvature/Roundness), maps the signed
// distance through PingPongRepeat + ApplyGainAndBias to a t, and samples a rasterized gradient ROW
// (bound at t1) at (dBiased, 0). Optionally composites over an upstream image.
//
// ★This is the GRADIENT-mapped variant of NGon — DISTINCT from the already-ported NGon op (which lerps
//   Fill/Background via smoothstep feather). NGonGradient maps the SDF onto a Gradient via t1.
//
// ============================== HLSL→MSL NOTES (named forks) ==============================
// [fork-gain-bias-inline]  ApplyGainAndBias + GetBias + GetSchlickBias inlined verbatim from
//   shared/bias-functions.hlsl (scalar overload only — psMain calls ApplyGainAndBias(c, GainAndBias)
//   with a SCALAR c). Same inline pattern as lineargradient.metal / ngon.metal.
// [fork-blend-inline]  BlendColors inlined verbatim from shared/blend-functions.hlsl (identical to
//   lineargradient.metal / ngon.metal). Used only when IsTextureValid >= 0.5 (Image wired).
// [fork-fmod-floor]  NGonGradient.hlsl declares a local floor-based fmod() and USES it in
//   sdRegularPolygon (bn = fmod(atan2(p.x,p.y), 2*an) - an). Ported to sw_mod() here, VERBATIM.
// [fork-grad-sampler-clamp]  The gradient row is sampled at v=0 with the clampedSampler (ClampToEdge);
//   ImageA uses texSampler (Wrap, per _multiImageFxSetupStatic.t3 WrapMode=Wrap). HLSL spells it
//   `clammpedSampler` (sic) at s1 — we bind a ClampToEdge sampler at s1. Clamp is MANDATORY.
// [fork-position-yx]  NGonGradient.hlsl:149 does `p += Position.yx` — the .yx swap is in the shader,
//   ported verbatim (the host fills Position.x/.y straight; this line swaps).
//
// NGonGradient.hlsl uses no saturate() on c before ApplyGainAndBias (unlike LinearGradient), and clamps
//   dBiased to [0.001, 0.999] (NOT [0.000001, 0.99999]). PingPongRepeat here has baseValue = x (NO +0.5,
//   unlike LinearGradient). All three differences ported VERBATIM.
#include <metal_stdlib>
#include "ngongradient_params.h"
using namespace metal;

struct VSOut {
  float4 position [[position]];
  float2 texCoord;
};

// Fullscreen triangle from vertex_id — same convention as lineargradient_vs / ngon's caller.
vertex VSOut ngongradient_vs(uint vid [[vertex_id]]) {
  VSOut o;
  float2 uv = float2((vid << 1) & 2, vid & 2);   // (0,0) (2,0) (0,2)
  o.position = float4(uv * 2.0f - 1.0f, 0.0f, 1.0f);
  o.texCoord = float2(uv.x, 1.0f - uv.y);        // flip Y: NDC top-left vs texture bottom-left
  return o;
}

// NGonGradient.hlsl constants (lines 43-44).
constant float PI  = 3.141592653f;
constant float TAU = (3.1415926535f * 2.0f);

// NGonGradient.hlsl floor-based fmod() (lines 46-49). [fork-fmod-floor]
static inline float sw_mod(float x, float y) { return (x - y * floor(x / y)); }

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

// sdRegularPolygon — verbatim NGonGradient.hlsl lines 77-106. Blades/Curvature/Roundness read from the
// cbuffer (passed in as scalars so the helper stays free of cbuffer coupling).
static inline float sdRegularPolygon(float2 p, float r, float n,
                                     float Blades, float Curvature, float Roundness) {
  // these lines can be precomputed for a given shape
  float an = 3.141593f / float(n);
  float2 acs = float2(cos(an), sin(an));

  // Store original length for curvature calculation
  float originalLen = length(p);

  // reduce to first sector
  float bn = sw_mod(atan2(p.x, p.y), 2.0f * an) - an;
  bn *= bn > 0.0f ? (1.0f - saturate(Blades)) : 1.0f;  // Blades parameter is working

  p = length(p) * float2(cos(bn), abs(sin(bn)));

  // line sdf
  p -= r * acs;

  p.y += clamp(-p.y, 0.0f, r * acs.y);
  p.y *= p.y > 0.0f ? (saturate(Roundness)) : 1.0f;  // we can control the roundness

  // Apply curvature effect to the distance field
  float dist = length(p) * sign(p.x);

  // Adjust distance based on curvature (flower effect)
  float flowerEffect = (r - originalLen) * Curvature;  // Curvature is working again ^_^
  dist += flowerEffect;

  return dist;
}

// rotatePoint — verbatim NGonGradient.hlsl lines 109-117.
static inline float2 rotatePoint(float2 p, float angle) {
  angle = angle * (3.14159265358979323846f / 180.0f);  // HLSL radians(angle)
  float cosAngle = cos(angle);
  float sinAngle = sin(angle);
  return float2(
      p.x * cosAngle + p.y * sinAngle,
      p.x * sinAngle - p.y * cosAngle);
}

// PingPongRepeat — verbatim NGonGradient.hlsl lines 119-137 (NOTE: baseValue = x, NO +0.5).
static inline float PingPongRepeat(float x, float pingPong, float repeat) {
  float baseValue = x;
  float repeatValue = fract(baseValue);
  float pingPongValue = 1.0f - abs(fract(x * 0.5f) * 2.0f - 1.0f);
  float singlePingPong = abs(x);

  float pingPongOutput = mix(singlePingPong, pingPongValue, step(0.5f, repeat));

  float value = mix(baseValue, repeatValue, step(0.5f, repeat));   // if repeat, use repeatValue
  value = mix(value, pingPongOutput, step(0.5f, pingPong));         // pingpong overrides
  value = mix(saturate(value), value, step(0.5f, repeat));         // clamp [0..1] if NOT repeating
  return value;
}

// Mirror of NGonGradient.hlsl psMain (lines 139-161), line for line.
fragment float4 ngongradient_fs(VSOut input                            [[stage_in]],
                                texture2d<float> ImageA                [[texture(0)]],
                                texture2d<float> Gradient              [[texture(1)]],
                                sampler texSampler                     [[sampler(0)]],
                                sampler clammpedSampler                [[sampler(1)]],
                                constant NGonGradientParams& P         [[buffer(NGONGRADIENT_Params)]],
                                constant NGonGradientResolution& Res   [[buffer(NGONGRADIENT_Resolution)]]) {
  float aspectRatio = Res.TargetWidth / Res.TargetHeight;            // :141
  float2 p = input.texCoord;                                          // :142
  p -= 0.5f;                                                          // :143
  p.x *= aspectRatio;                                                 // :144

  // Rotate
  p = rotatePoint(p, P.Rotate);                                       // :147

  p += float2(P.PositionY, P.PositionX);                             // :149 [fork-position-yx] Position.yx
  // float c = sdNgon(p, Radius, Sides) * 2 - Offset * Width;        // :150 (commented out in TiXL)
  float c = sdRegularPolygon(p, P.Radius, P.Sides,                   // :151
                             P.Blades, P.Curvature, P.Roundness) * 2.0f - P.Offset * P.Width;

  float4 orgColor = ImageA.sample(texSampler, input.texCoord);        // :153
  c = PingPongRepeat(c / P.Width, P.PingPong, P.Repeat);             // :154

  float dBiased = ApplyGainAndBias(c, float2(P.GainAndBiasX, P.GainAndBiasY));  // :156
  dBiased = clamp(dBiased, 0.001f, 0.999f);                          // :157
  float4 gradient = Gradient.sample(clammpedSampler, float2(dBiased, 0.0f));    // :158

  return (P.IsTextureValid < 0.5f) ? gradient                        // :160
                                   : BlendColors(orgColor, gradient, (int)P.BlendMode);
}
