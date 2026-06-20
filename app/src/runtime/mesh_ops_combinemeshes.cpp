// CombineMeshes mesh op (the mesh-input seam's MultiInput proving leaf). N meshes in → ONE mesh out:
// concatenate every input's vertices, then concatenate every input's faces with each face's indices
// REBASED by the running vertex offset (so face i of mesh k points at mesh k's vertices in the merged
// buffer). TiXL authority: external/tixl/Operators/Lib/mesh/modify/CombineMeshes.cs (a .t3 compound;
// the math is the two dispatched compute shaders below).
//
// VERBATIM math:
//   mesh-CombineVertexBuffers.hlsl: ResultVertices[i + startVertexIndex] = Vertices[i]; (verbatim copy
//     — DebugValue defaults to 0 so the `Position.y += DebugValue` is a no-op). startVertexIndex = the
//     count of all PRECEDING meshes' vertices.
//   mesh-CombineIndexBuffers.hlsl: ResultIndices[i + StartIndex] = Indices[i] + StartVertex; (int3 face
//     offset by StartVertex = the count of all preceding meshes' vertices). StartIndex = preceding faces.
//   → output verticesCount = Σ input vtxCount; faceCount = Σ input faceCount.
//
// FORKS (named): DebugValue=0 (default → vertex copy is verbatim); IsEnabled=true (default; a disabled
// CombineMeshes is the bypass case, not modeled in this leaf). MultiInput order = wire-declaration
// order (TiXL Meshes.CollectedInputs), the SAME order the driver gathers SwMeshViews.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"      // EvaluationContext
#include "runtime/mesh_op_registry.h"  // MeshOp self-registration + MeshCookCtx + SwMeshView
#include "runtime/sw_mesh.h"           // SwVertex (80B) + SwTriIndex (12B)

namespace sw {
namespace {

// count: Σ over all gathered inputs (each input view is one wired Mesh; MultiInput expanded by driver).
// Empty / all-unwired → 0/0 (faithful: no input meshes → empty merged mesh).
void combineMeshesCount(const std::map<std::string, float>* /*params*/, const SwMeshView* inputs,
                        int inputCount, uint32_t& vtx, uint32_t& idx) {
  uint32_t sumV = 0, sumF = 0;
  for (int i = 0; i < inputCount; ++i) {
    if (!inputs[i].vtx || !inputs[i].idx) continue;  // an unwired view contributes nothing
    sumV += inputs[i].vtxCount;
    sumF += inputs[i].faceCount;
  }
  vtx = sumV;
  idx = sumF;
}

void combineMeshesCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  SwVertex* dst = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* dstIdx = (SwTriIndex*)c.output_indices->contents();

  uint32_t vOffset = 0;  // running vertex count of preceding meshes = startVertexIndex / StartVertex
  uint32_t fOffset = 0;  // running face count of preceding meshes   = StartIndex
  for (int k = 0; k < c.inputMeshCount; ++k) {
    const SwMeshView& in = c.inputMeshes[k];
    if (!in.vtx || !in.idx) continue;  // skip an unwired view (matches the count loop's skip)
    const SwVertex* sv = (const SwVertex*)const_cast<MTL::Buffer*>(in.vtx)->contents();
    const SwTriIndex* sf = (const SwTriIndex*)const_cast<MTL::Buffer*>(in.idx)->contents();

    // Vertices: verbatim copy at the running offset (clamp to the sized output, defensive).
    for (uint32_t i = 0; i < in.vtxCount && (vOffset + i) < c.vertexCount; ++i)
      dst[vOffset + i] = sv[i];
    // Faces: copy + REBASE each index by vOffset (the merged-buffer vertex base of THIS mesh).
    for (uint32_t f = 0; f < in.faceCount && (fOffset + f) < c.indexCount; ++f) {
      dstIdx[fOffset + f].X = sf[f].X + (int32_t)vOffset;
      dstIdx[fOffset + f].Y = sf[f].Y + (int32_t)vOffset;
      dstIdx[fOffset + f].Z = sf[f].Z + (int32_t)vOffset;
    }
    vOffset += in.vtxCount;
    fOffset += in.faceCount;
  }

  // Test injection (golden RED): corrupt the FIRST merged face's index triple in the REAL cook so the
  // golden's exact-rebase assertion fires on the actual combine path.
  if (meshInjectBug() && c.indexCount > 0) { dstIdx[0].X = 99; dstIdx[0].Y = 99; dstIdx[0].Z = 99; }
}

NodeSpec combineMeshesSpec() {
  NodeSpec s;
  s.type = "CombineMeshes";
  s.title = "Combine Meshes";
  // Meshes = MultiInput Mesh (TiXL MultiInputSlot<MeshBuffers>). IsEnabled deferred (default true).
  PortSpec meshes; meshes.id = "Meshes"; meshes.name = "Meshes"; meshes.dataType = "Mesh";
  meshes.isInput = true; meshes.multiInput = true;
  PortSpec out; out.id = "CombinedMesh"; out.name = "CombinedMesh"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {meshes, out};
  return s;
}

const MeshOp g_combineMeshesOp(combineMeshesSpec(), combineMeshesCount, combineMeshesCook);

}  // namespace
}  // namespace sw
