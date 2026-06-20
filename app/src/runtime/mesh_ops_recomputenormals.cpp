// RecomputeNormals mesh op (a pure mesh→mesh CONSUMER, count = input vertex/face count, topology
// unchanged — it only rewrites Normal/Tangent/Bitangent/Selected, never adds/removes verts or faces).
// TiXL authority: external/tixl/Operators/Lib/mesh/modify/RecomputeNormals.cs (one Mesh input
// `InputMesh` + one bool `RecomputeIndices`) + compute shader mesh-RecomputeNormals.hlsl (the math).
//
// VERBATIM math (mesh-RecomputeNormals.hlsl `computeNormal` entry, the only geometry pass):
//   For each vertex gi, over EVERY face that references gi (built by registerFaceVertices), accumulate
//   the face's UNNORMALIZED area-vector twice (the .hlsl does `normalSum += N` then `normalSum += N*area`
//   inside the loop → per face N*(1+area)):
//       N    = cross(P2 - P1, P3 - P1)            (P1/P2/P3 = the face's 3 vertex positions, in order)
//       area = 0.5 * length(N)
//       normalSum += N + N * area
//   Then:
//       newNormal    = normalize(normalSum)
//       newTangent   = cross(Bitangent_src, newNormal)     (reuses the SOURCE bitangent)
//       newBitangent = cross(newNormal, newTangent)
//       Selected     = faceCount   (# of faces touching gi; ★the shader literally stores this)
//   A vertex touched by NO face keeps its source values verbatim (shader early-returns before writing).
//   Position / TexCoord / TexCoord2 / ColorRGB are copied (ResultVertices[gi] = SourceVertices[gi]
//   before the overwrite). MaxNeighbourFaceCount=15 cap (shader's per-vertex face-list ceiling) honored.
//
// FORKS (named):
//   • RecomputeIndices (RecomputeNormals.cs bool input): in TiXL it only feeds an `Any`/IsBufferDirty
//     re-cook TRIGGER (RecomputeNormals.t3 conn b55aeb9b/fd3f8225 → `a1593636` Any), NOT the geometry
//     math — `computeNormal` never reads it. We expose the port (faithful .cs surface) but it is an
//     inert no-op on the CPU result (named fork: GPU-caching knob with no output effect).
//   • The cbuffer `float Amount` in the .hlsl is DEAD (declared, never used in any entry) → not a port.
//   • Tangent basis on a degenerate (zero-area) star: normalize(0)→shader returns NaN; we guard
//     len<eps → keep source frame (named fork: avoids NaN; TiXL would emit NaN on that vertex, which
//     only happens for fully-degenerate input the goldens never feed).
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <cmath>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/mesh_op_registry.h"  // MeshOp self-registration + MeshCookCtx + SwMeshView
#include "runtime/sw_mesh.h"           // SwVertex (80B) + SwTriIndex (12B)

namespace sw {
namespace {

constexpr int kMaxNeighbourFaceCount = 15;  // mesh-RecomputeNormals.hlsl static const

struct V3 { float x, y, z; };
inline V3 sub(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 crossV(const V3& a, const V3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float lengthV(const V3& a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
inline V3 normalizeV(const V3& a) {
  float l = lengthV(a);
  return l > 1e-12f ? V3{a.x / l, a.y / l, a.z / l} : V3{0, 0, 0};
}

// count: pure consumer — output topology == input[0]. Unwired → 0/0 (empty mesh, faithful no-op).
void recomputeNormalsCount(const std::map<std::string, float>* /*params*/, const SwMeshView* inputs,
                           int inputCount, uint32_t& vtx, uint32_t& idx) {
  if (inputCount < 1 || !inputs[0].vtx) { vtx = 0; idx = 0; return; }
  vtx = inputs[0].vtxCount;
  idx = inputs[0].faceCount;
}

void recomputeNormalsCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  if (c.inputMeshCount < 1 || !c.inputMeshes[0].vtx || !c.inputMeshes[0].idx) return;  // unwired → empty
  const SwMeshView& in = c.inputMeshes[0];

  const SwVertex* src = (const SwVertex*)const_cast<MTL::Buffer*>(in.vtx)->contents();
  const SwTriIndex* srcIdx = (const SwTriIndex*)const_cast<MTL::Buffer*>(in.idx)->contents();
  SwVertex* dst = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* dstIdx = (SwTriIndex*)c.output_indices->contents();

  uint32_t nv = c.vertexCount < in.vtxCount ? c.vertexCount : in.vtxCount;
  uint32_t nf = c.indexCount < in.faceCount ? c.indexCount : in.faceCount;

  // Build the vertex→faces adjacency exactly as the shader's `clear` + `registerFaceVertices` passes do:
  // for each face, push its index onto each of its 3 vertices' lists, capped at MaxNeighbourFaceCount.
  std::vector<std::vector<int>> vertFaces(nv);
  std::vector<int> faceCountPerVert(nv, 0);  // raw count (the value the shader stores in .Selected)
  for (uint32_t f = 0; f < nf; ++f) {
    const SwTriIndex& tri = srcIdx[f];
    int vi[3] = {tri.X, tri.Y, tri.Z};
    for (int side = 0; side < 3; ++side) {
      int vIndex = vi[side];
      if (vIndex < 0 || (uint32_t)vIndex >= nv) continue;
      int org = faceCountPerVert[vIndex]++;        // matches InterlockedAdd(orgValue then store)
      if (org >= kMaxNeighbourFaceCount) continue;  // shader stops pushing past the cap (count still grows)
      vertFaces[vIndex].push_back((int)f);
    }
  }

  for (uint32_t gi = 0; gi < nv; ++gi) {
    dst[gi] = src[gi];  // copy first (ResultVertices[gi] = SourceVertices[gi]); overwrite below if any face

    int faceCount = faceCountPerVert[gi];
    if (faceCount == 0) continue;  // shader early-returns → vertex keeps source frame

    V3 normalSum{0, 0, 0};
    int used = 0;
    // Iterate the registered neighbour faces (≤ MaxNeighbourFaceCount), shader loop condition
    // `face < faceCount && face <= MaxNeighbourFaceCount`.
    const std::vector<int>& faces = vertFaces[gi];
    for (size_t k = 0; k < faces.size(); ++k) {
      const SwTriIndex& tri = srcIdx[faces[k]];
      const SwVertex& a = src[tri.X];
      const SwVertex& b = src[tri.Y];
      const SwVertex& d = src[tri.Z];
      V3 P1{a.Position.x, a.Position.y, a.Position.z};
      V3 P2{b.Position.x, b.Position.y, b.Position.z};
      V3 P3{d.Position.x, d.Position.y, d.Position.z};
      V3 N = crossV(sub(P2, P1), sub(P3, P1));
      float area = 0.5f * lengthV(N);
      normalSum = {normalSum.x + N.x, normalSum.y + N.y, normalSum.z + N.z};               // += N
      normalSum = {normalSum.x + N.x * area, normalSum.y + N.y * area, normalSum.z + N.z * area};  // += N*area
      ++used;
    }
    if (used == 0) continue;  // shader: usedNeighbourghFaceCount==0 → return (no overwrite)

    V3 bitangent{src[gi].Bitangent.x, src[gi].Bitangent.y, src[gi].Bitangent.z};
    V3 newNormal = normalizeV(normalSum);
    if (lengthV(newNormal) < 1e-9f) continue;  // degenerate star guard (named fork — keep source frame)
    V3 newTangent = crossV(bitangent, newNormal);
    V3 newBitangent = crossV(newNormal, newTangent);

    dst[gi].Selection = (float)faceCount;  // ★shader stores faceCount into .Selected
    dst[gi].Normal = {newNormal.x, newNormal.y, newNormal.z};
    dst[gi].Tangent = {newTangent.x, newTangent.y, newTangent.z};
    dst[gi].Bitangent = {newBitangent.x, newBitangent.y, newBitangent.z};
  }

  // Indices: topology unchanged → copy verbatim.
  for (uint32_t f = 0; f < nf; ++f) dstIdx[f] = srcIdx[f];

  // Test injection (golden RED): corrupt the FIRST output vertex in the REAL cook. We corrupt BOTH the
  // op's primary output (Normal — the flat golden's load-bearing assertion) AND its Position (so the
  // production resident pixel golden bites too: DrawMeshUnlit is unlit and ignores Normal, so only a
  // position move shifts the on-screen quad). Teeth fire on the actual cook path.
  if (meshInjectBug() && c.vertexCount > 0) {
    dst[0].Normal = {-999.0f, -999.0f, -999.0f};
    dst[0].Position = {-999.0f, -999.0f, -999.0f};
  }
}

NodeSpec recomputeNormalsSpec() {
  NodeSpec s;
  s.type = "RecomputeNormals";
  s.title = "Recompute Normals";
  // RecomputeNormals.cs: Mesh input `InputMesh` (b55aeb9b) + bool `RecomputeIndices` (fd3f8225, inert
  // re-cook trigger here) + Mesh output `Result` (69a94ae6).
  PortSpec meshIn; meshIn.id = "Mesh"; meshIn.name = "InputMesh"; meshIn.dataType = "Mesh"; meshIn.isInput = true;
  PortSpec recIdx; recIdx.id = "RecomputeIndices"; recIdx.name = "RecomputeIndices"; recIdx.dataType = "Float";
  recIdx.isInput = true; recIdx.def = 0.0f; recIdx.minV = 0.0f; recIdx.maxV = 1.0f; recIdx.widget = Widget::Bool;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {meshIn, recIdx, out};
  return s;
}

const MeshOp g_recomputeNormalsOp(recomputeNormalsSpec(), recomputeNormalsCount, recomputeNormalsCook);

}  // namespace
}  // namespace sw
