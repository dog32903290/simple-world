// FlipNormals mesh op (a pure mesh→mesh CONSUMER, count = input vertex/face count, topology unchanged).
// TiXL authority: external/tixl/Operators/Lib/mesh/modify/FlipNormals.cs (one Mesh input, one Mesh
// output, NO params) + its compute shader mesh-FlipNormals.hlsl (the verbatim per-vertex math).
//
// VERBATIM math (mesh-FlipNormals.hlsl:21-27):
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

  // Indices: topology unchanged → copy verbatim.
  uint32_t nf = c.indexCount < in.faceCount ? c.indexCount : in.faceCount;
  for (uint32_t f = 0; f < nf; ++f) dstIdx[f] = srcIdx[f];

  // Test injection (golden RED): corrupt the FIRST output vertex in the REAL cook. We corrupt BOTH the
  // op's primary output (Normal — the flat golden's load-bearing assertion) AND its Position (so the
  // production resident pixel golden bites too: DrawMeshUnlit is unlit and ignores Normal, so only a
  // position move shifts the on-screen quad). Teeth fire on the actual cook path, not by inverting the
  // expected value.
  if (meshInjectBug() && c.vertexCount > 0) {
    dst[0].Normal = {-999.0f, -999.0f, -999.0f};
    dst[0].Position = {-999.0f, -999.0f, -999.0f};
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
