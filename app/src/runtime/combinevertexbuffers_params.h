// Host<->shader params for TiXL mesh-CombineVertexBuffers — mathv transpiler-batch kernel INVENTORY
// (2026-07-10, MATH_VERIFY_WORKFLOW.md §10 wave-2). NOT wired to node_registry / t3_import — a
// verified-kernel-in-stock entry; connecting it to a stage is a separate future lane (owner-lock).
//
// Mirrors external/tixl .../Assets/shaders/3d/mesh/_/mesh-CombineVertexBuffers.hlsl — TWO HLSL
// cbuffers (b0 startVertexIndex, b1 DebugValue), flattened into one host ABI struct (§10.2③ dual-
// cbuffer reconstruction, same pattern as AddNoise's Params/Params_1). Owner: external/tixl/
// Operators/Lib/mesh/modify/CombineMeshes.t3 (sibling of CombineIndexBuffers — dispatched once per
// source mesh's vertex buffer; DebugValue is a debug-visualization nudge on Position.y, dead in
// production use but faithfully reproduced).
//
// `Count` is NOT an HLSL cbuffer field — host-ABI-only (§10.2③/§10.5①, replaces
// Vertices.GetDimensions(size,stride)).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct CombineVertexBuffersParams {
#ifdef __METAL_VERSION__
  int   StartVertexIndex;  // b0 .hlsl:5 startVertexIndex
  float DebugValue;        // b1 .hlsl:10 DebugValue
  uint  Count;              // host ABI: Vertices SRV element count
#else
  int32_t  StartVertexIndex;
  float    DebugValue;
  uint32_t Count;
#endif
};

enum CombineVertexBuffersBinding {
  // Raw glslang+spirv-cross output ALIASED Params(b0) onto the SAME buffer slot as ResultVertices(u0)
  // (a spvBufferAliasSet0Binding0 pointer-cast hack) — a genuine binding COLLISION from the --hlsl-
  // iomap shift-flag scheme on this specific dual-cbuffer+SRV+UAV combination, not a usable layout.
  // This is a §10.5③-adjacent but WORSE case (not just "order differs" — an actual aliasing bug), so
  // the adapter assigns FRESH, non-colliding bindings instead of reusing the raw numbers (documented
  // here rather than silently copied, same discipline as the packed_float3/packed_int3 struct fixes).
  CVB_Vertices       = 0,  // const device SwVertex* (t0)
  CVB_ResultVertices = 1,  // device SwVertex*       (u0)
  CVB_Params         = 2,  // constant CombineVertexBuffersParams&
};

#ifndef __METAL_VERSION__
static_assert(sizeof(CombineVertexBuffersParams) == 12, "CombineVertexBuffersParams must be 12 bytes");
#endif
