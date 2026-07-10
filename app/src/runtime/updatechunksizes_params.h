// Host<->shader params for TiXL mesh/chunks/MeshChunks-UpdateChunkSizes — mathv transpiler-batch
// kernel INVENTORY (2026-07-10, MATH_VERIFY_WORKFLOW.md §10 wave-3). NOT wired to node_registry /
// t3_import — a verified-kernel-in-stock entry; connecting it to a stage is a separate future lane.
//
// Mirrors external/tixl .../Assets/shaders/3d/mesh/chunks/MeshChunks-UpdateChunkSizes.hlsl. Owner:
// external/tixl/Operators/Lib/mesh/draw/DrawMeshChunksAtPoints.t3 (a large 59-child compound; this is
// ONE of several ComputeShader kernels it dispatches — only THIS kernel is ported/verified here, per
// §10's per-.hlsl-file granularity; the op's other kernels, e.g. the sort-5/6 multi-UAV ones, are a
// separate/later target, see docs/agent/census/ENGINE_GAP_BUFFER_SHAPES.md).
//
// Entry name is `UpdateChunkSizes`, NOT `main` (§10.5②) — confirmed via `grep numthreads -A2`.
//
// For each dispatch thread (one per point): looks up which chunk that point belongs to
// (ChunkIndicesForPoints[pointIndex % ChunkIndexForPointsCounts], a defensive wraparound index —
// HLSL `%` on ints is TRUNCATED/C-style per HLSL spec, sign of dividend), then writes that chunk's
// FaceCount into ChunkSizes[pointIndex]. Two int mods total; both operands are non-negative in the
// realistic domain (thread index >=0, buffer-stored indices are indices not signed deltas) — the fuzz
// domain here is deliberately kept non-negative for both dividend/divisor (see mathv_ref/selftest
// header notes) so the floored-vs-truncated `%` question (glslang emits SPIR-V's OpSMod/spvSMod, which
// is FLOORED not HLSL's spec'd truncated-toward-dividend — verified by reading the raw transpiler
// output) never actually forks: floored and truncated mod agree whenever both operands are >=0.
//
// `PointCount`/`ChunkDefCount`/`ChunkIndexForPointsCounts` ARE real HLSL cbuffer fields (b0, all three
// present verbatim) — no GetDimensions/host-ABI substitution needed for this op (§10.5① N/A here).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct UpdateChunkSizesParams {
#ifdef __METAL_VERSION__
  int Count;                     // PointCount (HLSL cbuffer field)
  int ChunkDefCount;
  int ChunkIndexForPointsCounts;
#else
  int32_t Count;
  int32_t ChunkDefCount;
  int32_t ChunkIndexForPointsCounts;
#endif
};

// HLSL struct ChunkDef (3d/mesh/chunks/MeshChunks-UpdateChunkSizes.hlsl:11-16): 4 plain ints, no
// vec3/float3 fields -> no packed_ trap (only vector-typed members need the packed_ override; scalar
// int members are naturally 4-byte aligned/packed on both HLSL and MSL).
struct SwChunkDef {
#ifdef __METAL_VERSION__
  int StartFaceIndex;
  int FaceCount;
  int StartVertexIndex;
  int VertexCount;
#else
  int32_t StartFaceIndex;
  int32_t FaceCount;
  int32_t StartVertexIndex;
  int32_t VertexCount;
#endif
};

// Binding numbers read off the ACTUAL glslang+spirv-cross raw output (§10.5③ — NOT a formula; this
// op's declaration order is Params(b0)/ChunkIndicesForPoints(t0)/ChunkDefs(t1)/ChunkSizes(u0), but the
// compacted [[buffer(N)]] numbering spirv-cross emitted interleaves u0 BEFORE t1: 0=Params, 1=t0,
// 2=u0, 3=t1 — confirmed by reading /tmp raw transpile output verbatim, not assumed).
enum UpdateChunkSizesBinding {
  UCS_Params                 = 0,  // constant UpdateChunkSizesParams& (b0)
  UCS_ChunkIndicesForPoints  = 1,  // const device int*        (t0)
  UCS_ChunkSizes             = 2,  // device uint*              (u0)
  UCS_ChunkDefs              = 3,  // const device SwChunkDef*  (t1)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(UpdateChunkSizesParams) == 12, "UpdateChunkSizesParams must be 12 bytes (3 int32)");
static_assert(sizeof(SwChunkDef) == 16, "SwChunkDef must be 16 bytes (4 int32), matches HLSL ChunkDef");
#endif
