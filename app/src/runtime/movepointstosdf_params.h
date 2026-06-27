// Shared host<->shader params for the TiXL-ported MoveToSDF point MODIFIER (SDF point-modify seam).
// Mirrors external/tixl .../Assets/shaders/points/modify/MovePointsToSDF.hlsl (b0/b2 scalars; the field's
// FloatParams ride a SEPARATE assembled buffer at b1 in TiXL / slot 3 here, owned by assembleFieldMSL).
//
// A count-preserving modifier: reads an input bag, raymarches each point to the wired SDF field's surface,
// and lerps Position toward the converged surface point by Amount. Count is INHERITED from upstream.
//
// We flatten b0 (Amount/MinDistance/StepDistanceFactor/NormalSamplingDistance) + b2 (MaxSteps) into one
// 32-byte struct (no packed_float3 / matrix traps — the particle_params.h discipline). The kernel reads
// MaxSteps/Count as ints/uints. 16-byte alignment maintained via static_assert. This struct's layout is
// byte-identical to the inlined `MoveToSdfParams` in shaders/templates/move_points_to_sdf_template.metal.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct MoveToSdfParams {
  float Amount;                  // .cs Amount, default 1.0 (lerp toward surface)
  float MinDistance;             // .cs MinDistance, default 0.005 (raymarch stop threshold)
  float StepDistanceFactor;      // .cs StepDistanceFactor, default 0.5 (march step scale)
  float NormalSamplingDistance;  // .cs NormalSamplingDistance, default 0.1 (finite-diff h)
#ifdef __METAL_VERSION__
  uint  Count;                   // inherited from upstream bag (modifier: count from input)
  int   MaxSteps;                // .cs MaxSteps, default 20 (raymarch iteration cap)
  float _pad0;                   // align
  float _pad1;                   // align -> 32 bytes (2x16)
#else
  uint32_t Count;
  int32_t  MaxSteps;
  float    _pad0;
  float    _pad1;
#endif
};

enum MoveToSdfBinding {
  MTS_SourcePoints = 0,  // const device SwPoint* (t0)
  MTS_ResultPoints = 1,  // device SwPoint*       (u0)
  MTS_Params       = 2,  // constant MoveToSdfParams& (b0)
  MTS_FieldParams  = 3,  // constant FieldParams&  (the assembled field's packed buffer)
};

#ifndef __METAL_VERSION__
// Amount(4)+MinDistance(4)+StepDistanceFactor(4)+NormalSamplingDistance(4) = 16
// + Count(4)+MaxSteps(4)+pad0(4)+pad1(4) = 16
// Total = 32 bytes
static_assert(sizeof(MoveToSdfParams) == 32, "MoveToSdfParams must be 32 bytes (2x16)");
#endif
