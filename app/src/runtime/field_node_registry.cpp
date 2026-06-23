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

std::shared_ptr<FieldNode> makeFieldNode(const std::string& type, const std::string& shortId) {
  for (const auto& [t, factory] : fieldNodeFactories())
    if (t == type && factory) return factory(shortId);
  return nullptr;
}

// Per-op from-map configure entries live in their owning leaf TUs (the leaf subclass is TU-private, so
// only that TU can downcast). PF-0a routes ONLY ToroidalVortexField (the probe's field op); each future
// field op adds one extern + one dispatch line here (or PF-0c moves this into the factory signature).
void configureToroidalVortexFieldFromParams(FieldNode&, const std::map<std::string, float>&);

void configureFieldNodeFromParams(FieldNode& node, const std::string& type,
                                  const std::map<std::string, float>& params) {
  if (type == "ToroidalVortexField") configureToroidalVortexFieldFromParams(node, params);
  // else: NO-OP (PF-0a narrow path) — the node keeps its makeFieldNode-ctor .t3 defaults. PF-0c wires
  // the rest of the field ops here (or generalizes via the factory signature, blueprint §1.5 choice C).
}

FieldOp::FieldOp(NodeSpec spec, FieldNodeFactory factory) {
  fieldNodeFactories().push_back({spec.type, std::move(factory)});
  fieldSpecSink().push_back(std::move(spec));
}

}  // namespace sw
