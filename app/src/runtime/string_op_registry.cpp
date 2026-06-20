// runtime/string_op_registry — the string-op self-registration sinks + resolved-param accessor.
// Meyers singletons (init-order safe, same as floatlist_op_registry.cpp / mesh_op_registry.cpp).
#include "runtime/string_op_registry.h"

namespace sw {

std::vector<NodeSpec>& stringSpecSink() {
  static std::vector<NodeSpec> v;
  return v;
}

std::map<std::string, StringCookFn>& stringCookFns() {
  static std::map<std::string, StringCookFn> m;
  return m;
}

const StringCookFn* findStringOp(const std::string& type) {
  auto& m = stringCookFns();
  auto it = m.find(type);
  return it != m.end() ? &it->second : nullptr;
}

bool& stringInjectBug() {
  static bool b = false;
  return b;
}

StringOp::StringOp(NodeSpec spec, StringCookFn cook) {
  stringCookFns()[spec.type] = cook;
  stringSpecSink().push_back(std::move(spec));
}

// Resolved-param accessor (mirror floatListParam in floatlist_op_registry.cpp). Defined here so the
// leaf ops + goldens can read Float params without depending on the point-graph TU.
float stringFloatParam(const std::map<std::string, float>* params, const char* id, float def) {
  if (!params) return def;
  auto it = params->find(id);
  return it != params->end() ? it->second : def;
}

}  // namespace sw
