// Host<->shader params for TiXL points/draw-sorted/sort-1-CleanBucketCounter — mathv transpiler-batch
// kernel INVENTORY (2026-07-10, MATH_VERIFY_WORKFLOW.md §10 wave-4). NOT wired to node_registry /
// t3_import — a verified-kernel-in-stock entry; connecting it to a stage is a separate future lane.
//
// Mirrors external/tixl .../Assets/shaders/points/draw-sorted/sort-1-CleanBucketCounter.hlsl. Owner:
// external/tixl/Operators/Lib/point/draw/DrawPointsDOF.t3 (a large 67-child compound; this is the
// FIRST of six numbered sort-N passes it dispatches to depth-sort points for OIT draw — only THIS
// pass's kernel is ported/verified here, per §10's per-.hlsl-file granularity; sort-2/4/5/6 are
// separate targets, see docs/agent/census/ENGINE_GAP_BUFFER_SHAPES.md).
//
// Entry name is `ClearBucketCounter`, NOT `main` (§10.5②) — confirmed via `grep numthreads -A2`.
//
// For each dispatch thread (one per bucket): zeroes BucketCounter[threadIdx] iff threadIdx <
// BucketCount. `ParticleCount` IS a real HLSL cbuffer field (b0) but is NEVER READ by this kernel
// (only BucketCount is used) — kept in the params struct anyway to mirror the HLSL cbuffer layout
// byte-for-byte (no host-ABI reason to drop an unread field; the adapter binds the SAME cbuffer the
// production DrawPointsDOF compound would send to all six sort-N passes).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct CleanBucketCounterParams {
#ifdef __METAL_VERSION__
  int BucketCount;
  int ParticleCount;  // unread by this kernel (see header note); present for cbuffer-layout fidelity
#else
  int32_t BucketCount;
  int32_t ParticleCount;
#endif
};

// Binding numbers read off the ACTUAL glslang+spirv-cross raw output (§10.5③ — declaration order,
// not a formula): Params(b0)->buffer(0), BucketCounter(u0)->buffer(1).
enum CleanBucketCounterBinding {
  CLEANBUCKETCOUNTER_Params        = 0,  // constant CleanBucketCounterParams& (b0)
  CLEANBUCKETCOUNTER_BucketCounter = 1,  // device uint*                       (u0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(CleanBucketCounterParams) == 8, "CleanBucketCounterParams must be 8 bytes (2 int32)");
#endif
