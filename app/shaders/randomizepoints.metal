// randomizepoints — faithful port of external/tixl
// .../Assets/shaders/points/modify/RandomizePoints.hlsl. A MODIFIER op: reads a bag of
// SwPoints (SourcePoints), jitters per-point attributes by a hash/noise-driven pseudo-random
// offset, writes a count-preserving bag (ResultPoints). Per-point seed = hash of pointId.
//
// TiXL parity (RandomizePoints.hlsl, lines 63-130):
//  - Per-point phase: pointU = pointId*_PRIME0 % (Repeat==0?999999999:Repeat); phaseOffset =
//    hash11u(pointU); phase = |phaseOffset + RandomSeed|; phaseIndex = (uint)phase + pointU.
//  - Two biased 4-vectors of noise (biasedA/biasedB), each lerp(hash41u(phaseIndex),
//    hash41u(phaseIndex+1), t) then ApplyGainAndBias; biasedB uses phaseIndex+_PRIME0.
//    t = Interpolation: None->0, Linear->frac(phase), Smooth->smoothstep(frac(phase)).
//  - Scatter (OffsetMode==1) remaps biased noise [0,1]->[-1,1] (centered jitter).
//  - strength = Strength * (StrengthFactor: None->1, F1->p.FX1, F2->p.FX2).
//  - Position += strength * (PointSpace? qRotate(noise*RandomizePosition, p.Rotation)
//                                       : noise*RandomizePosition).   [UsePointSpace flips]
//  - Color jittered in HSB space (only if |RandomizeColor|>0.001); ClampColorsEtc clamps.
//  - FX1 += biasedA.w*RandomizeF1*strength; FX2 += biasedA.r*RandomizeF2*strength.
//  - Scale += (float3(biasedB.w,biasedA.w,biasedA.z)*Stretch + biasedA.r*Scale)*strength.
//  - Rotation: three successive qMul(qFromAngleAxis) about X/Y/Z by RandomizeRotation(deg→rad)
//    *strength*biasedA.xyz, re-normalized each step. (qMul/qFromAngleAxis from quat.metal.h.)
//
// The .hlsl shared helpers (hash11u/hash41u/_PRIME0, ApplyGainAndBias, hsb<->rgb) aren't in
// the project's MSL headers, so they're inlined here 1:1 from external/tixl
// .../shared/{hash-functions,bias-functions}.hlsl and the in-file hsb2rgb/rgb2hsb.
//
// Baked / deferred (flagged): nothing material is baked — every .cs input maps to a param.
// ClampColorsEtc's NaN guard (`!isnan(p.Scale.x)`) is preserved.
#include <metal_stdlib>
#include "tixl_point.h"               // SwPoint (64B)
#include "randomizepoints_params.h"   // RandomizeParams, RandomizeBinding
#include "shared/quat.metal.h"        // qFromAngleAxis, qMul, qRotateVec3
using namespace metal;

// ---- inlined from external/tixl .../shared/hash-functions.hlsl ----
constant uint _PRIME0 = 13331u;

inline float4 hash41u(uint x) {
  const uint k = 1103515245u;  // GLIB C
  x *= _PRIME0;
  x = ((x >> 8u) ^ x) * k;
  uint y = ((x >> 8u) ^ x) * k;
  uint z = ((y >> 8u) ^ x) * k;
  uint w = ((z >> 8u) ^ y) * k;
  uint4 i4 = uint4(x, y, z, w);
  return float4(i4) * (1.0f / float(0xffffffffu));
}

inline float hash11u(uint x) {
  const uint k = 1103515245u;  // GLIB C
  x *= _PRIME0;
  x = ((x >> 8u) ^ x) * k;
  x = ((x >> 8u) ^ x) * k;
  return float(x) * (1.0f / float(0xffffffffu));
}

// ---- inlined from external/tixl .../shared/bias-functions.hlsl (float4 path) ----
inline float4 GetBias(float bias, float4 x) {
  return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}
inline float4 GetSchlickBias(float4 x, float gain) {
  return select(GetBias(1.0f - gain, x * 2.0f - 1.0f) / 2.0f + 0.5f,
                GetBias(gain, x * 2.0f) / 2.0f,
                x < 0.5f);
}
inline float4 ApplyGainAndBias(float4 v4, float2 gainBias) {
  float g = saturate(gainBias.x);
  float b = saturate(gainBias.y);

  // avoid modifying 0 and 1 for extreme bias and gain values
  float4 hiMask = step(0.999f, v4);
  float4 loMask = step(v4, 0.00001f);
  float4 result = v4;

  if (g < 0.5f) {
    v4 = GetBias(b, v4);
    v4 = GetSchlickBias(v4, g);
  } else {
    v4 = GetSchlickBias(v4, g);
    v4 = GetBias(b, v4);
  }
  // (the .hlsl computes hi/lo masks but `return v4;` — port it faithfully, masks unused)
  result = hiMask * 1.0f + (1.0f - hiMask) * result;
  result = (1.0f - loMask) * result;
  return v4;
}

// ---- inlined hsb<->rgb from RandomizePoints.hlsl ----
inline float3 hsb2rgb(float3 c) {
  float4 K = float4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
  float3 p = abs(fract(c.xxx + K.xyz) * 6.0f - K.www);
  return c.z < 0.5f
             ? c.z * 2.0f * mix(K.xxx, clamp(p - K.xxx, 0.0f, 1.0f), c.y)
             : mix(K.xxx, clamp(p - K.xxx, 0.0f, 1.0f), mix(c.y, 0.0f, (c.z * 2.0f - 1.0f)));
}
inline float3 rgb2hsb(float3 c) {
  float4 K = float4(0.0f, -1.0f / 3.0f, 2.0f / 3.0f, -1.0f);
  float4 p = mix(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
  float4 q = mix(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
  float d = q.x - min(q.w, q.y);
  float e = 1.0e-10f;
  return float3(abs(q.z + (q.w - q.y) / (6.0f * d + e)), d / (q.x + e), q.x * 0.5f);
}

kernel void randomizepoints(const device SwPoint*       src [[buffer(RANDOMIZE_SourcePoints)]],
                            device SwPoint*              dst [[buffer(RANDOMIZE_ResultPoints)]],
                            constant RandomizeParams&     P   [[buffer(RANDOMIZE_Params)]],
                            uint                          tid [[thread_position_in_grid]]) {
  if (tid >= P.Count) return;
  SwPoint p = src[tid];

  float3 randomizePosition = float3(P.RandomizePositionX, P.RandomizePositionY, P.RandomizePositionZ);
  float3 randomizeRotation = float3(P.RandomizeRotationX, P.RandomizeRotationY, P.RandomizeRotationZ);
  float4 randomizeColor = float4(P.RandomizeColorX, P.RandomizeColorY, P.RandomizeColorZ, P.RandomizeColorW);
  float3 stretch = float3(P.StretchX, P.StretchY, P.StretchZ);
  float2 gainAndBias = float2(P.GainAndBiasX, P.GainAndBiasY);

  uint pointId = tid;
  uint pointU = pointId * _PRIME0 % (P.Repeat == 0 ? 999999999u : uint(P.Repeat));
  float particlePhaseOffset = hash11u(pointU);

  float phase = abs(particlePhaseOffset + P.RandomSeed);

  uint phaseIndex = uint(phase) + pointU;

  float t = fmod(phase, 1.0f);
  t = P.Interpolation == 0u ? 0.0f : (P.Interpolation == 1u ? t : smoothstep(0.0f, 1.0f, t));
  float4 biasedA = ApplyGainAndBias(mix(hash41u(phaseIndex), hash41u(phaseIndex + 1u), t), gainAndBias);
  float4 biasedB = ApplyGainAndBias(mix(hash41u(phaseIndex + _PRIME0), hash41u(phaseIndex + _PRIME0 + 1u), t), gainAndBias);

  float strength = P.Strength * (P.StrengthFactor == 0
                                     ? 1.0f
                                 : (P.StrengthFactor == 1) ? p.FX1
                                                           : p.FX2);

  float4 rot = p.Rotation;

  if (P.OffsetMode == 1u) {
    biasedA = (biasedA * 2.0f) - 1.0f;
    biasedB = (biasedB * 2.0f) - 1.0f;
  }

  p.Position += strength * (P.UsePointSpace == 0u
                                ? qRotateVec3(biasedA.xyz * randomizePosition, p.Rotation)
                                : biasedA.xyz * randomizePosition);

  float4 rgba = p.Color;
  if (length(randomizeColor) > 0.001f) {
    float4 HSBa = float4(rgb2hsb(float3(p.Color.x, p.Color.y, p.Color.z)), p.Color.w);
    HSBa += biasedB * randomizeColor * strength;
    HSBa.x = fmod(HSBa.x, 1.0f);
    rgba = float4(hsb2rgb(HSBa.xyz), HSBa.w);
  }

  p.Color = P.ClampColorsEtc ? clamp(rgba, 0.0f, float4(1000.0f, 1000.0f, 1000.0f, 1.0f)) : rgba;

  p.FX1 += biasedA.w * P.RandomizeF1 * strength;
  p.FX2 += biasedA.r * P.RandomizeF2 * strength;

  if (P.ClampColorsEtc && !isnan(p.Scale.x)) {
    p.FX1 = max(0.0f, p.FX1);
    p.FX2 = max(0.0f, p.FX2);
  }
  // Not ideal... distribution overlap (TiXL comment)
  float3 scaleAdd = (float3(biasedB.w, biasedA.w, biasedA.z) * stretch) + biasedA.r * P.Scale;
  p.Scale = float3(p.Scale) + scaleAdd * strength;

  // Rotation: three successive axis rotations, re-normalized each step (TiXL lines 124-128).
  float3 randomRotate = (randomizeRotation / 180.0f * M_PI_F) * strength * biasedA.xyz;
  rot = normalize(qMul(rot, qFromAngleAxis(randomRotate.x, float3(1, 0, 0))));
  rot = normalize(qMul(rot, qFromAngleAxis(randomRotate.y, float3(0, 1, 0))));
  rot = normalize(qMul(rot, qFromAngleAxis(randomRotate.z, float3(0, 0, 1))));
  p.Rotation = rot;

  dst[tid] = p;
}
