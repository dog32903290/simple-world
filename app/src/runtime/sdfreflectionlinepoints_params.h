// Shared host<->shader params for the TiXL-ported SdfReflectionLinePoints op (SDF point-modify seam +
// count-multiply seam). Mirrors external/tixl .../field/use/SdfReflectionLinePoints.cs (.cs ports) +
// .../Assets/shaders/points/modify/SdfReflectionLinePoints.hlsl (the raymarch-and-reflect kernel).
//
// A COUNT-MULTIPLYING op with a DIRECT "Field" input (TiXL ShaderGraphNode). For each SOURCE point it
// raymarches along the point's forward axis (qRotateVec3((0,0,-1), Rotation)) toward the wired SDF, and
// on each surface hit reflects the ray (HLSL `reflect`) and continues — emitting a polyline per source
// point: [source, hit_0, hit_1, ... , separators]. The output line length is fixed per the .t3 plumbing:
//   pointsPerLine = clamp(MaxReflectionCount, 0, 10) + 3   (SdfReflectionLinePoints.hlsl:84 `MaxReflections+3`;
//   .t3 ClampInt(Max=10,Min=0) on MaxReflectionCount -> AddInts(+3) -> MultiplyInt(srcCount)).
//   output buffer element count = SourceCount * pointsPerLine.
// This combines TWO proven seams: the MoveToSDF SDF-field gather (assembleFieldMSL -> runtime-compiled
// source PSO, hence this is a RUNTIME STRING TEMPLATE, not a precompiled metallib kernel) AND the
// SubdivideLinePoints count-multiply driver path (countTransform static-stash + c.inputCounts[0] source
// count). The dispatch is over SOURCE points (one thread = one source point writes a whole line), guarded
// by SourcePointCount; the cook reads c.inputCounts[0] for SourceCount and c.count for the output size.
//
// FORK (named) — FieldParams ride a SEPARATE assembled buffer (slot SRL_FieldParams), owned by
// assembleFieldMSL, exactly like MoveToSDF's MTS_FieldParams. The .hlsl's b0/b2 scalars are flattened
// into ONE 32-byte struct here (no packed_float3 / matrix; particle_params.h discipline). The two
// non-scalar cbuffers TiXL splits (b0 raymarch scalars, b2 line-shape ints) become one host struct row.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SdfReflectionLineParams {
  float MinDistance;             // b0.MinDistance, .t3 default 0.005 (raymarch stop threshold)
  float StepDistanceFactor;      // b0.StepDistanceFactor, .t3 default 1.0 (march step scale)
  float NormalSamplingDistance;  // b0.NormalSamplingDistance, .t3 default 0.01 (finite-diff h)
  float MaxDistance;             // b0.MaxDistance, .t3 default 100.0 (accumulated-distance cutoff)
#ifdef __METAL_VERSION__
  uint  SourcePointCount;        // b2.SourcePointCount (= input bag count; thread guard)
  int   MaxSteps;                // b2.MaxSteps, .t3 default 40 (per-reflection raymarch iteration cap)
  int   MaxReflections;          // b2.MaxReflections, .t3 default 2 (clamped 0..10 by .t3)
  int   WriteDistanceTo;         // b2.WriteDistanceTo, .t3 default 1=FX1 (None=0/FX1=1/FX2=2) — DEFAULT-ACTIVE
  int   WriteStepCountTo;        // b2.WriteStepCountTo, .t3 default 2=FX2 (None=0/FX1=1/FX2=2) — DEFAULT-ACTIVE
#else
  uint32_t SourcePointCount;
  int32_t  MaxSteps;
  int32_t  MaxReflections;
  int32_t  WriteDistanceTo;
  int32_t  WriteStepCountTo;
#endif
};

enum SdfReflectionLineBinding {
  SRL_SourcePoints = 0,  // const device SwPoint* (t0)
  SRL_ResultPoints = 1,  // device SwPoint*       (u0)
  SRL_Params       = 2,  // constant SdfReflectionLineParams& (b0)
  SRL_FieldParams  = 3,  // constant FieldParams& (the assembled field's packed buffer)
};

#ifndef __METAL_VERSION__
// 4 floats (16B) + 5 int32 (20B) = 36 bytes, all 4-byte scalars (no packed_float3 / matrix) so the
// struct is naturally 4-byte aligned and 36 bytes — Metal `constant ...&` accepts it (no 16B-multiple
// requirement for a setBytes argument-buffer scalar struct; the field FloatParams ride a SEPARATE buffer).
static_assert(sizeof(SdfReflectionLineParams) == 36,
              "SdfReflectionLineParams must be 36 bytes (4 floats + 5 int32)");
#endif
