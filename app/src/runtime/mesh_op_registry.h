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

// A borrowed, single-frame view of ONE upstream mesh (its cooked SwVertex + SwTriIndex pair). The
// mesh-input seam's currency: a CONSUMING mesh op (TransformMesh/CombineMeshes) reads its inputs as
// SwMeshView, NOT by allocating. The cook DRIVER gathers each wired Mesh input port (recursing the
// upstream mesh node, then reading its buffers via debugCookedMesh) into an array of these and hands
// it in via MeshCookCtx::inputMeshes — same borrowed lifetime as a Texture2D gather (the buffers
// live in PointGraph::Impl::meshVtxBuf/meshIdxBuf for this cook; the consumer copies what it needs).
struct SwMeshView {
  const MTL::Buffer* vtx = nullptr;  // upstream SwVertex buffer (read-only this frame)
  uint32_t vtxCount = 0;             // # SwVertex in vtx
  const MTL::Buffer* idx = nullptr;  // upstream SwTriIndex buffer (read-only this frame)
  uint32_t faceCount = 0;            // # SwTriIndex (faces) in idx
};

// Everything a mesh op gets to cook one node this frame. Mirrors PointCookCtx (point_graph.h) but
// carries the TWO owned output buffers of the mesh currency. The cook DRIVER pre-sizes BOTH output
// buffers to vertexCount SwVertex + indexCount SwTriIndex (the count-change-reuse lifetime rule,
// same as a point op's `output`) and the op WRITES its result via contents() — it does NOT allocate.
//
// vertexCount / indexCount: the counts the driver sized the buffers to. A generator computes these
// from its own params (NGon: verts=Segments+1, faces=Segments; Quad: verts=cols*rows, faces=...). A
// CONSUMER (TransformMesh/CombineMeshes) computes them from inputMeshes (TransformMesh: inputs[0]'s
// counts; CombineMeshes: the sum across all inputs). The driver asks the op for the counts FIRST
// (countFn, given the gathered inputs), sizes the buffers, then runs cook — so by the time cook runs,
// output_vertices/output_indices are correctly sized.
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
  // Gathered upstream Mesh inputs (mesh-input seam). The driver fills this from each wired Mesh input
  // port (spec order; a MultiInput Mesh port — CombineMeshes — expands every wire). A generator
  // (NGonMesh/QuadMesh) owns no Mesh input port → inputMeshCount stays 0 and these are unread, so its
  // path is byte-identical. Borrowed single-frame (do NOT retain past cook).
  const SwMeshView* inputMeshes = nullptr;
  int inputMeshCount = 0;
  // RESOLVED Float params of THIS node (same value spine as PointCookCtx::params): the cook DRIVER
  // resolves every Float input port (override → binding → wire → stored → spec default) and hands
  // the result here. Ops read via cookMeshParam/cookMeshVecN and stay graph-model-agnostic.
  const std::map<std::string, float>* params = nullptr;
};

// A mesh op: compute counts (countFn) then write vertices+indices (cookFn). Two fns because the
// driver must know the sizes BEFORE it allocates the output buffers (it can't peek inside cook).
//   countFn: given the resolved params AND the gathered upstream mesh inputs, return
//            (vertexCount, indexCount) for this frame. A generator IGNORES the inputs; a consumer
//            (TransformMesh: inputs[0].vtxCount/faceCount; CombineMeshes: Σ) reads them. This is the
//            fork-mesh-1 signature widen — the input-dependent count the generator-only signature
//            couldn't express. Existing generators add two IGNORED params; their math is unchanged.
//   cookFn:  fill output_vertices/output_indices (already sized to those counts).
using MeshCountFn = void (*)(const std::map<std::string, float>* params, const SwMeshView* inputs,
                            int inputCount, uint32_t& vertexCount, uint32_t& indexCount);
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
