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
// LogLevel (param-completion fan-out, MappedType<LogLevels> SetIntVar.cs:77-78, .t3 default 0=None):
// on the no-SubGraph value rail the ONLY reachable log is the empty-name warning (cs:32-33,
// `logLevel >= Warnings`); the Changes/AllUpdates debug lines (cs:47-55) live in the SubGraph branch,
// which is the Cmd-rail fork (SetIntVarCmd). Gated emission proves the knob is behaviour-bearing.
void stepSetIntVar(const std::map<std::string, float>& in, float, float, StatefulValueState&,
                   float out[3], const TransportSnapshot&, ContextVarMap* vars,
                   const std::string& varName) {
  const long newValue = (long)std::trunc(getIn(in, "Value", 0.0f));  // C# (int) cast = truncate
  out[0] = (float)newValue;
  int logLevel = (int)std::trunc(getIn(in, "LogLevel", 0.0f));  // 0=None 1=Warnings 2=Changes 3=AllUpdates
  if (ctxVarLogBug()) logLevel = 3;  // -bug: ignore the knob, always emit (dead-knob degeneracy)
  if (varName.empty() || !vars) {
    if (varName.empty() && logLevel >= 1) ctxVarLogSink().warnings++;  // cs:32-33 Log.Warning invalid name
    return;
  }
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
  // LogUpdates (param-completion fan-out, MappedType<LogLevels> GetIntVar.cs:60-61, .t3 default 0=None):
  // on a HIT, logUpdates >= AllUpdates(3) → Log.Debug "int X is V" (cs:34-35). on a MISS, complain when
  // >= Warnings(1) (or == AllUpdates) → Log.Warning "Can't read undefined int" (cs:41-48). NAMED FORK vs
  // TiXL: sw drops the _complainedOnce once-latch (no cross-frame editor-noise suppression; a stateful
  // value op has no per-instance "complained" memory wired here) — every miss at >=Warnings emits. The
  // gating BY LEVEL is faithful; only the once-de-dupe is dropped (editor-telemetry, same class as the
  // dropped LogMessage perf timing). Read value/fallback is byte-identical at every level.
  int logUpdates = (int)std::trunc(getIn(in, "LogUpdates", 0.0f));
  if (ctxVarLogBug()) logUpdates = 3;  // -bug: ignore the knob, always emit (dead-knob degeneracy)
  if (vars) {
    auto it = vars->intVars.find(varName);
    if (it != vars->intVars.end()) {
      out[0] = (float)it->second;                      // TryGetValue hit
      if (logUpdates >= 3) ctxVarLogSink().updates++;  // cs:34-35 Log.Debug on hit at AllUpdates
      return;
    }
  }
  out[0] = (float)fallback;                   // unset → fallbackValue
  if (logUpdates >= 1) ctxVarLogSink().warnings++;  // cs:41-48 Log.Warning on undefined read
}

// --- SetBoolVar (TiXL Lib/flow/context/SetBoolVar.cs) — writes the bool (0/1) into vars.intVars[name]
// (no-SubGraph branch cs:37-40). NAMED FORK: sw has no boolVars dict (ContextVarMap = floatVars+intVars),
// so bool rides the INT channel as 0/1. BoolValue arrives on a Float port (no Bool port) → !=0 ⇒ 1. Empty
// name → no-op (cs:19-23). out[0] echoes the stored 0/1 (golden). .t3 default: BoolValue=false, Name="b".
void stepSetBoolVar(const std::map<std::string, float>& in, float, float, StatefulValueState&,
                    float out[3], const TransportSnapshot&, ContextVarMap* vars,
                    const std::string& varName) {
  const long newValue = (getIn(in, "BoolValue", 0.0f) != 0.0f) ? 1 : 0;  // bool (0/1) on the int channel
  out[0] = (float)newValue;
  if (varName.empty() || !vars) return;
  vars->intVars[varName] = newValue;
}

// --- GetBoolVar (TiXL Lib/flow/context/GetBoolVar.cs:15-29) — reads vars.intVars[name] (!=0 ⇒ 1), else
// FallbackDefault (!=0 ⇒ 1). NAMED FORK: reads the INT channel (no boolVars dict). DROP ICustomDropdownHolder
// (editor UI). FallbackDefault on a Float port carrying a bool. .t3 default: VariableName="b", Fallback=false.
void stepGetBoolVar(const std::map<std::string, float>& in, float, float, StatefulValueState&,
                    float out[3], const TransportSnapshot&, ContextVarMap* vars,
                    const std::string& varName) {
  const float fallback = (getIn(in, "FallbackDefault", 0.0f) != 0.0f) ? 1.0f : 0.0f;
  if (vars) {
    auto it = vars->intVars.find(varName);
    if (it != vars->intVars.end()) { out[0] = (it->second != 0) ? 1.0f : 0.0f; return; }  // TryGetValue hit
  }
  out[0] = fallback;                          // unset → FallbackDefault.GetValue(context)
}

// --- SetVec3Var (TiXL Lib/flow/context/SetVec3Var.cs) — writes vars.vec3Vars[name]=(x,y,z) (no-
// SubGraph branch cs:42-44). NAMED FORK fork-ctxvar-vec3-typed-channel: TiXL boxes a Vector3 into
// context.ObjectVariables (object dict); sw stores it on a TYPED vec3 channel (same choice it made
// for float/int — round-trip value byte-identical). Vec3 arrives as 3 Float ports (sw vec-as-3-floats,
// like AddVec3) → in["Vec3Value.x/.y/.z"]. Empty name → no-op (cs:20-24, string.IsNullOrEmpty). out[0..2]
// ECHO the written (x,y,z) (TiXL output is a Command passthrough with no value-rail analog — the real
// product is the map mutation; echo is the golden probe). .t3 defaults: VariableName="pos", Vec3Value=(0,0,0).
void stepSetVec3Var(const std::map<std::string, float>& in, float, float, StatefulValueState&,
                    float out[3], const TransportSnapshot&, ContextVarMap* vars,
                    const std::string& varName) {
  const float x = getIn(in, "Vec3Value.x", 0.0f);
  const float y = getIn(in, "Vec3Value.y", 0.0f);
  const float z = getIn(in, "Vec3Value.z", 0.0f);
  out[0] = x; out[1] = y; out[2] = z;          // echo (Command has no value; golden probe)
  if (varName.empty() || !vars) return;        // string.IsNullOrEmpty(name) → no-op
  vars->vec3Vars[varName] = {x, y, z};         // no-SubGraph branch: context.ObjectVariables[name]=v
}

// --- GetVec3Var (TiXL Lib/flow/context/GetVec3Var.cs:24-31) — reads vars.vec3Vars[name] onto
// out[0..2] (Result.x/.y/.z), else FallbackDefault. TiXL casts ObjectVariables[name] `is Vector3`;
// the typed channel makes the cast a plain map hit (a non-vec3 of the same name can't collide here —
// it lives on floatVars/intVars). DROP ICustomDropdownHolder (editor UI). .t3: VariableName="pos",
// FallbackDefault=(0,0,0).
void stepGetVec3Var(const std::map<std::string, float>& in, float, float, StatefulValueState&,
                    float out[3], const TransportSnapshot&, ContextVarMap* vars,
                    const std::string& varName) {
  if (vars) {
    auto it = vars->vec3Vars.find(varName);
    if (it != vars->vec3Vars.end()) {           // TryGetValue + `is Vector3` hit
      out[0] = it->second[0]; out[1] = it->second[1]; out[2] = it->second[2];
      return;
    }
  }
  out[0] = getIn(in, "FallbackDefault.x", 0.0f);  // unset → FallbackDefault.GetValue(context)
  out[1] = getIn(in, "FallbackDefault.y", 0.0f);
  out[2] = getIn(in, "FallbackDefault.z", 0.0f);
}

}  // namespace

// context-var seam: the Set*Var writer family (run before any reader in the 2-pass cook). Kept as
// an explicit name list rather than a prefix match — explicit is refuter-auditable and a future
// "SetupX" op can't accidentally join the writer pass. SetBoolVar joins (bool rides intVars 0/1);
// SetVec3Var joins (writes the typed vec3 channel).
bool isContextVarWriter(const std::string& opType) {
  return opType == "SetFloatVar" || opType == "SetIntVar" || opType == "SetBoolVar"
      || opType == "SetVec3Var";
}

// The LogLevel/LogUpdates diagnostic sink (declared in stateful_value_ops.h). Process-scoped leaf data
// the golden flips/reads to prove the new enum knobs gate emission. No upper consumer wired yet (sw has
// no editor log pane) — same deferred-consumer shape as point_ops_logmessage.cpp's logSink.
CtxVarLogSink& ctxVarLogSink() { static CtxVarLogSink s; return s; }
bool& ctxVarLogBug() { static bool v = false; return v; }

static const StatefulOpReg _reg_SetFloatVar{"SetFloatVar", stepSetFloatVar};
static const StatefulOpReg _reg_GetFloatVar{"GetFloatVar", stepGetFloatVar};
static const StatefulOpReg _reg_SetIntVar{"SetIntVar", stepSetIntVar};
static const StatefulOpReg _reg_GetIntVar{"GetIntVar", stepGetIntVar};
static const StatefulOpReg _reg_SetBoolVar{"SetBoolVar", stepSetBoolVar};
static const StatefulOpReg _reg_GetBoolVar{"GetBoolVar", stepGetBoolVar};
static const StatefulOpReg _reg_SetVec3Var{"SetVec3Var", stepSetVec3Var};
static const StatefulOpReg _reg_GetVec3Var{"GetVec3Var", stepGetVec3Var};

}  // namespace sw
