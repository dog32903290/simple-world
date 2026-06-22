// runtime/math_op_registry — self-registration seam for MATH / VALUE NodeSpecs.
//
// ARCHITECTURE rule 7 (data-driven) pushed down to the op layer, mirror of the image-filter /
// value-op / string-op / point-modify self-registration sinks: adding a math op = drop ONE leaf .cpp
// (node_registry_math_<subfamily>.cpp) that ends with a file-scope `MathOp` registrar. No shared
// manifest is edited — the old撞車點 (the single 980-line node_registry_math.cpp specs table) is gone.
//
// How it works (identical lifetime guarantee to pointModifySpecSink / imageFilterSpecSink):
//   - Each leaf defines its registrar(s) at namespace scope. The registrar's constructor runs during
//     pre-main dynamic initialization and pushes its NodeSpec into mathSpecSink().
//   - The consumer (node_registry.cpp's findSpec/specTypes) reads the sink LIVE — NEVER from the
//     cached registry() snapshot. This matters: a pre-main static (doc::g_lib, document.cpp) calls
//     findSpec during its own initialization; if math specs were baked into the cached registry()
//     vector, that snapshot could run before the leaf registrars finished and MISS them. Reading the
//     sink live (same as image-filter / point-modify) is init-order safe: every registrar is a
//     namespace-scope static, so all finish their dynamic-init constructors before main and before
//     any live sink read after main starts.
//
// WHY a NodeSpec sink (no cook-fn sink): a math spec is a NodeSpec CARRIER. Its NodeSpec::evaluate
// is a pure value-node fn pointer (evalAdd, evalSine, …) for the stateless ops, or nullptr for the
// stateful ops (Damp/Spring/Anim*/Set*Var/…) whose per-frame cook is dispatched BY TYPE NAME inside
// frame_cook's stateful-value seam, NOT through this registry. So this seam carries the port shape +
// the (possibly-null) evaluate fn + the Add-menu entry — the exact role the old mathSpecs() table did.
//
// FORK / risk (named, same as the sibling registries): intra-family ORDER in the sink follows
// cross-TU dynamic-init order (unspecified). Cosmetic only (Add-menu position) — findSpec is keyed by
// type name and .swproj wires are keyed by port id, neither depends on spec position.
#pragma once
#include <vector>

#include "runtime/graph.h"  // NodeSpec

namespace sw {

// Meyers singleton — the one sink every math leaf registrar feeds (node_registry reads live).
std::vector<NodeSpec>& mathSpecSink();

// RAII registrar: declare one file-scope static of this type per spec at the end of each leaf.
//   MathOp{ {"Add", ... , evalAdd} };  // pushes the NodeSpec into the sink
struct MathOp {
  explicit MathOp(NodeSpec spec);
};

}  // namespace sw
