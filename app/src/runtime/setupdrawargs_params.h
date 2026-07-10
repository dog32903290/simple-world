// Host<->shader params for TiXL points/draw-sorted/sort-6-SetupDrawArgs — mathv transpiler-batch
// kernel INVENTORY (2026-07-10, MATH_VERIFY_WORKFLOW.md §10 wave-4). NOT wired to node_registry /
// t3_import — a verified-kernel-in-stock entry; connecting it to a stage is a separate future lane.
//
// Mirrors external/tixl .../Assets/shaders/points/draw-sorted/sort-6-SetupDrawArgs.hlsl. Owner:
// external/tixl/Operators/Lib/point/draw/DrawPointsDOF.t3 (see cleanbucketcounter_params.h header —
// same six-pass sort-N family; this is pass 6 of 6, the LAST pass). numthreads(1,1,1) — a
// SINGLE-THREAD kernel (not a per-element dispatch); it reads the last element of two prefix-sum
// buffers and writes a fixed 5-uint indirect-draw-args record.
//
// Entry name is `SetupDrawArgs`, NOT `main` (§10.5②) — confirmed via `grep numthreads -A2`.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SetupDrawArgsParams {
#ifdef __METAL_VERSION__
  int BucketCount;
  int ParticleCount;  // unread by this kernel (see cleanbucketcounter_params.h header note)
#else
  int32_t BucketCount;
  int32_t ParticleCount;
#endif
};

// Binding numbers read off the ACTUAL glslang+spirv-cross raw output (§10.5③ — declaration order):
// BucketPrefixSum(u0)->buffer(0), Params(b0)->buffer(1), BucketCounter(u1)->buffer(2),
// DrawArgsBuffer(u2)->buffer(3).
enum SetupDrawArgsBinding {
  SETUPDRAWARGS_BucketPrefixSum = 0,  // device uint* (u0, read)
  SETUPDRAWARGS_Params          = 1,  // constant SetupDrawArgsParams& (b0)
  SETUPDRAWARGS_BucketCounter   = 2,  // device uint* (u1, read)
  SETUPDRAWARGS_DrawArgsBuffer  = 3,  // device uint* (u2, write; needs >=5 elements)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(SetupDrawArgsParams) == 8, "SetupDrawArgsParams must be 8 bytes (2 int32)");
#endif
