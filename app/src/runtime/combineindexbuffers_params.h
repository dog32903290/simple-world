// Host<->shader params for TiXL mesh-CombineIndexBuffers — mathv transpiler-batch kernel INVENTORY
// (2026-07-10, MATH_VERIFY_WORKFLOW.md §10 wave-2). NOT wired to node_registry / t3_import — a
// verified-kernel-in-stock entry; connecting it to a stage is a separate future lane (owner-lock).
//
// Mirrors external/tixl .../Assets/shaders/3d/mesh/_/mesh-CombineIndexBuffers.hlsl cbuffer Params :
// register(b0) {int StartIndex; int StartVertex;}. Owner: external/tixl/Operators/Lib/mesh/modify/
// CombineMeshes.t3 (dispatches this once per source mesh's index buffer, offsetting into a shared
// combined result — StartIndex = where this mesh's faces land, StartVertex = vertex-index rebase so
// face indices point into the combined vertex buffer).
//
// `Count` is NOT an HLSL cbuffer field — host-ABI-only, added by the transpiler adapter (§10.2③/
// §10.5①) to replace `Indices.GetDimensions(size,stride)`.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct CombineIndexBuffersParams {
#ifdef __METAL_VERSION__
  int  StartIndex;   // .hlsl :3 cbuffer Params.StartIndex
  int  StartVertex;  // .hlsl :4 cbuffer Params.StartVertex
  uint Count;         // host ABI: Indices SRV element count (GetDimensions replacement)
#else
  int32_t  StartIndex;
  int32_t  StartVertex;
  uint32_t Count;
#endif
};

enum CombineIndexBuffersBinding {
  // Binding numbers match the ACTUAL glslang+spirv-cross transpile output for this file (§10.5③ —
  // NOT declaration order; verified against the raw .metal dump 2026-07-10).
  CIB_Indices       = 0,  // const device SwTriIndex* (t0) — packed_int3 tight stride
  CIB_Params        = 1,  // constant CombineIndexBuffersParams&
  CIB_ResultIndices = 2,  // device SwTriIndex*       (u0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(CombineIndexBuffersParams) == 12, "CombineIndexBuffersParams must be 12 bytes (3 x int32)");
#endif
