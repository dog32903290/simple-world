// runtime/point_graph_mesh_cook — cookFlatMeshNode / cookFlatMeshInto: the FLAT-path cook for the
// MESH currency (4th cook flow = TiXL MeshBuffers = vertex-buffer + index-buffer pair).
//
// Extracted VERBATIM from the cookMeshNode / cookMeshInto lambdas that lived inside PointGraph::cook
// (point_graph.cpp) — a zero-behaviour-change move that buys ratchet headroom (point_graph.cpp was at
// its line cap) AND establishes the EXTRACTION PATTERN (Cut 6 pilot) the later flat-cook flows
// (string / host-value / tex) will follow. Mirror of the resident twin resident_mesh_cook.cpp
// (cookResidentMesh), which extracted the same currency from PointGraph::cookResident.
//
// THE MECHANISM = thin-lambda → Impl-method delegation (NOT a context struct). The flat cook body is a
// web of [&]-capturing std::function lambdas sharing cook()-stack-local memos. The mesh flow is the
// IDEAL pilot because it is a CLOSED sub-graph: cookMeshNode/cookMeshInto only ever recurse into each
// OTHER (mesh→mesh); they never call back into cookNode/cookCommand. So the body moves cleanly to two
// Impl methods that take the minimal shared state by reference — the const Graph& g, the const
// EvaluationContext& ctx, and the nodeParams memo (a std::function&). cook()'s original cookMeshNode /
// cookMeshInto std::function slots stay AS thin forwarding lambdas (so cookNode/cookCommand keep their
// one-call invocation + the closure web is untouched) that just delegate the body to these methods.
//
// THE ONE DIFFERENCE from the in-lambda version (output byte-identical): the lambda read back a cooked
// source pair via PointGraph::debugCookedMesh, a method on the OWNER (PointGraph), which Impl has no
// back-pointer to. So the method INLINES the identical map reads (meshVtxBuf/meshIdxBuf/meshVtxCount/
// meshIdxCount lookups — debugCookedMesh's exact body, point_graph_debug.cpp:42). Same maps, same keys,
// same values → byte-identical gather.
//
// PLACEMENT: runtime leaf (depends only on the flat Graph + the mesh registry + PointGraph::Impl — all
//   runtime). Defined as methods on PointGraph::Impl (so they can name the private nested Impl and reach
//   ensureMesh / meshVtxBuf); cook() wraps them in forwarding lambdas.
#include "runtime/point_graph_internal.h"  // PointGraph::Impl (ensureMesh / meshVtxBuf) + the method decls

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <Metal/Metal.hpp>

#include "runtime/graph.h"             // Graph / Node / NodeSpec / PortSpec / Connection / pinId / pinNode / findSpec
#include "runtime/mesh_op_registry.h"  // MeshCookCtx / SwMeshView / MeshOpReg / findMeshOp
#include "runtime/tixl_point.h"        // EvaluationContext (MeshCookCtx::ctx)

namespace sw {

using pgdetail::flatKey;

// Cook a MESH-flow node (the 4th cook flow = TiXL MeshBuffers). A mesh GENERATOR (NGonMesh/QuadMesh)
// owns NO Mesh input — it computes its vertex+index counts from its own params. A mesh CONSUMER
// (TransformMesh in×1 / CombineMeshes MultiInput, the mesh-input seam) first GATHERS its wired Mesh
// input(s): for each Mesh input port (spec order; MultiInput expands every wire in connection order),
// recurse cookFlatMeshNode on the source then read its cooked pair (the inlined debugCookedMesh map
// reads) into a SwMeshView. The gather mirrors the cookPointListNode walk (input-dependent currency,
// borrowed single-frame). Then countFn(params, views, n) decides the OUTPUT sizes (generator ignores
// views; consumer reads them), the driver sizes the OWNED pair (ensureMesh, count-change reuse), and the
// op writes both buffers via contents(). The currency is a PAIR (SwVertex + SwTriIndex). Returns true if
// a mesh op cooked (the buffers are then in meshVtxBuf/meshIdxBuf, readable via debugCookedMesh).
//
// ★ Source meshes are cooked into meshVtxBuf[srcKey] BEFORE this node's ensureMesh(flatKey(id),...).
// ensureMesh keys by flat id, so a consumer's own pair never aliases its sources' pairs — the borrowed
// SwMeshView buffers stay valid across this node's ensureMesh (different map keys, no realloc churn).
bool PointGraph::Impl::cookFlatMeshNode(const Graph& g, const EvaluationContext& ctx,
                                        const NodeParamsFn& nodeParams, int id) {
  const Node* n = g.node(id);
  if (!n) return false;
  const MeshOpReg* reg = findMeshOp(n->type);
  if (!reg || !reg->cook || !reg->count) return false;

  // Gather upstream Mesh inputs (spec port order; MultiInput → one view per wire, connection order).
  std::vector<SwMeshView> inputMeshes;
  const NodeSpec* s = findSpec(n->type);
  if (s) {
    for (size_t i = 0; i < s->ports.size(); ++i) {
      const PortSpec& port = s->ports[i];
      if (!(port.isInput && port.dataType == "Mesh")) continue;
      const int inPin = pinId(id, (int)i);
      for (const Connection& c : g.connections) {
        if (c.toPin != inPin) continue;
        const int srcId = pinNode(c.fromPin);
        SwMeshView v;
        if (cookFlatMeshNode(g, ctx, nodeParams, srcId))  // cook the source pair into meshVtxBuf/meshIdxBuf[srcKey]
          debugCookedMeshInline(srcId, v.vtx, v.vtxCount, v.idx, v.faceCount);
        inputMeshes.push_back(v);   // an unwired/non-mesh source pushes an empty view (faithful no-op)
        if (!port.multiInput) break;  // single-input: first wire only
      }
    }
  }

  const std::map<std::string, float>* mp = nodeParams(id);
  uint32_t vtxCount = 0, idxCount = 0;
  // count FIRST: generator ignores the views; consumer reads them (TransformMesh inputs[0]; CombineMeshes Σ).
  reg->count(mp, inputMeshes.data(), (int)inputMeshes.size(), vtxCount, idxCount);

  MTL::Buffer* vb = nullptr;
  MTL::Buffer* ib = nullptr;
  ensureMesh(flatKey(id), vtxCount, idxCount, vb, ib);  // size the owned pair before cook

  MeshCookCtx mc;
  mc.dev = dev; mc.lib = lib; mc.queue = queue;
  mc.ctx = &ctx; mc.nodeId = id;
  mc.vertexCount = vtxCount; mc.indexCount = idxCount;
  mc.output_vertices = vb; mc.output_indices = ib;
  mc.inputMeshes = inputMeshes.data(); mc.inputMeshCount = (int)inputMeshes.size();
  mc.params = mp;
  reg->cook(mc);
  return true;
}

// Bridge: cook the upstream mesh node, then read its buffers back (so a Mesh-consuming command op —
// DrawMeshUnlit — can borrow them). cook() assigns this to the forward-declared cookMeshInto
// std::function so cookCommand can call it. Returns false (and leaves the out-params untouched) if the
// node is not a mesh op / produced nothing.
bool PointGraph::Impl::cookFlatMeshInto(const Graph& g, const EvaluationContext& ctx,
                                        const NodeParamsFn& nodeParams, int id, const MTL::Buffer*& vtx,
                                        uint32_t& vtxCount, const MTL::Buffer*& idx, uint32_t& faceCount) {
  if (!cookFlatMeshNode(g, ctx, nodeParams, id)) return false;  // cook the generator into meshVtxBuf/meshIdxBuf (or no-op)
  return debugCookedMeshInline(id, vtx, vtxCount, idx, faceCount);
}

// The inlined twin of PointGraph::debugCookedMesh (point_graph_debug.cpp:42): read back a node's cooked
// mesh PAIR from the Impl maps. Replicated here byte-for-byte because Impl has no PointGraph back-pointer
// to call the owner method through — same maps, same keys, same values.
bool PointGraph::Impl::debugCookedMeshInline(int nodeId, const MTL::Buffer*& vtx, uint32_t& vtxCount,
                                             const MTL::Buffer*& idx, uint32_t& idxCount) {
  const std::string key = flatKey(nodeId);
  auto vb = meshVtxBuf.find(key);
  auto ib = meshIdxBuf.find(key);
  if (vb == meshVtxBuf.end() || ib == meshIdxBuf.end() || !vb->second || !ib->second) return false;
  vtx = vb->second;
  idx = ib->second;
  vtxCount = meshVtxCount.count(key) ? meshVtxCount[key] : 0u;
  idxCount = meshIdxCount.count(key) ? meshIdxCount[key] : 0u;
  return true;
}

}  // namespace sw
