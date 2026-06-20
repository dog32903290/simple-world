// runtime/gradient_op_registry — the gradient-op self-registration sinks + resolved-param accessor.
// Meyers singletons (init-order safe, same as floatlist_op_registry.cpp / pointlist_op_registry.cpp).
#include "runtime/gradient_op_registry.h"

namespace sw {

std::vector<NodeSpec>& gradientSpecSink() {
  static std::vector<NodeSpec> v;
  return v;
}

std::map<std::string, GradientCookFn>& gradientCookFns() {
  static std::map<std::string, GradientCookFn> m;
  return m;
}

const GradientCookFn* findGradientOp(const std::string& type) {
  auto& m = gradientCookFns();
  auto it = m.find(type);
  return it != m.end() ? &it->second : nullptr;
}

bool& gradientInjectBug() {
  static bool b = false;
  return b;
}

GradientOp::GradientOp(NodeSpec spec, GradientCookFn cook) {
  gradientCookFns()[spec.type] = cook;
  gradientSpecSink().push_back(std::move(spec));
}

// Resolved-param accessor (mirror floatListParam in floatlist_op_registry.cpp). Defined here so the
// leaf ops + goldens can read params without depending on the point-graph TU.
float gradientParam(const std::map<std::string, float>* params, const char* id, float def) {
  if (!params) return def;
  auto it = params->find(id);
  return it != params->end() ? it->second : def;
}

}  // namespace sw
