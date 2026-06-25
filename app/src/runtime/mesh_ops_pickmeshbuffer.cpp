// PickMeshBuffer mesh op (multi-input Mesh selector). N mesh inputs → pick the mesh at Index.
// TiXL authority: external/tixl/Operators/Lib/mesh/modify/PickMeshBuffer.cs.
// One .cpp owns the whole op (NodeSpec + MeshCountFn + MeshCookFn). ZERO edits to cook-core.
//
// VERBATIM logic (PickMeshBuffer.cs Update):
//   connections = Input.GetCollectedTypedInputs();
//   index = Index.GetValue(context);
//   Output.Value = connections[index.Mod(connections.Count)].GetValue(context);
//
// i.e., just a modular index-select from a multi-input. We implement this as:
//   effectiveIndex = (int)Index modulo inputMeshCount (non-negative mod).
//   output = copy of inputMeshes[effectiveIndex] (vertex + face buffers verbatim).
//
// FORKS (named):
//   (1) Index param: TiXL uses int; we store as Float port (default 0) and round. Range: 0..∞,
//       modular. If no inputs, output is empty.
//   (2) Since no compute shader is involved, this is a pure CPU copy — TiXL in-place shares the
//       MeshBuffers COM pointer; we do a verbatim copy of the selected mesh's vertex+face data.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <cstring>
#include <map>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/mesh_op_registry.h"  // MeshOp self-registration + MeshCookCtx + cookMeshParam/VecN
#include "runtime/sw_mesh.h"           // SwVertex (80B) + SwTriIndex (12B)

namespace sw {
namespace {

void pickMeshBufferCount(const std::map<std::string, float>* params, const SwMeshView* inputs,
                         int inputCount, uint32_t& vtx, uint32_t& idx) {
  if (inputCount <= 0) { vtx = 0; idx = 0; return; }
  int index = (int)(cookMeshParam(params, "Index", 0.0f) + 0.5f);
  // Non-negative modulo (matches TiXL Int.Mod extension).
  int n = inputCount;
  int effective = ((index % n) + n) % n;
  if (!inputs[effective].vtx) { vtx = 0; idx = 0; return; }
  vtx = inputs[effective].vtxCount;
  idx = inputs[effective].faceCount;
}

void pickMeshBufferCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  if (c.inputMeshCount <= 0) return;

  int index = (int)(cookMeshParam(c.params, "Index", 0.0f) + 0.5f);
  int n = c.inputMeshCount;
  int effective = ((index % n) + n) % n;
  const SwMeshView& picked = c.inputMeshes[effective];
  if (!picked.vtx || !picked.idx) return;

  const SwVertex* srcV = (const SwVertex*)const_cast<MTL::Buffer*>(picked.vtx)->contents();
  const SwTriIndex* srcI = (const SwTriIndex*)const_cast<MTL::Buffer*>(picked.idx)->contents();
  SwVertex* dstV = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* dstI = (SwTriIndex*)c.output_indices->contents();

  uint32_t nv = (c.vertexCount < picked.vtxCount) ? c.vertexCount : picked.vtxCount;
  uint32_t nf = (c.indexCount < picked.faceCount) ? c.indexCount : picked.faceCount;
  std::memcpy(dstV, srcV, nv * sizeof(SwVertex));
  std::memcpy(dstI, srcI, nf * sizeof(SwTriIndex));

  // Test injection (golden RED): corrupt the first output vertex position.
  if (meshInjectBug() && c.vertexCount > 0) dstV[0].Position = {-999.0f, -999.0f, -999.0f};
}

NodeSpec pickMeshBufferSpec() {
  NodeSpec s;
  s.type = "PickMeshBuffer";
  s.title = "Pick Mesh Buffer";
  // Input: MultiInput Mesh. Index: Float (0, represents int).
  PortSpec meshIn; meshIn.id = "Input"; meshIn.name = "Input"; meshIn.dataType = "Mesh";
  meshIn.isInput = true; meshIn.multiInput = true;
  PortSpec idx; idx.id = "Index"; idx.name = "Index"; idx.dataType = "Float"; idx.isInput = true;
  idx.def = 0.0f; idx.minV = 0.0f; idx.maxV = 16.0f;
  PortSpec out; out.id = "Output"; out.name = "Output"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {meshIn, idx, out};
  return s;
}

const MeshOp g_pickMeshBufferOp(pickMeshBufferSpec(), pickMeshBufferCount, pickMeshBufferCook);

}  // namespace
}  // namespace sw
