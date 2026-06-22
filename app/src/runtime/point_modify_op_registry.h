// runtime/point_modify_op_registry — self-registration seam for point MODIFIER NodeSpecs.
//
// ARCHITECTURE rule 7 (data-driven) pushed down to the op layer, mirror of the image-filter /
// value-op / string-op self-registration sinks: adding a point-modify op = drop ONE leaf .cpp
// (node_registry_point_modify_<subfamily>.cpp, or a dedicated per-op leaf) that ends with a
// file-scope `PointModifyOp` registrar. No shared manifest is edited — the old撞車點 (the single
// 852-line node_registry_point_modify.cpp specs table) is gone.
//
// How it works (identical lifetime guarantee to imageFilterSpecSink):
//   - Each leaf defines its registrar(s) at namespace scope. The registrar's constructor runs during
//     pre-main dynamic initialization and pushes its NodeSpec into pointModifySpecSink().
//   - The consumer (node_registry.cpp's findSpec/specTypes) reads the sink LIVE — NEVER from the
//     cached registry() snapshot. This matters: a pre-main static (doc::g_lib, document.cpp) calls
//     findSpec during its own initialization; if point-modify specs were baked into the cached
//     registry() vector, that snapshot could run before the leaf registrars finished and MISS them.
//     Reading the sink live (same as image-filter) is init-order safe: every registrar is a
//     namespace-scope static, so all finish their dynamic-init constructors before main and before
//     any live sink read after main starts.
//
// WHY ONLY a NodeSpec sink (no cook-fn sink, unlike StringOp/ImageFilterOp): a point-modify spec is a
// PURE NodeSpec CARRIER — its NodeSpec::cook is nullptr and its evaluate is nullptr. The actual cook
// is dispatched BY TYPE NAME inside the point cook driver (point_graph.cpp) + the per-op point_ops_*
// leaves, NOT through this registry. So this seam carries only the port shape + Add-menu entry, the
// exact role the old pointModifySpecs() table played.
//
// FORK / risk (named, same as the sibling registries): intra-family ORDER in the sink follows
// cross-TU dynamic-init order (unspecified). Cosmetic only (Add-menu position) — findSpec is keyed by
// type name and .swproj wires are keyed by port id, neither depends on spec position.
#pragma once
#include <vector>

#include "runtime/graph.h"  // NodeSpec

namespace sw {

// Meyers singleton — the one sink every point-modify leaf registrar feeds (node_registry reads live).
std::vector<NodeSpec>& pointModifySpecSink();

// RAII registrar: declare one file-scope static of this type per spec at the end of each leaf.
//   PointModifyOp{ {"TransformPoints", ... , nullptr} };  // pushes the NodeSpec into the sink
struct PointModifyOp {
  explicit PointModifyOp(NodeSpec spec);
};

}  // namespace sw
