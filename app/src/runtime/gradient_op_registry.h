// runtime/gradient_op_registry — self-registration seam for GRADIENT ops (the 8th cook flow).
//
// The Gradient channel is a HOST-side value currency — TiXL's Slot<Gradient> (a CPU Gradient value
// that rides between ops), NOT a GPU buffer. It is the value-graph parallel of FloatList/String/
// PointList: a producer port (dataType=="Gradient" output) hands a SwGradient to a consumer port
// (dataType=="Gradient" input). The gradient lives in host memory the whole way; it never touches the
// 16-byte GPU EvaluationContext. (GradientsToTexture = the Gradient CONSUMER + tex-output fork; it
// uploads sampled colors to an R32G32B32A32 texture — NOT here, it is a tex op like ValuesToTexture.)
//
// Pattern cloned VERBATIM from floatlist_op_registry.h (the 5th cook flow) / pointlist_op_registry.h
// (the 7th): adding a gradient op = add ONE leaf .cpp ending with a file-scope `GradientOp` registrar.
// The registrar feeds two sinks:
//   (1) gradientSpecSink()   — its NodeSpec (so it appears in the Add menu / findSpec, like any op),
//   (2) gradientCookFns()    — its GradientCookFn (so the cook driver's cookGradientNode runs it).
//
// Init-order safety (identical to the floatlist / mesh / string sinks): every registrar is a
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

#include "runtime/graph.h"       // NodeSpec
#include "runtime/sw_gradient.h"  // SwGradient — the 8th flow's currency

namespace MTL {
class Device;
class Library;
class CommandQueue;
}  // namespace MTL

struct EvaluationContext;  // runtime/eval_context.h

namespace sw {

// Everything a gradient op gets to cook one node this frame. Mirrors FloatListCookCtx (floatlist_op_
// registry.h) but the currency is a HOST SwGradient, not a std::vector<float> — so there is NO pre-
// sizing and NO Metal allocation. The dev/lib/queue refs ride along for symmetry (a future consumer
// might need them; a pure producer like DefineGradient ignores them).
//
//   inputGradients : the already-cooked upstream Gradient inputs (in spec port order, MultiInput-
//                    expanded into wire-declaration order). A pure producer (DefineGradient) has none →
//                    the vector is empty. A consumer reads inputGradients->at(i) for its i-th source.
//   output         : THIS node's host gradient. The cook driver owns it (in Impl::gradientBuf, keyed
//                    by flatKey(id)); the op WRITES into *output — it does not allocate it.
//   params         : RESOLVED Float params of THIS node (same value spine as FloatListCookCtx::params)
//                    — the cook driver resolves every Float input port and hands the result here.
struct GradientCookCtx {
  MTL::Device* dev = nullptr;
  MTL::Library* lib = nullptr;
  MTL::CommandQueue* queue = nullptr;
  const EvaluationContext* ctx = nullptr;  // time / frameIndex / deltaTime
  int nodeId = 0;
  // Cooked upstream Gradient inputs (one entry per WIRED Gradient source, in spec port order with
  // MultiInput ports expanded into wire-declaration order). Borrowed (driver-owned); never retained.
  const std::vector<SwGradient>* inputGradients = nullptr;
  // Driver-owned output gradient. The op writes via output->steps; never allocates/frees it.
  SwGradient* output = nullptr;
  // RESOLVED Float params of THIS node (mirror of FloatListCookCtx::params); read via gradientParam.
  const std::map<std::string, float>* params = nullptr;
};

// A gradient op: read inputGradients (+ resolved Float params) → write *output. ONE fn (like a
// floatlist op) because a host SwGradient self-sizes — the driver never pre-allocates it.
using GradientCookFn = void (*)(GradientCookCtx&);

// Read a Float param from a GradientCookCtx's RESOLVED map (mirror of floatListParam); `def` when the
// driver supplied no map (ops invoked outside a cook driver, e.g. a hand-built ctx in a golden).
float gradientParam(const std::map<std::string, float>* params, const char* id, float def);

// --- the two sinks every gradient-op leaf registrar feeds ---
std::vector<NodeSpec>& gradientSpecSink();                  // NodeSpecs (node_registry reads live)
std::map<std::string, GradientCookFn>& gradientCookFns();  // type-name -> cook fn

// Lookup the cook fn for a type (nullptr if not a gradient op). Used by the cook driver's dispatch.
const GradientCookFn* findGradientOp(const std::string& type);

// Test-only injection seam (goldens): when set, a gradient op's cook corrupts its REAL output (e.g.
// drops the last step) so the golden's RED case fires on the actual cook path (NOT by flipping the
// expected value). Off in production. A leaf reads it at the end of its cook.
bool& gradientInjectBug();

// RAII registrar: declare one file-scope static of this type at the end of each gradient-op leaf.
//   GradientOp(spec, cookFn);  // pushes spec into gradientSpecSink() and cook into gradientCookFns()
struct GradientOp {
  GradientOp(NodeSpec spec, GradientCookFn cook);
};

}  // namespace sw
