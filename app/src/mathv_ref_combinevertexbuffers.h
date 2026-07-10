#pragma once
// mathv_ref_combinevertexbuffers — CPU scalar oracle for TiXL mesh-CombineVertexBuffers (3d/mesh,
// vertex-buffer utility; owner: external/tixl/Operators/Lib/mesh/modify/CombineMeshes.t3).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/3d/mesh/_/mesh-CombineVertexBuffers.hlsl — NOT derived from sw's MSL
// kernel (app/shaders/combinevertexbuffers.metal intentionally never opened while writing this file).
//   cbuffer Params b0 (startVertexIndex)  :3-6
//   cbuffer Params b1 (DebugValue)        :8-11
//   main() body                           :16-28
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper — pure host arithmetic transcribed from the HLSL text above.
//
// SCOPE: this op copies the WHOLE PbrVertex (Position/Normal/Tangent/Bitangent/TexCoord/TexCoord2/
// Selected/ColorRGB) then nudges Position.y by DebugValue — the ref models Position (the only field
// the kernel actually MUTATES beyond a straight copy) plus one other field (Normal) as a copy-fidelity
// witness; the fuzz TU asserts both.
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
#include <cstdint>

namespace sw {
namespace mathv_ref {

struct CombineVertexBuffersParams {
  float debugValue;
};
struct CombineVertexBuffersIn {
  float posX, posY, posZ;
  float normX, normY, normZ;
};
struct CombineVertexBuffersOut {
  float posX, posY, posZ;
  float normX, normY, normZ;
};

// combineVertexBuffersOne — HLSL main():26-27:
//   ResultVertices[targetIndex] = Vertices[i.x];                    // :26 (straight copy)
//   ResultVertices[targetIndex].Position.y += DebugValue;           // :27 (Position.y nudge only)
inline void combineVertexBuffersOne(const CombineVertexBuffersIn& in, CombineVertexBuffersOut& out,
                                    const CombineVertexBuffersParams& p) {
  out.posX = in.posX;
  out.posY = in.posY + p.debugValue;
  out.posZ = in.posZ;
  out.normX = in.normX;  // copy-fidelity witness: Normal is untouched by the kernel
  out.normY = in.normY;
  out.normZ = in.normZ;
}

}  // namespace mathv_ref
}  // namespace sw
