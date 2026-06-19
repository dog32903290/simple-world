// runtime/mesh_op_registry — self-registration seam for MESH ops (the 4th cook flow).
//
// ARCHITECTURE rule 7 (data-driven), same shape as image_filter_op_registry / value_op_registry /
// field_node_registry: adding a mesh op = add ONE leaf .cpp (mesh_ops_<name>.cpp) that ends with a
// file-scope `MeshOp` registrar. No shared list file is edited.
//
// A mesh op is the 4th cook flow's producer. Unlike a point op (single SwPoint output buffer) or a
// texture op (single texture), a mesh op produces a PAIR of buffers that travel together =
// TiXL MeshBuffers (VertexBuffer + IndicesBuffer). So the registrar feeds two sinks:
//   (1) meshSpecSink()   — its NodeSpec (so it appears in the Add menu / findSpec, like any op),
//   (2) meshCookFns()    — its MeshCookFn (so the cook driver's cookMeshNode branch can run it).
//
// Init-order safety (identical to the image-filter / value-op / field sinks): every registrar is a
// namespace-scope static, so all of them finish their dynamic-init constructors before main runs and
// before any LIVE sink read (node_registry's findSpec/specTypes read the sink live, never snapshot).
//
// FORK / risk (named, same as the sibling registries): intra-family ORDER in the sink follows
// cross-TU dynamic-init order (unspecified). Cosmetic only (Add-menu position); findSpec is keyed by
// type name, the cook by type name — neither depends on spec position.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "runtime/graph.h"  // NodeSpec

namespace MTL {
class Device;
class Library;
class CommandQueue;
class Buffer;
}  // namespace MTL

struct EvaluationContext;  // runtime/eval_context.h

namespace sw {

// Everything a mesh op gets to cook one node this frame. Mirrors PointCookCtx (point_graph.h) but
// carries the TWO owned output buffers of the mesh currency. The cook DRIVER pre-sizes BOTH output
// buffers to vertexCount SwVertex + indexCount SwTriIndex (the count-change-reuse lifetime rule,
// same as a point op's `output`) and the op WRITES its result via contents() — it does NOT allocate.
//
// vertexCount / indexCount: the counts the driver sized the buffers to. A generator computes these
// from its own params (NGon: verts=Segments+1, faces=Segments; Quad: verts=cols*rows, faces=...).
// The driver asks the op for the counts FIRST (countFn), sizes the buffers, then runs cook — so by
// the time cook runs, output_vertices/output_indices are correctly sized.
struct MeshCookCtx {
  MTL::Device* dev = nullptr;
  MTL::Library* lib = nullptr;
  MTL::CommandQueue* queue = nullptr;
  const EvaluationContext* ctx = nullptr;  // time / frameIndex / deltaTime
  int nodeId = 0;
  uint32_t vertexCount = 0;  // # SwVertex the output_vertices buffer holds
  uint32_t indexCount = 0;   // # SwTriIndex (faces) the output_indices buffer holds
  MTL::Buffer* output_vertices = nullptr;  // driver-owned, pre-sized to vertexCount SwVertex; cook writes here
  MTL::Buffer* output_indices = nullptr;   // driver-owned, pre-sized to indexCount SwTriIndex; cook writes here
  // RESOLVED Float params of THIS node (same value spine as PointCookCtx::params): the cook DRIVER
  // resolves every Float input port (override → binding → wire → stored → spec default) and hands
  // the result here. Ops read via cookMeshParam/cookMeshVecN and stay graph-model-agnostic.
  const std::map<std::string, float>* params = nullptr;
};

// A mesh op: compute counts (countFn) then write vertices+indices (cookFn). Two fns because the
// driver must know the sizes BEFORE it allocates the output buffers (it can't peek inside cook).
//   countFn: given the resolved params, return (vertexCount, indexCount) for this frame.
//   cookFn:  fill output_vertices/output_indices (already sized to those counts).
using MeshCountFn = void (*)(const std::map<std::string, float>* params, uint32_t& vertexCount,
                            uint32_t& indexCount);
using MeshCookFn = void (*)(MeshCookCtx&);

// Read a Float param from a MeshCookCtx's RESOLVED map (mirror of cookParam); `def` when the driver
// supplied no map (ops invoked outside a cook driver, e.g. a hand-built ctx in a golden).
float cookMeshParam(const std::map<std::string, float>* params, const char* id, float def);
// Vector params (mirror of cookVecN / readVecN: components "<base>.x"/".y"/".z"). Missing -> fallback[i].
void cookMeshVecN(const std::map<std::string, float>* params, const char* base, const float* fallback,
                 int n, float* out);

// --- the two sinks every mesh-op leaf registrar feeds ---
std::vector<NodeSpec>& meshSpecSink();  // NodeSpecs (node_registry reads live)
struct MeshOpReg {
  MeshCountFn count = nullptr;
  MeshCookFn cook = nullptr;
};
std::map<std::string, MeshOpReg>& meshCookFns();  // type-name -> {count, cook}

// Lookup the cook reg for a type (nullptr if not a mesh op). Used by the cook driver's dispatch.
const MeshOpReg* findMeshOp(const std::string& type);

// Test-only injection seam (goldens): when set, a mesh op's cook corrupts ONE value in its REAL
// output (a vertex position / index triple) so the golden's RED case fires on the actual cook path
// (NOT by flipping the expected value). Off in production. A leaf reads it at the end of its cook.
bool& meshInjectBug();

// RAII registrar: declare one file-scope static of this type at the end of each mesh-op leaf.
//   MeshOp(spec, countFn, cookFn);  // pushes spec into meshSpecSink() and {count,cook} into meshCookFns()
struct MeshOp {
  MeshOp(NodeSpec spec, MeshCountFn count, MeshCookFn cook);
};

}  // namespace sw
