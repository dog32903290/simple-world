// runtime/field_node_registry — self-registration seam for FIELD (SDF shader-graph) ops.
//
// ARCHITECTURE rule 7 (data-driven), same shape as image_filter_op_registry / value_op_registry:
// adding a field op = add ONE leaf .cpp (field_ops_<name>.cpp) that ends with a file-scope
// `FieldOp` registrar. No shared list file is edited.
//
// A field op differs from an image-filter op in WHAT it produces: not a cooked texture, but a node
// in a field graph (a FieldNode subtree). So the registrar feeds two sinks:
//   (1) fieldSpecSink()    — its NodeSpec (so it appears in the Add menu / findSpec, like any op),
//   (2) fieldNodeFactories() — a factory  type-name -> make a FieldNode for an instance (shortId).
// The FieldNode tree built from these factories is what runtime/field_render.renderField2d consumes
// (assemble MSL -> source PSO -> fullscreen draw). For Build-2 the GPU golden builds its leaf
// directly; the factory sink is the data-driven hook a later graph-cook walk uses to instantiate a
// field op by type name.
//
// Init-order safety (identical to the image-filter / value-op sinks): every registrar is a
// namespace-scope static, so all of them finish their dynamic-init constructors before main runs and
// before any LIVE sink read (node_registry's findSpec/specTypes read the sink live, never snapshot).
//
// FORK / risk (named, same as the sibling registries): intra-family ORDER in the sink follows
// cross-TU dynamic-init order (unspecified). Cosmetic only (Add-menu position); findSpec is keyed by
// type name, wires by port id — neither depends on spec position.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "runtime/graph.h"  // NodeSpec

namespace sw {

struct FieldNode;  // field_graph.h

// Factory: build a fresh FieldNode instance for this op type. `shortId` is the instance's
// collision-free id seed (e.g. a node guid prefix), passed into the node's prefix (BuildNodeId).
using FieldNodeFactory = std::function<std::shared_ptr<FieldNode>(const std::string& shortId)>;

// Meyers singletons — the two sinks every field-op leaf registrar feeds.
std::vector<NodeSpec>& fieldSpecSink();
std::vector<std::pair<std::string, FieldNodeFactory>>& fieldNodeFactories();

// Build a FieldNode for `type` via the registered factory (nullptr if no factory registered).
std::shared_ptr<FieldNode> makeFieldNode(const std::string& type, const std::string& shortId);

// Apply a RESOLVED param map (named-param → value) to a FieldNode, dispatched by op `type` (PF-0
// field-input-projection). The leaf FieldNode subclass is TU-private, so the per-op configure lives in
// the owning leaf TU (which downcasts) and this free function routes to it by type name. PF-0a wires ONLY
// ToroidalVortexField (the field op the particle-field probe needs); every other type is a NO-OP here
// (the node keeps its makeFieldNode-ctor .t3 defaults). PF-0c generalizes the dispatch to all field ops
// (blueprint §1.5 choice C). NOT a frozen-base change — the base never learns about params.
void configureFieldNodeFromParams(FieldNode& node, const std::string& type,
                                  const std::map<std::string, float>& params);

// RAII registrar: declare one file-scope static of this type at the end of each field-op leaf.
//   FieldOp(spec, factory);   // pushes spec into fieldSpecSink() and {spec.type, factory} into
//                             // fieldNodeFactories().
struct FieldOp {
  FieldOp(NodeSpec spec, FieldNodeFactory factory);
};

}  // namespace sw
