// Shared host<->shader params for the TiXL-ported TransformPoints MODIFIER (lane A, batch 2).
// Mirrors external/tixl .../Assets/shaders/points/modify/TransformPoints.hlsl, but the TRS is
// passed as scalar components (the .hlsl gets a prebuilt float4x4 + a separate b1 int cbuffer;
// we send the raw TRS so the shader composes it — fewer host deps, and all-scalar = no
// packed_float3 / matrix alignment traps, the particle_params.h discipline).
//
// This is the FIRST modifier op (reads an input bag -> writes an output bag), so it doubles as
// the modifier leaf TEMPLATE for the batch-2 fan-out (OrientPoints/RandomizePoints/FilterPoints).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// cbuffer Params — TransformPoints. TRS components + Space + Strength, all scalar.
struct TransformParams {
#ifdef __METAL_VERSION__
  uint Count;   // inherited from the input bag (modifier: count comes from upstream Points)
  int  Space;   // 0 = PointSpace (offset in each point's own frame), 1 = ObjectSpace (world)
#else
  uint32_t Count;
  int32_t  Space;
#endif
  float TranslationX, TranslationY, TranslationZ;  // TiXL Translation (Vector3)
  float RotationX, RotationY, RotationZ;           // TiXL Rotation (Vector3, Euler degrees)
  float StretchX, StretchY, StretchZ;              // TiXL Stretch (Vector3, per-axis scale)
  float Scale;                                     // TiXL Scale (uniform), multiplies Stretch
  float PivotX, PivotY, PivotZ;                    // TiXL Pivot (Vector3, center of transform)
  float Strength;                                  // TiXL Strength (0..1 lerp old->new)
};

enum TransformBinding {
  TRANSFORM_SourcePoints = 0,  // const device SwPoint* (t0)
  TRANSFORM_ResultPoints = 1,  // device SwPoint*       (u0)
  TRANSFORM_Params = 2,        // constant TransformParams& (b0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(TransformParams) == 64, "TransformParams 64 bytes (16-byte multiple)");
#endif
