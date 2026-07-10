#pragma once
// mathv_ref_updatechunksizes — CPU scalar oracle for TiXL mesh/chunks/MeshChunks-UpdateChunkSizes
// (3d/mesh/chunks; owner: external/tixl/Operators/Lib/mesh/draw/DrawMeshChunksAtPoints.t3, ONE of
// several ComputeShader kernels that compound dispatches — only this kernel is ported here).
//
// TRANSCRIBED from external/tixl (SHA 395c4c55)
// Operators/Lib/Assets/shaders/3d/mesh/chunks/MeshChunks-UpdateChunkSizes.hlsl — NOT derived from sw's
// MSL kernel (app/shaders/updatechunksizes.metal intentionally never opened while writing this file).
//   cbuffer Params (PointCount/ChunkDefCount/ChunkIndexForPointsCounts) :18-22
//   UpdateChunkSizes() body                                              :29-36
//
// HLSL `%` on signed ints is spec'd TRUNCATED (sign follows dividend, C-style) — this ref implements
// that literally via C++'s built-in `%` (also truncated for ints). NOTE (transpiler cross-check, see
// updatechunksizes.metal header): glslang's SPIR-V for this operator lowers to `OpSMod`-style FLOORED
// semantics (spvSMod in the raw transpile output), not `OpSRem`/truncated — a genuine HLSL-spec vs
// transpiler-output divergence IN GENERAL. This ref does NOT special-case it: the fuzz TU deliberately
// keeps both modulo operands non-negative (dividend = thread index or a buffer-stored chunk index,
// both semantically indices, never intentionally negative; divisor = a Count, always >=1), and floored
// mod == truncated mod whenever both operands are >=0 — so this ref's plain C++ `%` and the GPU's
// spvSMod are mathematically required to agree over the tested domain. (Testing genuinely negative
// operands would be testing the transpiler's `%`-lowering choice, not this kernel's math — out of
// scope for this op's mathv case; flagged here for the next op that touches negative-domain int `%`.)
//
// PROVENANCE (GOLDEN_STANDARD.md P5-safe oracle 判準): zero metal include, zero app/shaders/
// reference, zero sw math helper — pure host arithmetic transcribed from the HLSL text above.
//
// ZONE: shell-tier mathv support (pure math; app/src/ root, no runtime/platform/Metal dependency).
#include <cstdint>
#include <vector>

namespace sw {
namespace mathv_ref {

struct UpdateChunkSizesChunkDef {
  int32_t startFaceIndex, faceCount, startVertexIndex, vertexCount;
};

// updateChunkSizesOne — HLSL main():31-35.
//   chunkIndex = ChunkIndicesForPoints[pointIndex % ChunkIndexForPointsCounts]
//   ChunkSizes[pointIndex] = ChunkDefs[chunkIndex % ChunkDefCount].FaceCount
// `pointIndex` here is the dispatch index (caller-supplied, matches SV_DispatchThreadID.x already
// bounds-checked against PointCount by the caller — this ref does not re-check `i.x >= PointCount`,
// mirroring the kernel's dispatch guard being outside the math being modeled).
inline uint32_t updateChunkSizesOne(int32_t pointIndex, const std::vector<int32_t>& chunkIndicesForPoints,
                                     const std::vector<UpdateChunkSizesChunkDef>& chunkDefs,
                                     int32_t chunkIndexForPointsCounts) {
  int32_t lookupIdx = pointIndex % chunkIndexForPointsCounts;
  int32_t chunkIndex = chunkIndicesForPoints[(size_t)lookupIdx];
  int32_t chunkDefCount = (int32_t)chunkDefs.size();
  int32_t defIdx = chunkIndex % chunkDefCount;
  return (uint32_t)chunkDefs[(size_t)defIdx].faceCount;
}

}  // namespace mathv_ref
}  // namespace sw
