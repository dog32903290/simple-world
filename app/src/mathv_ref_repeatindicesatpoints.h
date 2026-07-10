#pragma once
// mathv_ref_repeatindicesatpoints — CPU scalar oracle for TiXL mesh-RepeatIndicesAtPoints (3d/mesh,
// instancing utility; owner: external/tixl/Operators/Lib/mesh/generate/RepeatMeshAtPoints.t3).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/3d/mesh/mesh-RepeatIndicesAtPoints.hlsl — NOT derived from sw's MSL
// kernel (app/shaders/repeatindicesatpoints.metal intentionally never opened while writing this file).
//   cbuffer Params b1 (VertexCount,PointCount — only VertexCount is read)   :14-18
//   main() body                                                             :27-42
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper — pure host arithmetic transcribed from the HLSL text above.
//
// SCOPE: pure integer index remap, EXACT class (no floats at all). targetFaceIndex (the write SLOT)
// is index bookkeeping the fuzz TU applies itself when reading back ResultFaces[y*Count+x] — same
// split as combineindexbuffers' ref (value math here, slot bookkeeping in the TU).
#include <cstdint>

namespace sw {
namespace mathv_ref {

struct RepeatIndicesAtPointsIn {
  int32_t x, y, z;  // SourceFaces[faceIndex] (one face's three vertex indices)
};
struct RepeatIndicesAtPointsOut {
  int32_t x, y, z;
};

// repeatIndicesAtPointsOne — HLSL main():41:
//   ResultFaces[targetFaceIndex] = SourceFaces[faceIndex] + VertexCount * pointIndex;
// pointIndex (the dispatch y-coordinate) is passed in as `pointIndex` — VertexCount*pointIndex is the
// per-instance vertex-index rebase (instance k's vertices start at k*VertexCount in the combined
// vertex buffer built by the sibling mesh-RepeatVerticesAtPoints.hlsl kernel).
inline void repeatIndicesAtPointsOne(const RepeatIndicesAtPointsIn& in, RepeatIndicesAtPointsOut& out,
                                     int32_t vertexCount, uint32_t pointIndex) {
  int32_t rebase = vertexCount * (int32_t)pointIndex;
  out.x = in.x + rebase;
  out.y = in.y + rebase;
  out.z = in.z + rebase;
}

}  // namespace mathv_ref
}  // namespace sw
