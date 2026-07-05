// runtime/dict_op_registry — the Dict<float>-producer self-registration sinks + resolved-param accessor.
// Meyers singletons (init-order safe, same as floatlist_op_registry.cpp / mesh_op_registry.cpp).
#include "runtime/dict_op_registry.h"

namespace sw {

std::vector<NodeSpec>& dictSpecSink() {
  static std::vector<NodeSpec> v;
  return v;
}

std::map<std::string, DictCookFn>& dictCookFns() {
  static std::map<std::string, DictCookFn> m;
  return m;
}

const DictCookFn* findDictOp(const std::string& type) {
  auto& m = dictCookFns();
  auto it = m.find(type);
  return it != m.end() ? &it->second : nullptr;
}

bool& dictInjectBug() {
  static bool b = false;
  return b;
}

DictOp::DictOp(NodeSpec spec, DictCookFn cook) {
  dictCookFns()[spec.type] = cook;
  dictSpecSink().push_back(std::move(spec));
}

// Resolved-param accessor (mirror floatListParam). Defined here so the leaf ops + goldens can read
// params without depending on the point-graph TU.
float dictParam(const std::map<std::string, float>* params, const char* id, float def) {
  if (!params) return def;
  auto it = params->find(id);
  return it != params->end() ? it->second : def;
}

}  // namespace sw
