#pragma once
// mathv_ref_reversefacevertexindexorder — CPU scalar oracle for TiXL mesh-ReverseFaceVertexIndexOrder
// (3d/mesh, index-buffer utility; owner: external/tixl/Operators/Lib/mesh/modify/FlipNormals.t3).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/3d/mesh/mesh-ReverseFaceVertexIndexOrder.hlsl — NOT derived from sw's
// MSL kernel (app/shaders/reversefacevertexindexorder.metal intentionally never opened while writing
// this file).
//   cbuffer Params (EMPTY — zero real fields)     :3-6
//   main() body                                   :12-21 -- ResultIndices[i] = SourceIndices[i].zyx
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper — pure host arithmetic transcribed from the HLSL text above.
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
#include <cstdint>

namespace sw {
namespace mathv_ref {

struct ReverseFaceVertexIndexOrderIn {
  int32_t x, y, z;  // one triangle's SourceIndices[i] (Int3, TiXL vertex-index triple)
};
struct ReverseFaceVertexIndexOrderOut {
  int32_t x, y, z;
};

// reverseFaceVertexIndexOrderOne — HLSL main():20 `ResultIndices[i.x] = SourceIndices[i.x].zyx;`
// Pure swizzle: reverses the triangle's winding order (x,y,z) -> (z,y,x). No cbuffer params consumed
// (the HLSL Params block is declared empty).
inline void reverseFaceVertexIndexOrderOne(const ReverseFaceVertexIndexOrderIn& in,
                                           ReverseFaceVertexIndexOrderOut& out) {
  out.x = in.z;
  out.y = in.y;
  out.z = in.x;
}

}  // namespace mathv_ref
}  // namespace sw
