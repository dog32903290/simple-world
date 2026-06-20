// runtime/floatlist_op_registry — self-registration seam for FLOATLIST ops (the 5th cook flow).
//
// The FloatList channel is a HOST-side value currency — TiXL's Slot<List<float>> (a CPU list that
// rides between ops), NOT a GPU buffer. It is the value-graph parallel of "Points"/"Texture2D"/
// "String": a producer port (dataType=="FloatList" output) hands a std::vector<float> to a consumer
// port (dataType=="FloatList" input). The list lives in host memory the whole way; it never touches
// the 16-byte GPU EvaluationContext. (Slice B = ValuesToTexture uploads it to a texture; NOT here.)
//
// Pattern cloned from mesh_op_registry.h (the 4th cook flow, Cut 90) + the string-channel (Cut 87):
// adding a floatlist op = add ONE leaf .cpp ending with a file-scope `FloatListOp` registrar. The
// registrar feeds two sinks:
//   (1) floatListSpecSink()   — its NodeSpec (so it appears in the Add menu / findSpec, like any op),
//   (2) floatListCookFns()    — its FloatListCookFn (so the cook driver's cookFloatListNode runs it).
//
// Init-order safety (identical to the mesh / image-filter / value-op sinks): every registrar is a
// namespace-scope static, so all finish their dynamic-init constructors before main and before any
// LIVE sink read (node_registry's findSpec/specTypes read the sink live, never snapshot).
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
}  // namespace MTL

struct EvaluationContext;  // runtime/eval_context.h

namespace sw {

// Everything a floatlist op gets to cook one node this frame. Mirrors MeshCookCtx (mesh_op_registry.h)
// but the currency is a HOST std::vector<float>, not a GPU buffer — so there is NO pre-sizing (a
// vector self-sizes) and NO Metal allocation. The dev/lib/queue refs ride along for symmetry (Slice B
// consumers like ValuesToTexture need them; a pure producer like FloatsToList ignores them).
//
//   inputLists : the already-cooked upstream FloatList inputs (in spec port order, MultiInput-
//                expanded into wire-declaration order). A pure producer (FloatsToList) has none → the
//                vector is empty. A consumer reads inputLists->at(i) for its i-th gathered source.
//   output     : THIS node's host list. The cook driver owns it (in Impl::floatListBuf, keyed by
//                flatKey(id)); the op WRITES into *output (clear + fill) — it does not allocate it.
//   params     : RESOLVED Float params of THIS node (same value spine as MeshCookCtx::params) — the
//                cook driver resolves every Float input port and hands the result here.
struct FloatListCookCtx {
  MTL::Device* dev = nullptr;
  MTL::Library* lib = nullptr;
  MTL::CommandQueue* queue = nullptr;
  const EvaluationContext* ctx = nullptr;  // time / frameIndex / deltaTime
  int nodeId = 0;
  // Cooked upstream FloatList inputs (one entry per WIRED FloatList source, in spec port order with
  // MultiInput ports expanded into wire-declaration order). Borrowed (driver-owned); never retained.
  const std::vector<std::vector<float>>* inputLists = nullptr;
  // Driver-owned output list. The op writes via output->clear()/push_back; never allocates/frees it.
  std::vector<float>* output = nullptr;
  // RESOLVED Float params of THIS node (mirror of MeshCookCtx::params); read via floatListParam.
  const std::map<std::string, float>* params = nullptr;
};

// A floatlist op: read inputLists (+ resolved Float params) → write *output. ONE fn (unlike a mesh
// op's count+cook pair) because a host vector self-sizes — the driver never pre-allocates it.
using FloatListCookFn = void (*)(FloatListCookCtx&);

// Read a Float param from a FloatListCookCtx's RESOLVED map (mirror of cookMeshParam); `def` when the
// driver supplied no map (ops invoked outside a cook driver, e.g. a hand-built ctx in a golden).
float floatListParam(const std::map<std::string, float>* params, const char* id, float def);

// --- the two sinks every floatlist-op leaf registrar feeds ---
std::vector<NodeSpec>& floatListSpecSink();              // NodeSpecs (node_registry reads live)
std::map<std::string, FloatListCookFn>& floatListCookFns();  // type-name -> cook fn

// Lookup the cook fn for a type (nullptr if not a floatlist op). Used by the cook driver's dispatch.
const FloatListCookFn* findFloatListOp(const std::string& type);

// Test-only injection seam (goldens): when set, a floatlist op's cook corrupts its REAL output (e.g.
// drops the last element) so the golden's RED case fires on the actual cook path (NOT by flipping the
// expected value). Off in production. A leaf reads it at the end of its cook.
bool& floatListInjectBug();

// RAII registrar: declare one file-scope static of this type at the end of each floatlist-op leaf.
//   FloatListOp(spec, cookFn);  // pushes spec into floatListSpecSink() and cook into floatListCookFns()
struct FloatListOp {
  FloatListOp(NodeSpec spec, FloatListCookFn cook);
};

}  // namespace sw
