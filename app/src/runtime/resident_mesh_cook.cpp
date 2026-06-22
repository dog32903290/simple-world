// runtime/resident_mesh_cook — cookResidentMesh: the PRODUCTION (resident-path) cook for the MESH
// currency (the 4th cook flow = TiXL MeshBuffers = vertex-buffer + index-buffer pair).
//
// Extracted VERBATIM from the cookResidentMesh lambda that lived inside PointGraph::cookResident
// (point_graph_resident.cpp) — a zero-behaviour-change move that buys ratchet headroom (the monolith
// was at its line cap) AND co-locates the mesh gather with its new consumer (the mesh-into-points seam:
// MeshVerticesToPoints reads a cooked Mesh on the Points cook flow). Precedent: resident_string_cook /
// resident_colorlist_cook / resident_stringlist_cook already exist as extracted resident cooks.
//
// THE ONE DIFFERENCE from the in-lambda version (output byte-identical): the lambda read its node's
// resolved Float params through the cookResident-local `nodeParams` MEMO (paramsMemo). A free function
// has no memo, so it resolves params inline via resolveResidentFloatInputs(rg, *n, rc) — the SAME pure,
// deterministic resolver the memo wrapped (resolveResidentFloatInputs IS what nodeParams stores). Same
// inputs → same map → same cook. This matches the sibling resident cooks, which all resolve params
// inline per call (resident_stringlist_cook.cpp:99). The only effect is slightly less caching, not a
// different value.
//
// Cooks ONE mesh node (generator OR consumer) into its PER-PATH owned pair (Impl::meshVtxBuf/meshIdxBuf
// [path]) and returns a borrowed SwMeshView. A CONSUMER (TransformMesh/CombineMeshes) gathers its Mesh
// input(s) THROUGH the resident graph (Connection drivers; MultiInput → primary + extraConns, wire-
// declaration order) by RECURSING into this same fn. Mirror of the flat cookMeshNode (point_graph.cpp).
//
// PLACEMENT: runtime leaf (depends only on the resident graph + the mesh registry + PointGraph::Impl —
//   all runtime). Defined as a method on PointGraph::Impl (so it can name the private nested Impl and
//   reach ensureMesh/meshVtxBuf); cookResident wraps it in a forwarding lambda.
#include "runtime/point_graph_internal.h"  // PointGraph::Impl (ensureMesh / meshVtxBuf) + the method decl

#include <map>
#include <string>
#include <vector>

#include <Metal/Metal.hpp>

#include "runtime/graph.h"               // NodeSpec / PortSpec / findSpec
#include "runtime/mesh_op_registry.h"    // MeshCookCtx / SwMeshView / MeshOpReg / findMeshOp
#include "runtime/resident_eval_graph.h" // ResidentEvalGraph / drivers / resolveResidentFloatInputs
#include "runtime/tixl_point.h"          // EvaluationContext (MeshCookCtx::ctx)

namespace sw {

SwMeshView PointGraph::Impl::cookResidentMesh(const ResidentEvalGraph& rg, const std::string& path,
                                              const ResidentEvalCtx& rc, const EvaluationContext& ctx,
                                              int depth) {
  SwMeshView outView;
  if (depth > 64) return outView;  // same cycle guard as cookResident's kCookDepthCap (safe empty)
  const ResidentNode* n = rg.node(path);
  if (!n) return outView;
  const NodeSpec* s = findSpec(n->opType);
  if (!s) return outView;
  const MeshOpReg* reg = findMeshOp(n->opType);
  if (!reg || !reg->cook || !reg->count) return outView;

  // Gather upstream Mesh inputs through the resident graph (Connection drivers; MultiInput → primary +
  // extraConns, wire-declaration order). The recursion fills meshVtxBuf[srcPath] for each source.
  std::vector<SwMeshView> inputMeshes;
  for (const PortSpec& port : s->ports) {
    if (!(port.isInput && port.dataType == "Mesh")) continue;
    const ResidentInput* ri = n->input(port.id);
    if (ri && ri->driver == ResidentInput::Driver::Connection) {
      inputMeshes.push_back(cookResidentMesh(rg, ri->srcNodePath, rc, ctx, depth + 1));
      if (port.multiInput)
        for (const auto& ec : ri->extraConns)
          inputMeshes.push_back(cookResidentMesh(rg, ec.first, rc, ctx, depth + 1));
    }
    // (An unwired / Constant Mesh input contributes NO entry → empty → faithful to the flat gather.)
  }

  // Resolve THIS node's Float params inline (the memo-free twin of cookResident's nodeParams; same
  // pure resolver, byte-identical map). Held local so mc.params stays valid through reg->cook.
  std::map<std::string, float> params = resolveResidentFloatInputs(rg, *n, rc);
  uint32_t vtxCount = 0, idxCount = 0;
  reg->count(&params, inputMeshes.data(), (int)inputMeshes.size(), vtxCount, idxCount);  // counts FIRST

  MTL::Buffer* vb = nullptr;
  MTL::Buffer* ib = nullptr;
  ensureMesh(path, vtxCount, idxCount, vb, ib);  // per-path owned pair (string key, no flat collision)

  MeshCookCtx mc;
  mc.dev = dev; mc.lib = lib; mc.queue = queue;
  mc.ctx = &ctx; mc.nodeId = 0;
  mc.vertexCount = vtxCount; mc.indexCount = idxCount;
  mc.output_vertices = vb; mc.output_indices = ib;
  mc.inputMeshes = inputMeshes.data(); mc.inputMeshCount = (int)inputMeshes.size();
  mc.params = &params;
  reg->cook(mc);

  outView.vtx = vb; outView.vtxCount = vtxCount;
  outView.idx = ib; outView.faceCount = idxCount;
  return outView;
}

}  // namespace sw
