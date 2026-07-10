// Host<->shader params for TiXL points/draw-sorted/sort-4-CopyPrefixSum — mathv transpiler-batch
// kernel INVENTORY (2026-07-10, MATH_VERIFY_WORKFLOW.md §10 wave-4). NOT wired to node_registry /
// t3_import — a verified-kernel-in-stock entry; connecting it to a stage is a separate future lane.
//
// Mirrors external/tixl .../Assets/shaders/points/draw-sorted/sort-4-CopyPrefixSum.hlsl. Owner:
// external/tixl/Operators/Lib/point/draw/DrawPointsDOF.t3 (see cleanbucketcounter_params.h header —
// same six-pass sort-N family; this is pass 4 of 6). ParticleCount is unread by this kernel too
// (mirrors sort-1's params layout for cbuffer-binding uniformity across the sort-N family, matching
// what the real DrawPointsDOF compound sends: the SAME {BucketCount,ParticleCount} cbuffer to all
// six passes, even though most passes only read one field).
//
// Entry name is `CopyPrefixSum`, NOT `main` (§10.5②) — confirmed via `grep numthreads -A2`.
//
// For each dispatch thread (one per bucket): copies BucketPrefixSum[idx] -> BucketOffsetSum[idx] iff
// idx < BucketCount (a "snapshot the prefix sum into a scratch buffer before the scatter pass
// mutates it" idiom — sort-5-WriteSortIndices reads BucketOffsetSum via InterlockedAdd, which is why
// this copy exists as its own pass rather than reading BucketPrefixSum directly there).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct CopyPrefixSumParams {
#ifdef __METAL_VERSION__
  int BucketCount;
  int ParticleCount;  // unread by this kernel (see header note)
#else
  int32_t BucketCount;
  int32_t ParticleCount;
#endif
};

// Binding numbers read off the ACTUAL glslang+spirv-cross raw output (§10.5③ — NOT declaration
// order here, read off verbatim): Params(b0)->buffer(0), BucketOffsetSum(u0)->buffer(1)
// [write target], BucketPrefixSum(u1)->buffer(2) [read source]. Note both source and dest are
// RWStructuredBuffer<uint> in the HLSL (BucketPrefixSum is read-only in THIS kernel despite its
// RW-ness, per the HLSL comment "read-only during write pass").
enum CopyPrefixSumBinding {
  COPYPREFIXSUM_Params          = 0,  // constant CopyPrefixSumParams& (b0)
  COPYPREFIXSUM_BucketOffsetSum = 1,  // device uint*       (u0, write target)
  COPYPREFIXSUM_BucketPrefixSum = 2,  // device uint*       (u1, read source)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(CopyPrefixSumParams) == 8, "CopyPrefixSumParams must be 8 bytes (2 int32)");
#endif
