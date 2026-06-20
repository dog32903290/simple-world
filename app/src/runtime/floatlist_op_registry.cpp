// runtime/floatlist_op_registry — the floatlist-op self-registration sinks + resolved-param accessor.
// Meyers singletons (init-order safe, same as mesh_op_registry.cpp / field_node_registry.cpp).
#include "runtime/floatlist_op_registry.h"

namespace sw {

std::vector<NodeSpec>& floatListSpecSink() {
  static std::vector<NodeSpec> v;
  return v;
}

std::map<std::string, FloatListCookFn>& floatListCookFns() {
  static std::map<std::string, FloatListCookFn> m;
  return m;
}

const FloatListCookFn* findFloatListOp(const std::string& type) {
  auto& m = floatListCookFns();
  auto it = m.find(type);
  return it != m.end() ? &it->second : nullptr;
}

bool& floatListInjectBug() {
  static bool b = false;
  return b;
}

FloatListOp::FloatListOp(NodeSpec spec, FloatListCookFn cook) {
  floatListCookFns()[spec.type] = cook;
  floatListSpecSink().push_back(std::move(spec));
}

// Resolved-param accessor (mirror cookMeshParam in mesh_op_registry.cpp). Defined here so the leaf
// ops + goldens can read params without depending on the point-graph TU.
float floatListParam(const std::map<std::string, float>* params, const char* id, float def) {
  if (!params) return def;
  auto it = params->find(id);
  return it != params->end() ? it->second : def;
}

}  // namespace sw
