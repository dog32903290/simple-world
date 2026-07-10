// Host<->shader params for TiXL mesh-FlipNormals — mathv transpiler-batch kernel INVENTORY
// (2026-07-10, MATH_VERIFY_WORKFLOW.md §10 wave-2). NOT wired to node_registry / t3_import — a
// verified-kernel-in-stock entry; connecting it to a stage is a separate future lane (owner-lock).
//
// Mirrors external/tixl .../Assets/shaders/3d/mesh/mesh-FlipNormals.hlsl. The HLSL cbuffer Params :
// register(b0) is EMPTY — zero real TiXL parameters, this is a pure per-vertex field remap (negate
// Normal/Tangent, passthrough Position/Bitangent/TexCoord/Selected/ColorRGB). Owner: external/tixl/
// Operators/Lib/mesh/modify/FlipNormals.t3 (dispatches this AND mesh-ReverseFaceVertexIndexOrder.hlsl
// — the two together flip a mesh's winding+shading; each is ported as its own mathv op per §10's
// per-.hlsl-file granularity).
//
// NOTED-QUIRK, FAITHFULLY PRESERVED (not a fork — this IS the transcription target, MATH_VERIFY_
// WORKFLOW.md §10.3): the HLSL body assigns Position/Normal/Tangent/Bitangent/TexCoord/Selected/
// ColorRGB but NEVER TexCoord2 (.hlsl:21-27 — 7 of 8 PbrVertex fields, TexCoord2 silently missing).
// ResultVerts[i].TexCoord2 is therefore whatever was already in the destination buffer BEFORE
// dispatch — both the transpiled kernel and the CPU ref leave it untouched.
//
// `Count` is NOT an HLSL cbuffer field — host-ABI-only (§10.2③/§10.5①, replaces
// SourceVerts.GetDimensions(numStructs,stride)).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct FlipNormalsParams {
#ifdef __METAL_VERSION__
  uint Count;
#else
  uint32_t Count;
#endif
};

enum FlipNormalsBinding {
  FLIPNORMALS_SourceVerts = 0,  // const device SwVertex* (t0)
  FLIPNORMALS_ResultVerts = 1,  // device SwVertex*       (u0)
  FLIPNORMALS_Params      = 2,  // constant FlipNormalsParams& (host ABI, not HLSL b0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(FlipNormalsParams) == 4, "FlipNormalsParams must be 4 bytes (1 uint)");
#endif
