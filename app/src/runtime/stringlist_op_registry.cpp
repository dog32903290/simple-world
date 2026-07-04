// runtime/stringlist_op_registry — the stringlist-op self-registration sinks + resolved-param accessor.
// Meyers singletons (init-order safe). VERBATIM-shaped clone of colorlist_op_registry.cpp over std::string.
#include "runtime/stringlist_op_registry.h"

#include <set>

namespace sw {

std::vector<NodeSpec>& stringListSpecSink() {
  static std::vector<NodeSpec> v;
  return v;
}

std::map<std::string, StringListCookFn>& stringListCookFns() {
  static std::map<std::string, StringListCookFn> m;
  return m;
}

const StringListCookFn* findStringListOp(const std::string& type) {
  auto& m = stringListCookFns();
  auto it = m.find(type);
  return it != m.end() ? &it->second : nullptr;
}

bool& stringListInjectBug() {
  static bool b = false;
  return b;
}

// The set of STATEFUL stringlist op type names (Meyers singleton; the registrar inserts KeepStrings).
// stringListOpIsStateful reads this; the cook drivers thread state + apply the cook-once advance guard
// to members only (mirror of floatListStatefulSet / AmplifyValues).
static std::set<std::string>& stringListStatefulSet() {
  static std::set<std::string> s;
  return s;
}

bool stringListOpIsStateful(const std::string& type) {
  auto& s = stringListStatefulSet();
  return s.find(type) != s.end();
}

StringListOp::StringListOp(NodeSpec spec, StringListCookFn cook, bool stateful) {
  stringListCookFns()[spec.type] = cook;
  if (stateful) stringListStatefulSet().insert(spec.type);
  stringListSpecSink().push_back(std::move(spec));
}

// Resolved-param accessor (mirror colorListParam). Defined here so the leaf ops + goldens can read
// params without depending on the point-graph TU.
float stringListParam(const std::map<std::string, float>* params, const char* id, float def) {
  if (!params) return def;
  auto it = params->find(id);
  return it != params->end() ? it->second : def;
}

}  // namespace sw
