// FlipNormals mesh op (a pure mesh→mesh CONSUMER, count = input vertex/face count, counts unchanged).
// TiXL authority: external/tixl/Operators/Lib/mesh/modify/FlipNormals.t3 (one Mesh input, one Mesh
// output, NO params) — it dispatches TWO ComputeShader children (both named by the .t3's Source
// values): mesh-FlipNormals.hlsl (per-vertex negation) AND mesh-ReverseFaceVertexIndexOrder.hlsl
// (per-face winding reversal).
//
// VERBATIM vertex math (mesh-FlipNormals.hlsl:21-27):
//   Position  = SourceVerts.Position            (copied)
//   Normal    = -SourceVerts.Normal             (★flipped)
//   Tangent   = -SourceVerts.Tangent            (★flipped)
//   Bitangent =  SourceVerts.Bitangent          (★NOT flipped — only Normal & Tangent invert)
//   TexCoord  =  SourceVerts.TexCoord           (copied)
//   Selected  =  SourceVerts.Selected           (copied)
//   ColorRGB  =  SourceVerts.ColorRGB           (copied)
// The shader writes ResultVerts field-by-field and never sets TexCoord2; its ResultVerts scratch starts
// as a copy of Source, so TexCoord2 == Source.TexCoord2. We copy the whole SwVertex then overwrite the
// flipped fields → TexCoord2 (and everything else) is byte-identical to that.
//
// VERBATIM index math (mesh-ReverseFaceVertexIndexOrder.hlsl:20):
//   ResultIndices[i.x] = SourceIndices[i.x].zyx  → each face (x,y,z) → (z,y,x), winding reversed so
//   front-face determination stays consistent with the flipped normals. CPU transcription matches the
//   mathv-verified oracle mathv_ref_reversefacevertexindexorder.h (reverseFaceVertexIndexOrderOne).
//
// FORKS (named): none. FlipNormals.cs has NO knobs (no params) → nothing deferred.
#include "runtime/graph.h"  // NodeSpec, PortSpec

#include <map>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/mesh_op_registry.h"  // MeshOp self-registration + MeshCookCtx + SwMeshView
#include "runtime/sw_mesh.h"           // SwVertex (80B) + SwTriIndex (12B)

namespace sw {
namespace {

// count: pure consumer — output topology == input[0]. Unwired → 0/0 (empty mesh, faithful no-op).
void flipNormalsCount(const std::map<std::string, float>* /*params*/, const SwMeshView* inputs,
                      int inputCount, uint32_t& vtx, uint32_t& idx) {
  if (inputCount < 1 || !inputs[0].vtx) { vtx = 0; idx = 0; return; }
  vtx = inputs[0].vtxCount;
  idx = inputs[0].faceCount;
}

void flipNormalsCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  if (c.inputMeshCount < 1 || !c.inputMeshes[0].vtx || !c.inputMeshes[0].idx) return;  // unwired → empty
  const SwMeshView& in = c.inputMeshes[0];

  const SwVertex* src = (const SwVertex*)const_cast<MTL::Buffer*>(in.vtx)->contents();
  const SwTriIndex* srcIdx = (const SwTriIndex*)const_cast<MTL::Buffer*>(in.idx)->contents();
  SwVertex* dst = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* dstIdx = (SwTriIndex*)c.output_indices->contents();

  uint32_t nv = c.vertexCount < in.vtxCount ? c.vertexCount : in.vtxCount;
  for (uint32_t i = 0; i < nv; ++i) {
    SwVertex v = src[i];                                     // copy all fields (TexCoord/TexCoord2/etc.)
    v.Normal = {-v.Normal.x, -v.Normal.y, -v.Normal.z};      // -Normal
    v.Tangent = {-v.Tangent.x, -v.Tangent.y, -v.Tangent.z};  // -Tangent
    // Bitangent NOT flipped (shader copies it through), Position/TexCoord*/Selection/ColorRgb copied.
    dst[i] = v;
  }

  // Indices: winding REVERSED per face (mesh-ReverseFaceVertexIndexOrder.hlsl:20 `.zyx`) — counts
  // unchanged, each face (X,Y,Z) → (Z,Y,X).
  uint32_t nf = c.indexCount < in.faceCount ? c.indexCount : in.faceCount;
  for (uint32_t f = 0; f < nf; ++f) {
    SwTriIndex t = srcIdx[f];
    dstIdx[f] = {t.Z, t.Y, t.X};
  }

  // Test injection (golden RED): corrupt the REAL cook, not the expectations. dst[0].Normal fires the
  // flat golden's Normal assertion; face 0 written VERBATIM (the pre-fix parity bug itself: winding
  // NOT reversed) fires the flat index assertion. (In the production pixel leg the upstream QuadMesh
  // tooth corrupts the SOURCE face to a degenerate (99,99,99) under the same shared flag, so the
  // visibility flip is masked there — the flat leg carries the -bug polarity; the production culled
  // assertion was proven RED-capable against the real pre-fix verbatim-copy bug.)
  if (meshInjectBug() && c.vertexCount > 0) {
    dst[0].Normal = {-999.0f, -999.0f, -999.0f};
  }
  if (meshInjectBug() && nf > 0) {
    dstIdx[0] = srcIdx[0];
  }
}

NodeSpec flipNormalsSpec() {
  NodeSpec s;
  s.type = "FlipNormals";
  s.title = "Flip Normals";
  // FlipNormals.cs: ONE Mesh input (89400186), ONE Mesh output (83268faa). No knobs.
  PortSpec meshIn; meshIn.id = "Mesh"; meshIn.name = "Mesh"; meshIn.dataType = "Mesh"; meshIn.isInput = true;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {meshIn, out};
  return s;
}

const MeshOp g_flipNormalsOp(flipNormalsSpec(), flipNormalsCount, flipNormalsCook);

}  // namespace
}  // namespace sw
