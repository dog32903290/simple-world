// Host<->shader params for TiXL points/sim/grid-walk-points — mathv transpiler-batch kernel
// INVENTORY (2026-07-10, MATH_VERIFY_WORKFLOW.md §10 wave-4). NOT wired to node_registry /
// t3_import — a verified-kernel-in-stock entry; connecting it to a stage is a separate future lane.
//
// Mirrors external/tixl .../Assets/shaders/points/sim/grid-walk-points.hlsl. Owner: UNRESOLVED — no
// .t3 in the full external/tixl/Operators/Lib tree references this .hlsl by path or entry name
// (`grep -rl grid-walk-points external/tixl/Operators/Lib` empty); this is the census's documented
// "無法反查 owner" case (ENGINE_GAP_BUFFER_SHAPES.md §「對帳」), an orphaned/unreferenced compute
// shader still counted in the 190-shader census total.
//
// Entry name is `main` (confirmed via `grep numthreads -A2`).
//
// GetDimensions substitution (§10.5①): `ResultPoints.GetDimensions(numStructs, stride)` feeds BOTH
// the dispatch guard (:36) and (indirectly, via the guard's `if` shape) nothing else -- unlike
// randomizepointslegacy1, `numStructs` itself is NEVER read again after the guard (the source's
// `newLocalPosition = clamp(numStructs, 0, GridSize)` at :56 is a DEAD STORE, see
// mathv_ref_gridwalkpoints.h header for the proof). The adapter substitutes a host-ABI `Count`
// field for the guard only.
//
// `SpeedVariation` IS a real HLSL cbuffer field (:12) but is NEVER READ by the kernel body — kept in
// the params struct anyway for cbuffer-layout byte-fidelity (same treatment as
// cleanbucketcounter_params.h's unread ParticleCount).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct GridWalkPointsParams {
#ifdef __METAL_VERSION__
  int   Count;  // host-ABI substitution for GetDimensions (§10.5①) -- NOT an HLSL cbuffer field
  float GridSizeX, GridSizeY, GridSizeZ;      // :8 (packed_float3 in HLSL)
  float Speed;                                 // :9
  float GridOffsetX, GridOffsetY, GridOffsetZ; // :11 (packed_float3 in HLSL)
  float SpeedVariation;                        // :12 (unread by kernel, see header note)
  float TriggerTurn;                           // :14
  float Seed;                                  // :15
  float _pad0;  // -> 16-byte multiple (11 real fields + 1 pad = 12 floats/48 bytes)
#else
  int32_t Count;
  float   GridSizeX, GridSizeY, GridSizeZ;
  float   Speed;
  float   GridOffsetX, GridOffsetY, GridOffsetZ;
  float   SpeedVariation;
  float   TriggerTurn;
  float   Seed;
  float   _pad0;
#endif
};

// Binding numbers read off the ACTUAL glslang+spirv-cross raw output (§10.5③ — declaration order):
// ResultPoints(u0)->buffer(0), Params(b0)->buffer(1). The transpiler's spvBufferSizeConstants
// [[buffer(25)]] slot (§10.5①) is DROPPED -- the adapter substitutes P.Count instead.
enum GridWalkPointsBinding {
  GRIDWALKPOINTS_ResultPoints = 0,  // device SwPoint*             (u0, in-place read+write)
  GRIDWALKPOINTS_Params       = 1,  // constant GridWalkPointsParams& (b0, host-ABI extended)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(GridWalkPointsParams) == 48, "GridWalkPointsParams must be 48 bytes");
#endif
