// runtime/stateful_value_ops_wastrigger — WasTrigger: a one-frame rising-edge pulse on a named
// trigger VARIABLE going up. TiXL numbers/bool/logic/WasTrigger.cs.
//
// Stateful in the cook sense (evaluate==nullptr) AND a context-var READER: like Get*Var it reads the
// shared ContextVarMap.floatVars (the host-side EvaluationContext.FloatVariables), but it ALSO keeps a
// cross-frame rising-edge gate (_lastValue/_wasHit) — so it carries BOTH the var map AND a
// StatefulValueState. It is NOT a writer (NOT in isContextVarWriter) → cooked in the reader pass after
// every Set*Var writer, so a SetFloatVar("__TriggerA", v) earlier in the frame is already visible.
//
// runtime leaf: pure computation, no hardware, no UI.
#include <map>
#include <string>

#include "runtime/stateful_value_ops.h"
#include "runtime/stateful_value_op_registry.h"
#include "runtime/stateful_value_ops_internal.h"  // getIn

namespace sw {

// WasTrigger TEETH hook (--selftest-wastrigger; declared in stateful_value_ops.h). 0 = production;
// 1 = DROP the _wasHit state write; 2 = DROP the var read. Sticky module switch (the golden flips it
// around the REAL cook then resets), mirrors g_animValueBug.
namespace { int g_wasTriggerBug = 0; }

namespace {

// --- WasTrigger (TiXL Lib/numbers/bool/logic/WasTrigger.cs) ---
// TiXL Update():
//   if (Math.Abs(Playback.RunTimeInSecs - _lastEvalTime) < 0.010f) return;   // once-per-frame guard
//   var triggerIndex = Trigger.GetEnumValue<Triggers>(context);  _lastEvalTime = Playback.RunTimeInSecs;
//   var customVariableName = CustomVariableName.GetValue(context);
//   float value = 0;
//   switch (triggerIndex) {
//     case None:     value = 0; break;
//     case TriggerA: value = context.FloatVariables.GetValueOrDefault("__TriggerA", 0); break;
//     case TriggerB: value = context.FloatVariables.GetValueOrDefault("__TriggerB", 0); break;
//     case Custom:   value = IsNullOrEmpty(name) ? 0 : context.FloatVariables.GetValueOrDefault(name, 0); break;
//   }
//   var increased = (value > _lastValue);  _lastValue = value;
//   var triggered = MathUtils.WasTriggered(increased, ref _wasHit);  WasTriggered.Value = triggered;
// Triggers enum: None=0, TriggerA=1, TriggerB=2, Custom=3 (.t3 default Trigger=1=TriggerA).
// MathUtils.WasTriggered(cur, ref prev) = rising edge: result = cur && !prev; then prev = cur.
// State: s[0]=_lastValue, s[1]=_wasHit(0/1). Both zero-init = TiXL's (_lastValue=0, _wasHit=false) →
//   no `init` seeding needed (faithful: a first-frame value>0 is itself an increase 0→v and pulses).
// Forks (named):
//   • _lastEvalTime once-per-frame early-return DROPPED — frame_cook cooks each node exactly once per
//     frame (Damp/Spring/Trigger precedent). No _lastEvalTime stored.
//   • CustomVariableName → the rail's canonical `VariableName` string channel (frame_cook resolves
//     strInputs["VariableName"] → varName here). Inspector-label-only fork; value byte-identical.
//   • bool output WasTriggered emitted as 1.0/0.0 (Cut 32: no Bool port type).
void stepWasTrigger(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                    StatefulValueState& st, float out[3], const TransportSnapshot&,
                    ContextVarMap* vars, const std::string& varName) {
  const int triggerIndex = (int)getIn(in, "Trigger", 1.0f);  // .t3 default 1=TriggerA (enum is exact)

  float value = 0.0f;
  if (vars && g_wasTriggerBug != 2) {  // bug 2: DROP the var read (value forced to 0 → never fires)
    auto readVar = [&](const char* key) -> float {
      auto it = vars->floatVars.find(key);
      return it != vars->floatVars.end() ? it->second : 0.0f;  // GetValueOrDefault(key, 0)
    };
    switch (triggerIndex) {
      case 1: value = readVar("__TriggerA"); break;            // TriggerA
      case 2: value = readVar("__TriggerB"); break;            // TriggerB
      case 3:                                                  // Custom
        if (!varName.empty()) {
          auto it = vars->floatVars.find(varName);
          if (it != vars->floatVars.end()) value = it->second;
        }
        break;
      default: value = 0.0f; break;                            // None (0) / out-of-range
    }
  }

  const float lastValue = st.s[0];
  const bool increased = value > lastValue;
  st.s[0] = value;                          // _lastValue = value

  // MathUtils.WasTriggered(increased, ref _wasHit): rising edge of `increased`, then store current.
  const bool prevWasHit = st.s[1] > 0.5f;
  const bool triggered = increased && !prevWasHit;
  if (g_wasTriggerBug != 1)                  // bug 1: DROP the _wasHit state write (the edge gate
    st.s[1] = increased ? 1.0f : 0.0f;       //   never advances → a held-rising input re-pulses)

  out[0] = triggered ? 1.0f : 0.0f;         // WasTriggered.Value
}

}  // namespace

void setWasTriggerBug(int mode) { g_wasTriggerBug = mode; }

static const StatefulOpReg _reg_WasTrigger{"WasTrigger", stepWasTrigger};

}  // namespace sw
