// Host<->shader params for TiXL mesh-ReverseFaceVertexIndexOrder — mathv transpiler-batch kernel
// INVENTORY (2026-07-10, MATH_VERIFY_WORKFLOW.md §10 wave-2). NOT wired to node_registry / t3_import
// — a verified-kernel-in-stock entry; connecting it to a stage is a separate future lane (owner-lock).
//
// Mirrors external/tixl .../Assets/shaders/3d/mesh/mesh-ReverseFaceVertexIndexOrder.hlsl. The HLSL
// cbuffer Params : register(b0) is EMPTY (no fields) — the op has zero real TiXL parameters, it is a
// pure per-face index permutation: ResultIndices[i] = SourceIndices[i].zyx (reverses triangle winding).
//
// `Count` below is NOT an HLSL cbuffer field — it's a host-ABI-only value invented by the transpiler
// adapter (§10.2③/§10.5①) to replace the HLSL body's `SourceIndices.GetDimensions(numStructs,stride)`
// bound-check, which spirv-cross otherwise turns into a `spvBufferSizeConstants[[buffer(25)]]` magic
// buffer. Passing the true element count directly is simpler and is the established convention for
// every mathv op (e.g. runtime/wrappointposition_params.h's Count field).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct ReverseFaceVertexIndexOrderParams {
#ifdef __METAL_VERSION__
  uint Count;
#else
  uint32_t Count;
#endif
};

enum ReverseFaceVertexIndexOrderBinding {
  RFVIO_SourceIndices = 0,  // const device SwTriIndex* (t0) — packed_int3 tight stride (12B/elem)
  RFVIO_ResultIndices = 1,  // device SwTriIndex*       (u0)
  RFVIO_Params        = 2,  // constant ReverseFaceVertexIndexOrderParams& (host ABI, not HLSL b0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(ReverseFaceVertexIndexOrderParams) == 4, "ReverseFaceVertexIndexOrderParams must be 4 bytes (1 uint)");
#endif
