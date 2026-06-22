// runtime/stringlist_op_registry — self-registration seam for STRINGLIST ops (the host List<string> cook
// flow = TiXL Slot<List<string>>). Sub-seam A.
//
// The StringList channel is a HOST-side value currency — TiXL's Slot<List<string>> (a CPU list of strings
// that rides between ops), NOT a GPU buffer. It is the string twin of the ColorList rail
// (colorlist_op_registry.h) with the element type narrowed simd::float4 -> std::string: a producer port
// (dataType=="StringList" output) hands a std::vector<std::string> to a consumer port
// (dataType=="StringList" input). The list lives in host memory the whole way; it never touches the
// 16-byte GPU EvaluationContext.
//
// VERBATIM-shaped clone of colorlist_op_registry.h, SIMPLER on two axes: StringList ops are STATELESS
// (no KeepColors-style cross-frame accumulator → no `state`), and there is no Points-bag / 4-parallel-
// scalar input shape (a StringList is gathered whole, or built from a String + Float params like
// SplitString). Adding a stringlist op = add ONE leaf .cpp ending with a file-scope `StringListOp`
// registrar that feeds the two sinks below (spec → Add menu / findSpec; cook → the cook driver).
//
// Init-order safety / intra-family ORDER fork: identical to the sibling registries (every registrar is a
// namespace-scope static; ORDER in the sink follows cross-TU dynamic-init order, cosmetic only).
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

// Everything a stringlist op gets to cook one node this frame. Mirrors ColorListCookCtx but the currency
// is a HOST std::vector<std::string>, not std::vector<simd::float4>. NO pre-sizing (a vector self-sizes)
// and NO Metal allocation. dev/lib/queue ride along for symmetry; a pure host producer (SplitString)
// ignores them.
//
//   inputStrings   : already-cooked upstream STRING inputs (spec port order, MultiInput-expanded into
//                    wire-declaration order; an unwired single String port → its strDef const). SplitString
//                    reads inputStrings[0] (its String input). Same shape/semantics as StringCookCtx.
//   inputLists     : already-cooked upstream STRINGLIST inputs (spec port order, MultiInput-expanded). A
//                    pure producer (SplitString) has none → empty. A future StringList combiner reads them.
//   output         : THIS node's host string list. Driver-owned (Impl::stringListBuf, keyed by flatKey(id)
//                    / resident path); the op WRITES into *output (clear + fill) — never allocates it.
//   params         : RESOLVED Float params of THIS node (same value spine as the sibling ctxs). SplitString
//                    reads none today (its Split char is a String input, not a Float param) — empty map ok.
struct StringListCookCtx {
  MTL::Device* dev = nullptr;
  MTL::Library* lib = nullptr;
  MTL::CommandQueue* queue = nullptr;
  const EvaluationContext* ctx = nullptr;  // time / frameIndex / deltaTime
  int nodeId = 0;
  const std::vector<std::string>* inputStrings = nullptr;             // upstream String inputs (SplitString.String)
  const std::vector<std::vector<std::string>>* inputLists = nullptr;  // upstream StringList inputs (future combiner)
  std::vector<std::string>* output = nullptr;                         // driver-owned host string list
  const std::map<std::string, float>* params = nullptr;              // resolved Float params of THIS node
};

// A stringlist op: read inputs → write *output (clear + fill). ONE fn (a host vector self-sizes — the
// driver never pre-allocates it). Mirror of ColorListCookFn over std::string.
using StringListCookFn = void (*)(StringListCookCtx&);

// Read a Float param from a StringListCookCtx's RESOLVED map (mirror of colorListParam); `def` when the
// driver supplied no map (ops invoked outside a cook driver, e.g. a hand-built ctx in a golden).
float stringListParam(const std::map<std::string, float>* params, const char* id, float def);

// --- the two sinks every stringlist-op leaf registrar feeds ---
std::vector<NodeSpec>& stringListSpecSink();                     // NodeSpecs (node_registry reads live)
std::map<std::string, StringListCookFn>& stringListCookFns();  // type-name -> cook fn

// Lookup the cook fn for a type (nullptr if not a stringlist op). Used by the cook driver's dispatch.
const StringListCookFn* findStringListOp(const std::string& type);

// Test-only injection seam (goldens): when set, a stringlist op's cook corrupts its REAL output (e.g.
// drops the last element) so the golden's RED case fires on the actual cook path (NOT by flipping the
// expected value). Off in production. A leaf reads it at the end of its cook.
bool& stringListInjectBug();

// RAII registrar: declare one file-scope static of this type at the end of each stringlist-op leaf.
//   StringListOp(spec, cookFn);  // pushes spec into stringListSpecSink() and cook into stringListCookFns()
struct StringListOp {
  StringListOp(NodeSpec spec, StringListCookFn cook);
};

}  // namespace sw
