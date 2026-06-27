// Shared host<->shader params for the TiXL-ported RaymarchPoints op (SDF point-modify seam +
// count-multiply seam). Mirrors external/tixl .../field/use/RaymarchPoints.cs (.cs ports) +
// .../Assets/shaders/points/modify/MovePointsForwardToSDF.hlsl (the two-mode raymarch kernel).
//
// A COUNT-MULTIPLYING op with a DIRECT "Field" input (TiXL ShaderGraphNode). For each SOURCE point it
// raymarches along the point's forward axis (qRotateVec3((0,0,-1), Rotation)) toward the wired SDF. Two
// modes (RaymarchPoints.cs:49-53 enum Modes):
//   PointMode == 0 Raymarch  — keep the source point + one point per reflection bounce (the surface hits),
//                              padded with NaN-Scale separators to the per-line stride.
//   PointMode != 0 KeepSteps — keep the WHOLE march path: every raymarch step becomes an output point,
//                              for every reflection, with NaN-Scale separators between segments.
//
// THE ENTANGLED COUNT (mode-INDEPENDENT — both modes share the same buffer sizing). RaymarchPoints.t3 traces:
//   PointCountPerLine            = CountForALine = CompareInt(PointMode<0 ? 0 : MaxSteps) + 1.
//                                  CompareInt is IsSmaller-vs-0 (TestValue=0, Mode default 0=IsSmaller),
//                                  Value=PointMode -> ALWAYS false (PointMode>=0) -> ResultForFalse=MaxSteps.
//                                  So PointCountPerLine = MaxSteps + 1  (the +1 = the stepIndex<=MaxSteps
//                                  separator slot in KeepSteps mode).
//   PointCountPerLineReflections = CountWithReflections = PointCountPerLine * (clamp(MaxReflectionCount,0,10) + 1).
//   total output count           = SourceCount * PointCountPerLineReflections.
// Raymarch mode OVER-ALLOCATES (it only writes MaxReflections+1 of the PointCountPerLineReflections slots),
// but the .t3 does NOT branch the count per mode — CompareInt(IsSmaller,0) is unconditionally false, so the
// allocation is the SAME for both modes (KeepSteps needs every slot). We faithfully replicate that.
//
// .t3 DEFAULTS (load-bearing audit, GUID-keyed): MaxSteps=20, MaxReflectionCount=0, MinDistance=0.005,
// StepDistanceFactor=1.0, NormalSamplingDistance=0.01, MaxDistance=100, Mode=0 (Raymarch),
// WriteDistanceTo=1 (FX1), WriteStepCountTo=2 (FX2). BOTH Write* are DEFAULT-ACTIVE and target DIFFERENT
// FX slots -> both ported. NOTE MaxReflectionCount default 0 -> clamp(0,0,10)+1 = 1 reflection pass.
//
// FORK (named) — FieldParams ride a SEPARATE assembled buffer (slot RMP_FieldParams), owned by
// assembleFieldMSL, exactly like MoveToSDF / SdfReflectionLinePoints. The .hlsl's b0/b2 scalars are
// flattened into ONE struct here (no packed_float3 / matrix; particle_params.h discipline). TiXL's b2 carries
// PointCountPerLine / PointCountPerLineReflections as ints filled from the count chain; we recompute them on
// the host (identical formula) and pass them in so the kernel's per-line stride matches the allocated count.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct RaymarchPointsParams {
  float MinDistance;             // b0.MinDistance, .t3 default 0.005 (raymarch stop threshold)
  float StepDistanceFactor;      // b0.StepDistanceFactor, .t3 default 1.0 (march step scale)
  float NormalSamplingDistance;  // b0.NormalSamplingDistance, .t3 default 0.01 (finite-diff h)
  float MaxDistance;             // b0.MaxDistance, .t3 default 100.0 (accumulated-distance cutoff)
#ifdef __METAL_VERSION__
  uint  SourcePointCount;        // input bag count (thread guard)
  int   MaxSteps;                // b2.MaxSteps, .t3 default 20 (per-reflection raymarch iteration cap)
  int   MaxReflections;          // b2.MaxReflections, .t3 default 0 (clamped 0..10 by .t3)
  int   PointMode;               // b2.PointMode, .t3 default 0 (0=Raymarch / 1=KeepSteps)
  int   WriteDistanceTo;         // b2.WriteDistanceTo, .t3 default 1=FX1 (None=0/FX1=1/FX2=2)
  int   WriteStepCountTo;        // b2.WriteStepCountTo, .t3 default 2=FX2 (None=0/FX1=1/FX2=2)
  int   PointCountPerLine;       // b2.PointCountPerLine = MaxSteps+1 (per-reflection-segment stride)
  int   PointCountPerLineReflections;  // b2.PointCountPerLineReflections = PointCountPerLine*(MaxRefl+1)
#else
  uint32_t SourcePointCount;
  int32_t  MaxSteps;
  int32_t  MaxReflections;
  int32_t  PointMode;
  int32_t  WriteDistanceTo;
  int32_t  WriteStepCountTo;
  int32_t  PointCountPerLine;
  int32_t  PointCountPerLineReflections;
#endif
};

enum RaymarchPointsBinding {
  RMP_SourcePoints = 0,  // const device SwPoint* (t0)
  RMP_ResultPoints = 1,  // device SwPoint*       (u0)
  RMP_Params       = 2,  // constant RaymarchPointsParams& (b0/b2 flattened)
  RMP_FieldParams  = 3,  // constant FieldParams& (the assembled field's packed buffer)
};

#ifndef __METAL_VERSION__
// 4 floats (16B) + 8 int32 (32B) = 48 bytes, all 4-byte scalars (no packed_float3 / matrix) so the struct
// is naturally 4-byte aligned and 48 bytes — Metal `constant ...&` accepts it (the field FloatParams ride a
// SEPARATE buffer).
static_assert(sizeof(RaymarchPointsParams) == 48,
              "RaymarchPointsParams must be 48 bytes (4 floats + 8 int32)");
#endif
