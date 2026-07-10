// Host<->shader params for TiXL points/modify/RandomizePoints_Legacy1 — mathv transpiler-batch
// kernel INVENTORY (2026-07-10, MATH_VERIFY_WORKFLOW.md §10 wave-4). NOT wired to node_registry /
// t3_import — a verified-kernel-in-stock entry; connecting it to a stage is a separate future lane.
//
// Mirrors external/tixl .../Assets/shaders/points/modify/RandomizePoints_Legacy1.hlsl. Owner:
// external/tixl/Operators/Lib/point/modify/_RandomizePoints_Legacy1.t3 — a LEGACY point randomizer,
// distinct from sw's existing `randomizepoints` kernel (ported from the different, non-legacy
// RandomizePoints.hlsl — see mathv_ref_randomizepointslegacy1.h header note for the full field-set
// comparison proving these are not duplicates).
//
// Entry name is `main` (confirmed via `grep numthreads -A2`).
//
// GetDimensions substitution (§10.5①): `SourcePoints.GetDimensions(pointCount, stride)` feeds the
// `f = pointId/(float)pointCount` phase formula (:42 in the .hlsl) — NOT a dead store this time
// (contrast simulatepoints_params.h, where GetDimensions only fed a dispatch guard). The adapter
// substitutes a host-ABI `Count` field for the SAME value GetDimensions would report (the buffer's
// declared element count).
//
// NO dispatch guard (source-faithful): the HLSL's `if(i.x>=pointCount) return;` (:33-36) is
// COMMENTED OUT in the original source — every dispatched thread runs unconditionally. The host must
// size SourcePoints/ResultPoints to cover the full padded dispatch (numthreads(64,1,1) rounds up),
// same as any kernel with no internal bound guard (PointTrail-Clear precedent, MATH_VERIFY_WORKFLOW's
// wrappointposition pilot).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct RandomizePointsLegacy1Params {
#ifdef __METAL_VERSION__
  int   Count;  // host-ABI substitution for GetDimensions (§10.5①) -- NOT an HLSL cbuffer field
  float RandomizePositionX, RandomizePositionY, RandomizePositionZ;  // :9 (packed_float3 in HLSL)
  float Amount;                                                       // :10
  float RandomizeRotationX, RandomizeRotationY, RandomizeRotationZ;  // :12 (packed_float3 in HLSL)
  float RandomizeW;                                                   // :13
  float UseLocalSpace;                                                // :15
  float Seed;                                                         // :16
  float Bias;                                                         // :18
  float Offset;                                                       // :19
  float UseWAsSelection;                                              // :21
  float _pad0, _pad1;  // -> 16-byte multiple (14 real fields + 2 pad = 16 floats = 64 bytes)
#else
  int32_t Count;
  float   RandomizePositionX, RandomizePositionY, RandomizePositionZ;
  float   Amount;
  float   RandomizeRotationX, RandomizeRotationY, RandomizeRotationZ;
  float   RandomizeW;
  float   UseLocalSpace;
  float   Seed;
  float   Bias;
  float   Offset;
  float   UseWAsSelection;
  float   _pad0, _pad1;
#endif
};

// Binding numbers read off the ACTUAL glslang+spirv-cross raw output (§10.5③ — declaration order):
// SourcePoints(t0)->buffer(0), Params(b0)->buffer(1), ResultPoints(u0)->buffer(2). The transpiler's
// spvBufferSizeConstants [[buffer(25)]] slot (§10.5①) is DROPPED -- the adapter substitutes P.Count.
enum RandomizePointsLegacy1Binding {
  RANDOMIZEPOINTSLEGACY1_SourcePoints = 0,  // const device SwPoint* (t0)
  RANDOMIZEPOINTSLEGACY1_Params       = 1,  // constant RandomizePointsLegacy1Params& (b0, host-ABI extended)
  RANDOMIZEPOINTSLEGACY1_ResultPoints = 2,  // device SwPoint*       (u0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(RandomizePointsLegacy1Params) == 64, "RandomizePointsLegacy1Params must be 64 bytes");
#endif
