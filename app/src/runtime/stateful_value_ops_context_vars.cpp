// runtime/stateful_value_ops_context_vars — the context-var YELLOW seam family:
//   SetFloatVar / GetFloatVar / SetIntVar / GetIntVar.  These read/write the shared ContextVarMap.
// Also owns isContextVarWriter (the 2-pass writer-before-reader predicate used by frame_cook).
// Split VERBATIM from the old stateful_value_ops.cpp monolith (debt sprint, zero behavior change).
//
// runtime leaf: pure computation, no hardware, no UI.
#include <cmath>
#include <map>
#include <string>

#include "runtime/stateful_value_ops.h"
#include "runtime/stateful_value_op_registry.h"
#include "runtime/stateful_value_ops_internal.h"  // getIn

namespace sw {
namespace {

// ============================ context-var YELLOW seam ops ============================
// These are "stateful" in the cook sense (evaluate==nullptr, cooked once per frame into extOut) but
// their cross-frame channel is the SHARED ContextVarMap, not StatefulValueState. They consume the
// resolved String VariableName param (varName, from ResidentNode::strInputs) + the per-frame `vars`
// map. The 2-pass ordering (cookStatefulValueNodes) guarantees all Set*Var run before any Get*Var.

// --- SetFloatVar (TiXL Lib/flow/context/SetFloatVar.cs) — writes vars.floatVars[name]=FloatValue.
// We implement ONLY the no-SubGraph branch (SetFloatVar.cs:42-45): there is no Command sub-tree in
// the value rail (NAMED FORK — see header). Empty name → no-op (cs:20-24, string.IsNullOrEmpty).
// The TiXL output is a Command passthrough (no value-rail analog) → out[0] ECHOES the written value
// (golden anchor; the real product is the map mutation). .t3 defaults: FloatValue=0, VariableName="f".
void stepSetFloatVar(const std::map<std::string, float>& in, float, float, StatefulValueState&,
                     float out[3], const TransportSnapshot&, ContextVarMap* vars,
                     const std::string& varName) {
  const float newValue = getIn(in, "FloatValue", 0.0f);
  out[0] = newValue;                          // echo (Command has no value; this is the golden probe)
  if (varName.empty() || !vars) return;       // string.IsNullOrEmpty(name) → no-op
  vars->floatVars[varName] = newValue;        // no-SubGraph branch: context.FloatVariables[name]=v
}

// --- GetFloatVar (TiXL Lib/flow/context/GetFloatVar.cs:14-28) — reads vars.floatVars[name], else
// FallbackDefault. DROP ICustomDropdownHolder (editor UI). .t3 defaults: VariableName="f", Fallback=0.
void stepGetFloatVar(const std::map<std::string, float>& in, float, float, StatefulValueState&,
                     float out[3], const TransportSnapshot&, ContextVarMap* vars,
                     const std::string& varName) {
  const float fallback = getIn(in, "FallbackDefault", 0.0f);
  if (vars) {
    auto it = vars->floatVars.find(varName);
    if (it != vars->floatVars.end()) { out[0] = it->second; return; }  // TryGetValue hit
  }
  out[0] = fallback;                          // unset → FallbackDefault.GetValue(context)
}

// --- SetIntVar (TiXL Lib/flow/context/SetIntVar.cs) — writes vars.intVars[name]=(int)Value (no-
// SubGraph branch cs:61-64). Value arrives on a Float port (no Int port type) → C# (int) cast =
// TRUNCATION toward zero ((long)std::trunc; 7.9→7), NOT rounding (CountInt convention :709). Empty
// name → no-op (cs:30-36). out[0] echoes the stored int (golden). .t3 defaults: Value=0, Name="i".
void stepSetIntVar(const std::map<std::string, float>& in, float, float, StatefulValueState&,
                   float out[3], const TransportSnapshot&, ContextVarMap* vars,
                   const std::string& varName) {
  const long newValue = (long)std::trunc(getIn(in, "Value", 0.0f));  // C# (int) cast = truncate
  out[0] = (float)newValue;
  if (varName.empty() || !vars) return;
  vars->intVars[varName] = newValue;
}

// --- GetIntVar (TiXL Lib/flow/context/GetIntVar.cs:16-50) — reads vars.intVars[name], else
// FallbackValue. FallbackValue arrives on a Float port carrying an int (TiXL Slot<int>) → truncate
// toward zero. Result is int-valued. DROP LogLevels enum + ICustomDropdownHolder. NAMED FORK: TiXL
// returns early (no write) when variableName==null; our name is always a resolved string (never
// null) — an empty name is a normal lookup miss → fallback (cs:30-39). .t3 default: VariableName="i".
void stepGetIntVar(const std::map<std::string, float>& in, float, float, StatefulValueState&,
                   float out[3], const TransportSnapshot&, ContextVarMap* vars,
                   const std::string& varName) {
  const long fallback = (long)std::trunc(getIn(in, "FallbackValue", 0.0f));
  if (vars) {
    auto it = vars->intVars.find(varName);
    if (it != vars->intVars.end()) { out[0] = (float)it->second; return; }  // TryGetValue hit
  }
  out[0] = (float)fallback;                   // unset → fallbackValue
}

}  // namespace

// context-var seam: the Set*Var writer family (run before any reader in the 2-pass cook). Kept as
// an explicit name list (4 ops) rather than a prefix match — explicit is refuter-auditable and a
// future "SetupX" op can't accidentally join the writer pass.
bool isContextVarWriter(const std::string& opType) {
  return opType == "SetFloatVar" || opType == "SetIntVar";
}

static const StatefulOpReg _reg_SetFloatVar{"SetFloatVar", stepSetFloatVar};
static const StatefulOpReg _reg_GetFloatVar{"GetFloatVar", stepGetFloatVar};
static const StatefulOpReg _reg_SetIntVar{"SetIntVar", stepSetIntVar};
static const StatefulOpReg _reg_GetIntVar{"GetIntVar", stepGetIntVar};

}  // namespace sw
