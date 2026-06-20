// runtime/host_scalar_op_registry — the host-scalar-op self-registration sinks + the eval-side
// predicate + the resolved-param accessor. Meyers singletons (init-order safe, same as
// floatlist_op_registry.cpp / string_op_registry.cpp).
#include "runtime/host_scalar_op_registry.h"

namespace sw {

std::vector<NodeSpec>& hostScalarSpecSink() {
  static std::vector<NodeSpec> v;
  return v;
}

std::map<std::string, HostScalarCookFn>& hostScalarCookFns() {
  static std::map<std::string, HostScalarCookFn> m;
  return m;
}

std::set<std::string>& hostScalarTypes() {
  static std::set<std::string> s;
  return s;
}

const HostScalarCookFn* findHostScalarOp(const std::string& type) {
  auto& m = hostScalarCookFns();
  auto it = m.find(type);
  return it != m.end() ? &it->second : nullptr;
}

bool isHostScalarOp(const std::string& type) {
  auto& s = hostScalarTypes();
  return s.find(type) != s.end();
}

void registerHostScalarType(const std::string& type) { hostScalarTypes().insert(type); }

bool& hostScalarInjectBug() {
  static bool b = false;
  return b;
}

HostScalarOp::HostScalarOp(NodeSpec spec, HostScalarCookFn cook) {
  hostScalarCookFns()[spec.type] = cook;
  hostScalarTypes().insert(spec.type);
  hostScalarSpecSink().push_back(std::move(spec));
}

// Resolved-param accessor (mirror floatListParam). `def` when the driver supplied no map (a golden
// may hand-build a ctx with params == nullptr).
float hostScalarParam(const std::map<std::string, float>* params, const char* id, float def) {
  if (!params) return def;
  auto it = params->find(id);
  return it != params->end() ? it->second : def;
}

}  // namespace sw
