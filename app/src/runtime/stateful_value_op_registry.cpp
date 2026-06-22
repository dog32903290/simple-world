// runtime/stateful_value_op_registry — the stateful-op self-registration sink + registrar ctor + the
// two public-API entry points that read the sink (isStatefulValueOp / cookStatefulValueOp). These were
// the bottom of the old stateful_value_ops.cpp monolith; they now read the data-driven sink instead of
// the deleted hardcoded kStatefulValueOps[] array. Meyers singleton (init-order safe, same as
// string_op_registry.cpp / value_op_registry.cpp).
#include "runtime/stateful_value_op_registry.h"

#include "runtime/stateful_value_ops.h"  // full StatefulValueState/TransportSnapshot/ContextVarMap + public-API decls

namespace sw {

std::vector<StatefulOp>& statefulOpSink() {
  static std::vector<StatefulOp> v;
  return v;
}

const StatefulOp* findStatefulOp(const std::string& type) {
  for (const auto& o : statefulOpSink())
    if (type == o.type) return &o;
  return nullptr;
}

StatefulOpReg::StatefulOpReg(
    const char* type,
    void (*step)(const std::map<std::string, float>&, float, float, StatefulValueState&, float[8],
                 const TransportSnapshot&, ContextVarMap*, const std::string&)) {
  statefulOpSink().push_back(StatefulOp{type, step});
}

// === public API (declared in stateful_value_ops.h) — reads the sink, not the old array ===

bool isStatefulValueOp(const std::string& opType) { return findStatefulOp(opType) != nullptr; }

void cookStatefulValueOp(const std::string& opType, const std::map<std::string, float>& in,
                         float dt, float time, StatefulValueState& st, float out[8],
                         const TransportSnapshot& tr, ContextVarMap* vars,
                         const std::string& varName) {
  if (const StatefulOp* o = findStatefulOp(opType)) o->step(in, dt, time, st, out, tr, vars, varName);
}

}  // namespace sw
