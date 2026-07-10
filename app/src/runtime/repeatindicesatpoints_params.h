// Host<->shader params for TiXL mesh-RepeatIndicesAtPoints — mathv transpiler-batch kernel INVENTORY
// (2026-07-10, MATH_VERIFY_WORKFLOW.md §10 wave-2). NOT wired to node_registry / t3_import — a
// verified-kernel-in-stock entry; connecting it to a stage is a separate future lane (owner-lock).
//
// Mirrors external/tixl .../Assets/shaders/3d/mesh/mesh-RepeatIndicesAtPoints.hlsl. HLSL declares TWO
// cbuffers (b0: Stretch/UseWForSize/Size/UseStretch, b1: VertexCount/PointCount) but this kernel's
// body only ever reads VertexCount — the b0 fields (and b1's PointCount) are consumed exclusively by
// the SIBLING kernel mesh-RepeatVerticesAtPoints.hlsl (already ported, repeatverticesatpoints.metal);
// glslang/spirv-cross's dead-code elimination drops the unreferenced b0 cbuffer entirely from THIS
// file's SPIR-V, so the raw transpile output only carries {VertexCount, PointCount}. Owner: external/
// tixl/Operators/Lib/mesh/generate/RepeatMeshAtPoints.t3.
//
// `Count` is NOT an HLSL cbuffer field — host-ABI-only (§10.2③/§10.5①, replaces
// SourceFaces.GetDimensions(sourceFaceCount,stride)).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct RepeatIndicesAtPointsParams {
#ifdef __METAL_VERSION__
  int  VertexCount;  // b1 .hlsl:16 VertexCount — the per-instance vertex-index rebase stride
  int  PointCount;   // b1 .hlsl:17 PointCount — declared, unread by main() (see header note above)
  uint Count;         // host ABI: SourceFaces SRV element count
#else
  int32_t  VertexCount;
  int32_t  PointCount;
  uint32_t Count;
#endif
};

enum RepeatIndicesAtPointsBinding {
  // Binding numbers match the ACTUAL glslang+spirv-cross transpile output for this file (§10.5③).
  RIATP_SourceFaces = 0,  // const device SwTriIndex* (t0) — packed_int3 tight stride
  RIATP_ResultFaces = 1,  // device SwTriIndex*       (u0)
  RIATP_Params      = 2,  // constant RepeatIndicesAtPointsParams&
};

#ifndef __METAL_VERSION__
static_assert(sizeof(RepeatIndicesAtPointsParams) == 12, "RepeatIndicesAtPointsParams must be 12 bytes");
#endif
