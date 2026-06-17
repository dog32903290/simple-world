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

FieldOp::FieldOp(NodeSpec spec, FieldNodeFactory factory) {
  fieldNodeFactories().push_back({spec.type, std::move(factory)});
  fieldSpecSink().push_back(std::move(spec));
}

}  // namespace sw
