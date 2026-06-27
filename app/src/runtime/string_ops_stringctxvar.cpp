// runtime/string_ops_stringctxvar — the STRING-channel context-var seam (sub-seam C): SetStringVar +
// GetStringVar. The string twin of the float/int/vec3 ctx-var ops (stateful_value_ops_context_vars.cpp),
// but on the STRING currency: GetStringVar emits a Slot<string> (NOT a float), so these ride the String
// cook flow (cookStringNodes / extStrOut), not the stateful FLOAT cook. They read/write the host per-frame
// ContextVarMap.stringVars (= TiXL context.StringVariables, a typed Dictionary<string,string>,
// EvaluationContext.cs — the SAME typed-channel approach sw uses for floatVars, NOT a boxed-object fork).
//
// TiXL ground truth (read-only external/tixl):
//   GetStringVar.cs:19-26 — reads context.StringVariables[VariableName]; on a miss → FallbackDefault.
//   SetStringVar.cs:18-45 — the no-SubGraph branch (cs:42-45): context.StringVariables[name] = newValue;
//                           empty name → no-op (cs:20-24, string.IsNullOrEmpty). DROP ICustomDropdownHolder.
//   .t3 DEFAULT AUDIT (the .t3 DefaultValue OVERRIDES the C# ctor — the load-bearing default):
//     GetStringVariable.t3: VariableName default "s", FallbackDefault default "" (empty).
//     SetStringVariable.t3: VariableName default "s", StringValue default "", ClearAfterExecution false,
//                           SubGraph null.  → the NodeSpec strDef for VariableName is "s" for BOTH.
//
// ★DEFERRED (named loudly) — defer-setstringvar-subgraph-command-rail: SetStringVar's SubGraph push/restore
//   scope (SetStringVar.cs:26-41, the `if (SubGraph.HasInputConnections)` branch) is the SAME Command-rail
//   scoping that float/int's SetVarCmd is — NOT implemented here. sw's two-rail model can't put a String echo
//   output AND a Command SubGraph output on one node-spec (the precedent is point_ops_setvarcmd.cpp:
//   SetFloatVarCmd). THIS leaf is the no-SubGraph branch only (cs:42-45 — the flat map write). A future
//   "SetStringVarCmd" Command type carries the SubGraph half, exactly as SetFloatVarCmd did for the float rail.
//
// NAMED FORK (fork-setstringvar-echo-output): TiXL's SetStringVar.Output is a Slot<Command> (no value-rail
//   analog). sw gives it a String echo Output (the written value) — the SAME echo-as-golden-probe fork
//   SetFloatVar/SetVec3Var took. The real product is the map mutation; the echo is the readable golden probe
//   (and makes SetStringVar a String producer so cookStringNodes writes its extStrOut[0]).
//
// runtime leaf: pure CPU, no hardware, no UI.
#include <map>
#include <string>
#include <vector>

#include "runtime/graph.h"               // NodeSpec, PortSpec, Widget
#include "runtime/stateful_value_ops.h"  // ContextVarMap (complete type — touch .stringVars)
#include "runtime/string_op_registry.h"  // StringOp / StringCookCtx / stringInjectBug

namespace sw {
namespace {

// inputStrings layout (cookResidentString gathers String input ports in spec order, one entry per single
// String port): GetStringVar = [VariableName(0), FallbackDefault(1)]; SetStringVar = [VariableName(0),
// StringValue(1)]. VariableName is ALWAYS the FIRST String input port (matches the float-channel convention).
const std::string& gatheredOr(const std::vector<std::string>* v, size_t i, const std::string& def) {
  static const std::string empty;
  if (!v || i >= v->size()) return def;
  return (*v)[i];
}

// --- GetStringVar (TiXL GetStringVar.cs:19-26) — read stringVars[VariableName], else FallbackDefault.
// Empty VariableName is a normal lookup miss → fallback (sw's name is always a resolved string, never null;
// the TiXL `variableName != null` guard is vacuous here). .t3 defaults: VariableName="s", FallbackDefault="".
void cookGetStringVar(StringCookCtx& c) {
  if (!c.output) return;
  static const std::string kEmpty;
  const std::string& name = gatheredOr(c.inputStrings, 0, kEmpty);      // VariableName (String input 0)
  const std::string& fallback = gatheredOr(c.inputStrings, 1, kEmpty);  // FallbackDefault (String input 1)
  if (c.ctxVars && !name.empty()) {
    auto it = c.ctxVars->stringVars.find(name);
    if (it != c.ctxVars->stringVars.end()) {  // TryGetValue hit (GetStringVar.cs:22)
      *c.output = it->second;
      if (stringInjectBug() && !c.output->empty()) c.output->pop_back();  // teeth on the REAL cook path
      return;
    }
  }
  *c.output = fallback;  // unset / empty name → FallbackDefault.GetValue(context) (GetStringVar.cs:25)
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();
}

// --- SetStringVar (TiXL SetStringVar.cs:42-45, the no-SubGraph branch) — write stringVars[name]=value.
// Empty name → no-op (cs:20-24, string.IsNullOrEmpty). Output ECHOES the written value (NAMED FORK — the
// Command has no value-rail analog; the echo is the golden probe). .t3 defaults: VariableName="s", StringValue="".
void cookSetStringVar(StringCookCtx& c) {
  if (!c.output) return;
  static const std::string kEmpty;
  const std::string& name = gatheredOr(c.inputStrings, 0, kEmpty);   // VariableName (String input 0)
  const std::string& value = gatheredOr(c.inputStrings, 1, kEmpty);  // StringValue (String input 1)
  *c.output = value;  // echo (Command has no value; this is the golden probe)
  if (stringInjectBug() && !c.output->empty()) c.output->pop_back();  // teeth on the REAL cook path
  if (name.empty() || !c.ctxVars) return;  // string.IsNullOrEmpty(name) → no-op (SetStringVar.cs:20-24)
  c.ctxVars->stringVars[name] = value;     // no-SubGraph branch: context.StringVariables[name]=newValue
}

}  // namespace

// Self-registration. PortSpec positional: {id, name, dataType, isInput, def, minV, maxV, widget, labels,
// pinless, vecArity, multiInput, strDef}. VariableName strDef "s" = the .t3 DefaultValue (audited above).
// VariableName is the FIRST String INPUT port (so cookResidentString gathers it at inputStrings[0]).

// GetStringVar — Result(String out) first; VariableName(String in, strDef "s"); FallbackDefault(String in, "").
static const StringOp _reg_getstringvar{
    {"GetStringVar", "GetStringVar",
     {{"Result", "Result", "String", false},
      {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "s"},
      {"FallbackDefault", "FallbackDefault", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""}},
     /*evaluate=*/nullptr},  // String output cannot ride NodeSpec::evaluate (returns ONE float)
    cookGetStringVar};

// SetStringVar — Output(String echo out, NAMED FORK) first; VariableName(String in, strDef "s");
// StringValue(String in, ""). The SubGraph push/restore branch is DEFERRED (see header).
static const StringOp _reg_setstringvar{
    {"SetStringVar", "SetStringVar",
     {{"Output", "Output", "String", false},
      {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "s"},
      {"StringValue", "StringValue", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, ""}},
     /*evaluate=*/nullptr},
    cookSetStringVar};

}  // namespace sw
