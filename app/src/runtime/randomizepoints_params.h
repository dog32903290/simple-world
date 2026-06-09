// Shared host<->shader params for the TiXL-ported RandomizePoints MODIFIER (lane A, batch 2).
// Mirrors external/tixl .../Assets/shaders/points/modify/RandomizePoints.hlsl (the .cs only
// declares slots — the per-point math lives entirely in the .hlsl). The .hlsl splits its data
// over two cbuffers (b0 floats + b1 ints); we send one all-scalar struct so there is no
// packed_float3 / matrix alignment trap (the particle_params.h / transformpoints_params.h
// discipline). Vectors become NameX/NameY/NameZ[/W]; the kernel reassembles them.
//
// A MODIFIER: reads an input bag, jitters per-point attributes by a hash-driven pseudo-random
// offset, writes a count-preserving output bag. Naming follows the .hlsl cbuffer fields
// (Randomize* = the per-attribute magnitude), not the .cs slot names (Position/Rotation/...).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// cbuffer Params (b0) + IntParams (b1) of RandomizePoints.hlsl, flattened to all-scalar.
struct RandomizeParams {
#ifdef __METAL_VERSION__
  uint Count;  // inherited from the input bag (modifier: count comes from upstream Points)
#else
  uint32_t Count;
#endif
  float Strength;  // master magnitude (.cs Strength, b0)

  // Per-attribute jitter magnitudes (the .cs vector/scalar inputs -> b0 Randomize* fields).
  float RandomizePositionX, RandomizePositionY, RandomizePositionZ;  // .cs Position (Vector3)
  float RandomizeRotationX, RandomizeRotationY, RandomizeRotationZ;  // .cs Rotation (Vector3, degrees)
  float RandomizeColorX, RandomizeColorY, RandomizeColorZ, RandomizeColorW;  // .cs ColorHSB (Vector4, HSBa)
  float StretchX, StretchY, StretchZ;  // .cs Stretch (Vector3, per-axis scale jitter)
  float Scale;                         // .cs Scale (float, uniform scale jitter)
  float RandomizeF1;                   // .cs F1 (float)
  float RandomizeF2;                   // .cs F2 (float)
  float RandomSeed;                    // .cs RandomPhase (float, phase offset)
  float GainAndBiasX, GainAndBiasY;    // .cs GainAndBias (Vector2)

  // IntParams (b1). Stored as 32-bit ints; the host derives them from float param ports.
#ifdef __METAL_VERSION__
  uint OffsetMode;     // 0 = Add, 1 = Scatter (.cs OffsetModes)
  uint UsePointSpace;  // 0 = PointSpace, 1 = ObjectSpace (.cs Spaces; .hlsl: 0 -> qRotate into point frame)
  uint Interpolation;  // 0 = None, 1 = Linear, 2 = Smooth (.cs Interpolations)
  int  ClampColorsEtc; // .cs ClampColorsEtc (bool)
  int  Repeat;         // .cs Repeat (0 = no repeat -> mod 999999999)
  int  StrengthFactor; // 0 = None, 1 = F1, 2 = F2 (.cs StrengthFactors)
#else
  uint32_t OffsetMode;
  uint32_t UsePointSpace;
  uint32_t Interpolation;
  int32_t  ClampColorsEtc;
  int32_t  Repeat;
  int32_t  StrengthFactor;
#endif
  float _pad0;  // pad to a 16-byte multiple (see static_assert)
};

enum RandomizeBinding {
  RANDOMIZE_SourcePoints = 0,  // const device SwPoint* (t0)
  RANDOMIZE_ResultPoints = 1,  // device SwPoint*       (u0)
  RANDOMIZE_Params = 2,        // constant RandomizeParams& (b0)
};

#ifndef __METAL_VERSION__
// 27 scalars (all 4-byte: 1 uint + 20 floats + 6 ints) = 108, + 1 pad float = 112 = 16*7.
static_assert(sizeof(RandomizeParams) == 112, "RandomizeParams 112 bytes (16-byte multiple)");
#endif
