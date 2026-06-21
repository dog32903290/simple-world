// runtime/colorlist_op_registry — the colorlist-op self-registration sinks + resolved-param accessor.
// Meyers singletons (init-order safe). VERBATIM clone of floatlist_op_registry.cpp over simd::float4.
#include "runtime/colorlist_op_registry.h"

namespace sw {

std::vector<NodeSpec>& colorListSpecSink() {
  static std::vector<NodeSpec> v;
  return v;
}

std::map<std::string, ColorListCookFn>& colorListCookFns() {
  static std::map<std::string, ColorListCookFn> m;
  return m;
}

const ColorListCookFn* findColorListOp(const std::string& type) {
  auto& m = colorListCookFns();
  auto it = m.find(type);
  return it != m.end() ? &it->second : nullptr;
}

bool& colorListInjectBug() {
  static bool b = false;
  return b;
}

ColorListOp::ColorListOp(NodeSpec spec, ColorListCookFn cook) {
  colorListCookFns()[spec.type] = cook;
  colorListSpecSink().push_back(std::move(spec));
}

// Resolved-param accessor (mirror floatListParam). Defined here so the leaf ops + goldens can read
// params without depending on the point-graph TU.
float colorListParam(const std::map<std::string, float>* params, const char* id, float def) {
  if (!params) return def;
  auto it = params->find(id);
  return it != params->end() ? it->second : def;
}

}  // namespace sw
