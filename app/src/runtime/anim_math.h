// runtime/anim_math — the AnimMath shape engine, ported VERBATIM from TiXL Core/Utils/AnimMath.cs.
// This is the PURE-MATH foundation shared by the whole Anim* family (AnimValue / AnimVec2 / AnimVec3 /
// ...): a pure function of (Shape enum, normalizedTime, componentIndex, bias, ratio). No state, no
// hardware, no UI — a runtime leaf. HEADER-ONLY (inline functions): every consumer is in the same
// app/src tree, so the helper needs NO CMakeLists entry and no link unit, matching how the existing
// stateful_value_ops keeps its EasingFunctions port as static free functions (one translation unit's
// worth of curve math). Anyone who needs the Anim* shape value #includes this.
//
// PORT FIDELITY — the math is reproduced 1:1 from AnimMath.cs (lines cited inline). The pieces it
// depends on from MathUtils.cs are inlined here (they live in TiXL's MathUtils, but only these few
// are needed by the shape engine, so inlining keeps the helper self-contained — same precedent as
// stateful_value_ops.cpp inlining the EasingFunctions it needs):
//   • Fmod(v,mod) = v - mod*floor(v/mod)            (MathUtils.cs:333/339 — floored modulo, NOT C fmod)
//   • XxHash(uint)                                   (MathUtils.cs:113-124 — the Random shape's hash)
//   • PerlinNoise(value,period,octaves,seed)         (MathUtils.cs:16-41 — the PerlinNoise shapes)
//   • Clamp(v,lo,hi)                                 (generic numeric clamp)
//
// runtime leaf: pure computation, no hardware, no UI.
#pragma once
#include <cmath>
#include <cstdint>

namespace sw {
namespace anim_math {

// === Shape enum — order MUST match AnimMath.Shapes EXACTLY (AnimMath.cs:117-132). ===
// This integer IS the .t3 Shape DefaultValue selector (AnimValue.t3 Shape DefaultValue=1=Ramps), so
// the order is load-bearing: a reordered enum silently remaps every saved graph's Shape selector.
enum class Shapes : int {
  Endless = 0,
  Ramps = 1,
  Saws = 2,
  KickSaws = 3,
  Square = 4,
  ZigZag = 5,
  Wave = 6,
  Sin = 7,
  PerlinNoise = 8,
  PerlinNoiseSigned = 9,
  Random = 10,
  RandomSigned = 11,
  Steps = 12,
};
constexpr int kShapeCount = 13;  // Enum.GetNames(typeof(Shapes)).Length

// --- MathUtils.Fmod (MathUtils.cs:339, double overload) — FLOORED modulo: result has the sign of
// `mod`, unlike C fmod which has the sign of `v`. AnimMath relies on this for negative time. ---
inline double fmodFloored(double v, double mod) { return v - mod * std::floor(v / mod); }
inline float  fmodFloored(float v, float mod) { return v - mod * std::floor(v / mod); }

// --- MathUtils.XxHash(uint) (MathUtils.cs:113-124) ported VERBATIM. Used by the Random shapes. ---
inline uint32_t xxHash(uint32_t p) {
  const uint32_t prime32A = 3266489917U;
  const uint32_t prime32B = 668265263U, prime32C = 374761393U;
  uint32_t h32 = p + prime32C;
  h32 = prime32B * ((h32 << 17) | (h32 >> (32 - 17)));
  h32 = 2246822519U * (h32 ^ (h32 >> 15));
  h32 = prime32A * (h32 ^ (h32 >> 13));
  return h32 ^ (h32 >> 16);
}

// --- MathUtils.PerlinNoise + its Noise/Fade helpers (MathUtils.cs:16-41, 141-144) ported VERBATIM.
// Used by the PerlinNoise / PerlinNoiseSigned shapes. ---
inline float perlinNoiseFade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
inline float perlinLerp(float a, float b, float t) { return a + (b - a) * t; }  // MathUtils.Lerp
inline float perlinNoiseHash(int x, int seed) {
  int n = x + seed * 137;
  n = (n << 13) ^ n;
  return (float)(1.0 - ((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0);
}
inline float perlinNoise(float value, float period, int octaves, int seed) {
  float noiseSum = 0.0f;
  if (octaves < 1) octaves = 1;
  else if (octaves > 20) octaves = 20;  // octaves.Clamp(1,20)
  float frequency = period;
  float amplitude = 0.5f;
  for (int octave = 0; octave < octaves - 1; ++octave) {
    const float v = value * frequency + seed * 12.468f;
    const float a = perlinNoiseHash((int)v, seed);
    const float b = perlinNoiseHash((int)v + 1, seed);
    const float t = perlinNoiseFade(v - (float)std::floor(v));
    noiseSum += perlinLerp(a, b, t) * amplitude;
    frequency *= 2.0f;
    amplitude *= 0.5f;
  }
  return noiseSum;
}

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// --- SchlickBias / SchlickBiasWithNegative (AnimMath.cs:85-95) ported VERBATIM. ---
inline float schlickBias(float x, float bias) {
  return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}
inline float schlickBiasWithNegative(float xx, float bias) {
  const float normalized = xx / 2.0f + 0.5f;
  const float biased = normalized / ((1.0f / bias - 2.0f) * (1.0f - normalized) + 1.0f);
  return biased * 2.0f - 1.0f;
}

// --- The shape mapping functions (AnimMath._mapShapes[], AnimMath.cs:98-115) ported VERBATIM. The
// indices match the enum: index i = mapShape(i). Only the 6 "ramp-family" shapes + Sin route through
// these in CalcValueForNormalizedTime; the others (Random/Endless/Perlin/Steps) have their own paths.
inline float mapShape(int shapeIndex, float f) {
  switch (shapeIndex) {
    case 0:  return f;                                    // Endless
    case 1:  return f;                                    // Ramps
    case 2:  return 1.0f - fmodFloored(f, 1.0f);          // Saws
    case 3:  return f <= 0.0f ? 0.0f : (1.0f - clampf(f, 0.0f, 1.0f));  // KickSaws
    case 4:  return f > 0.5f ? 1.0f : 0.0f;               // Square
    case 5: {                                             // ZigZag
      const float ff = fmodFloored(f, 1.0f);
      return ff < 0.5f ? (ff * 2.0f) : (1.0f - (ff - 0.5f) * 2.0f);
    }
    case 6:  return (float)std::sin((f + 0.25) * 2.0 * 3.141592) / 2.0f + 0.5f;  // Wave
    case 7:  return (float)std::sin((f + 0.25) * 2.0 * 3.141592);                // Sin
    case 8:  return f;                                    // PerlinNoise
    case 9:  return f;                                    // Random (slot — Random has its own path)
    case 10: return f;                                    // Steps   (slot)
    default: return f;
  }
}

// === CalcValueForNormalizedTime (AnimMath.cs:37-83) ported VERBATIM. ===
// PURE: value depends only on (shape, time, componentIndex, bias, ratio). componentIndex de-correlates
// the per-channel Random/Perlin seeds for the vec animators (AnimVec2/3 pass 0/1/2); AnimValue passes 0.
inline float calcValueForNormalizedTime(Shapes shape, double time, int componentIndex,
                                        float bias, float ratio) {
  float result = 0.0f;
  const int s = (int)shape;
  switch (shape) {
    case Shapes::Ramps:
    case Shapes::Saws:
    case Shapes::Wave:
    case Shapes::Square:
    case Shapes::ZigZag:
    case Shapes::KickSaws:
      result = schlickBias(mapShape(s, clampf((float)fmodFloored(time, 1.0) / ratio, 0.0f, 1.0f)), bias);
      break;

    case Shapes::Sin:
      result = schlickBiasWithNegative(mapShape(s, clampf((float)fmodFloored(time, 1.0) / ratio, 0.0f, 1.0f)), bias);
      break;

    case Shapes::Random:
      result = schlickBias((float)((double)xxHash((uint32_t)(time + 28657.0 * componentIndex)) / (double)UINT32_MAX), bias);
      break;

    case Shapes::RandomSigned:
      result = schlickBias((float)((double)xxHash((uint32_t)(time + 28657.0 * componentIndex)) / (double)UINT32_MAX), bias) * 2.0f - 1.0f;
      break;

    case Shapes::Endless: {
      const float fraction = clampf((float)fmodFloored(time, 1.0) / ratio, 0.0f, 1.0f);
      result = (float)((int)time) + schlickBias(fraction, bias);
      break;
    }

    case Shapes::PerlinNoise:
      result = schlickBiasWithNegative(perlinNoise((float)time, 1.0f, 5, 43 * componentIndex) * 1.25f, bias) / 2.0f + 0.5f;
      break;

    case Shapes::PerlinNoiseSigned:
      result = schlickBiasWithNegative(perlinNoise((float)time, 1.0f, 5, 43 * componentIndex) * 1.25f, bias);
      break;

    case Shapes::Steps:
      result = (float)((int)time);
      break;
  }
  return result;
}

}  // namespace anim_math
}  // namespace sw
