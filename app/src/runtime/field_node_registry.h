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

// Per-op param-apply fn (PF-0c): project a RESOLVED param map onto a freshly-built FieldNode of this op's
// type. The leaf subclass is TU-private, so the configurer lives in the owning leaf TU (which downcasts +
// runs its slot table). A leaf that registers no configurer (or registers nullptr — TransformField /
// CustomSDF / Image2dSDF, the PF-0d matrix/string/texture ops) is an explicit NO-OP: the node keeps its
// ctor .t3 defaults, identical to today's unknown-type no-op. A plain free fn-ptr (no std::function) —
// each configurer is a file-scope fn in its leaf TU.
using FieldConfigureFn = void (*)(FieldNode& node, const std::map<std::string, float>& params);

// PF-0c slot-id guard (Option B — closes DEBT_LEDGER pf0c-slotid-guard-indirection): one row per
// (op type, slot id) that a migrated op's configurer ACTUALLY applies. Each migrated op registers its
// REAL slot ids here at static init (alongside its factory/configurer) — the SAME ids the configurer's
// applyFloatSlot/applyIntSelSlot/applyBoolSelSlot calls use, so the guard reads the real per-op table and
// CANNOT drift from a hand-copied list. The golden's slot-id==port-id guard loops fieldSlotSpecs() ×
// fieldSpecSink() asserting every registered slot id is a real PortSpec.id in that op's spec.
struct FieldSlotSpec {
  std::string opType;  // NodeSpec.type the slot belongs to.
  std::string slotId;  // the slot id the configurer applies (MUST equal a PortSpec.id in opType's spec).
};

// Meyers singletons — the sinks every field-op leaf registrar feeds.
std::vector<NodeSpec>& fieldSpecSink();
std::vector<std::pair<std::string, FieldNodeFactory>>& fieldNodeFactories();
// PF-0c: the configurer sink, mirroring fieldNodeFactories() (keyed by spec.type). One entry per registrar
// (nullptr for ops that pass no configurer). configureFieldNodeFromParams is a table lookup over this.
std::vector<std::pair<std::string, FieldConfigureFn>>& fieldConfigurers();
// PF-0c slot-id guard sink (Option B): every migrated op pushes its real apply-table slot ids here.
std::vector<FieldSlotSpec>& fieldSlotSpecs();

// Build a FieldNode for `type` via the registered factory (nullptr if no factory registered).
std::shared_ptr<FieldNode> makeFieldNode(const std::string& type, const std::string& shortId);

// Apply a RESOLVED param map (named-param → value) to a FieldNode, dispatched by op `type` (PF-0
// field-input-projection). The leaf FieldNode subclass is TU-private, so the per-op configure lives in
// the owning leaf TU (which downcasts) and this free function routes to it by type name. PF-0c generalizes
// the dispatch to ALL migrated field ops via fieldConfigurers(): a TABLE LOOKUP (find `type`, call its fn).
// A type with no registered configurer, or a null configurer (the PF-0d matrix/string/texture ops —
// TransformField / CustomSDF / Image2dSDF), is a NO-OP: the node keeps its makeFieldNode-ctor .t3 defaults,
// the SAME safety contract as the old if-ladder's unknown-type fall-through. NOT a frozen-base change — the
// base never learns about params.
void configureFieldNodeFromParams(FieldNode& node, const std::string& type,
                                  const std::map<std::string, float>& params);

// RAII registrar: declare one file-scope static of this type at the end of each field-op leaf.
//   FieldOp(spec, factory);                     // configurer = nullptr (explicit no-op; PF-0d ops).
//   FieldOp(spec, factory, configurer);         // PF-0c: also pushes {spec.type, configurer} into
//                                               // fieldConfigurers() so configureFieldNodeFromParams routes.
//   FieldOp(spec, factory, configurer, slots);  // PF-0c Option B: ALSO push one {spec.type, slotId} row
//                                               // per id in `slots` into fieldSlotSpecs() (the slot-id
//                                               // guard reads them). `slots` MUST list the SAME ids the
//                                               // configurer applies — one source of truth, no hand copy.
// All overloads push spec into fieldSpecSink() and {spec.type, factory} into fieldNodeFactories().
struct FieldOp {
  FieldOp(NodeSpec spec, FieldNodeFactory factory);
  FieldOp(NodeSpec spec, FieldNodeFactory factory, FieldConfigureFn configurer);
  FieldOp(NodeSpec spec, FieldNodeFactory factory, FieldConfigureFn configurer,
          std::vector<std::string> slotIds);
};

// Standalone slot-id registrar (Option B): register a migrated op's apply-table slot ids into
// fieldSlotSpecs() WITHOUT touching its FieldOp registrar line. Used for an op whose leaf .cpp cannot take
// the 4-arg FieldOp overload (e.g. a frozen-at-line-cap leaf): a separate tiny TU declares one file-scope
// static of this, listing the SAME ids the op's configurer applies. Same single-source-of-truth contract.
struct FieldSlotIds {
  FieldSlotIds(std::string opType, std::vector<std::string> slotIds);
};

}  // namespace sw
