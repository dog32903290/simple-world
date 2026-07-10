#pragma once
// mathv_ref_combineindexbuffers — CPU scalar oracle for TiXL mesh-CombineIndexBuffers (3d/mesh, index-
// buffer utility; owner: external/tixl/Operators/Lib/mesh/modify/CombineMeshes.t3).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/3d/mesh/_/mesh-CombineIndexBuffers.hlsl — NOT derived from sw's MSL
// kernel (app/shaders/combineindexbuffers.metal intentionally never opened while writing this file).
//   cbuffer Params (StartIndex,StartVertex)  :1-5
//   main() body                              :11-24
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper — pure host arithmetic transcribed from the HLSL text above.
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
#include <cstdint>

namespace sw {
namespace mathv_ref {

struct CombineIndexBuffersParams {
  int32_t startIndex;
  int32_t startVertex;
};
struct CombineIndexBuffersIn {
  int32_t x, y, z;  // Indices[i.x] (one face's three vertex indices)
};
struct CombineIndexBuffersOut {
  int32_t x, y, z;  // written to ResultIndices[i.x + StartIndex]
};

// combineIndexBuffersOne — HLSL main():20,22-23:
//   uint targetIndex = i.x + (int)StartIndex;                      // :20 (index REBASE, not modeled
//                                                                       here — caller places the
//                                                                       result at that slot)
//   int3 faceIndices = Indices[i.x] + StartVertex;                 // :22
//   ResultIndices[targetIndex] = faceIndices;                      // :23
// This function only computes the VALUE written (faceIndices); the TARGET SLOT (targetIndex) is an
// index-remap the fuzz TU applies itself when it reads back ResultIndices[i.x + StartIndex] — same
// split responsibility as SimBlendTo's ref (value math here, dispatch/slot bookkeeping in the TU).
inline void combineIndexBuffersOne(const CombineIndexBuffersIn& in, CombineIndexBuffersOut& out,
                                   const CombineIndexBuffersParams& p) {
  out.x = in.x + p.startVertex;
  out.y = in.y + p.startVertex;
  out.z = in.z + p.startVertex;
}

}  // namespace mathv_ref
}  // namespace sw
