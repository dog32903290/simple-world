// runtime/field_node_registry — see field_node_registry.h. Self-registration sinks for field ops.
#include "runtime/field_node_registry.h"

#include "runtime/field_graph.h"  // FieldNode

namespace sw {

std::vector<NodeSpec>& fieldSpecSink() {
  static std::vector<NodeSpec> s;
  return s;
}

std::vector<std::pair<std::string, FieldNodeFactory>>& fieldNodeFactories() {
  static std::vector<std::pair<std::string, FieldNodeFactory>> s;
  return s;
}

std::vector<std::pair<std::string, FieldConfigureFn>>& fieldConfigurers() {
  static std::vector<std::pair<std::string, FieldConfigureFn>> s;
  return s;
}

std::vector<FieldSlotSpec>& fieldSlotSpecs() {
  static std::vector<FieldSlotSpec> s;
  return s;
}

std::shared_ptr<FieldNode> makeFieldNode(const std::string& type, const std::string& shortId) {
  for (const auto& [t, factory] : fieldNodeFactories())
    if (t == type && factory) return factory(shortId);
  return nullptr;
}

// PF-0c: TABLE LOOKUP over fieldConfigurers() (each leaf registers its own configurer alongside its
// factory; the leaf subclass is TU-private so the downcast + slot table live in that TU). No per-type
// branch here. A type with no entry, or a null configurer (PF-0d matrix/string/texture ops), is a NO-OP:
// the node keeps its ctor .t3 defaults — the SAME safety as the old if-ladder's unknown-type fall-through.
void configureFieldNodeFromParams(FieldNode& node, const std::string& type,
                                  const std::map<std::string, float>& params) {
  for (const auto& [t, configure] : fieldConfigurers())
    if (t == type) {
      if (configure) configure(node, params);
      return;
    }
  // No registered configurer for `type` → NO-OP (node keeps ctor .t3 defaults).
}

FieldOp::FieldOp(NodeSpec spec, FieldNodeFactory factory)
    : FieldOp(std::move(spec), std::move(factory), nullptr) {}

FieldOp::FieldOp(NodeSpec spec, FieldNodeFactory factory, FieldConfigureFn configurer) {
  fieldNodeFactories().push_back({spec.type, std::move(factory)});
  fieldConfigurers().push_back({spec.type, configurer});
  fieldSpecSink().push_back(std::move(spec));
}

FieldOp::FieldOp(NodeSpec spec, FieldNodeFactory factory, FieldConfigureFn configurer,
                 std::vector<std::string> slotIds) {
  // Push the op's REAL apply-table slot ids into the guard sink BEFORE moving spec.type away. Option B:
  // one source of truth — these are the SAME ids the configurer applies.
  for (std::string& id : slotIds) fieldSlotSpecs().push_back({spec.type, std::move(id)});
  fieldNodeFactories().push_back({spec.type, std::move(factory)});
  fieldConfigurers().push_back({spec.type, configurer});
  fieldSpecSink().push_back(std::move(spec));
}

FieldSlotIds::FieldSlotIds(std::string opType, std::vector<std::string> slotIds) {
  for (std::string& id : slotIds) fieldSlotSpecs().push_back({opType, std::move(id)});
}

}  // namespace sw
