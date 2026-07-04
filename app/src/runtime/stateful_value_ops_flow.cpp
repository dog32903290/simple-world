// runtime/stateful_value_ops_flow — the flow-family stateful value ops: Once (the trigger latch).
// New leaf per the stateful self-registration seam (glob stateful_value_op*.cpp, no shared edit).
//
// --- Once (TiXL Lib/flow/Once.cs) — the ONE-SHOT trigger edge: OutputTrigger fires true for exactly
// one cook whenever the Trigger input is (re)set, then falls back to false. TiXL Update (Once.cs:14-35)
// ported faithfully:
//   if (|LocalFxTime - _lastUpdateTime| < 0.0001) return;         // same-frame dedup guard (:16-19)
//   _lastUpdateTime = LocalFxTime;
//   OutputTrigger.Value = Trigger.DirtyFlag.IsDirty;              // :23-24 (the latch read)
//   OutputTrigger.DirtyFlag.Trigger = dirty ? Always : None;      // :26-33 (editor dirty plumbing)
//   Trigger.DirtyFlag.Clear();                                    // :34 (the self-clearing latch)
//
// Output (single): OutputTrigger = Slot<bool> → Float 0/1 (Cut 32 bool dissolve).
// Input: Trigger = InputSlot<bool>, .cs field default true BUT ★Once.t3 DefaultValue=false (re-read &
//   confirmed — the .t3 authored default wins, the sw convention).
//
// State (the cross-frame latch): s[0] = the PRIOR cook's Trigger value; s[1] = primed flag (0 on the
//   very first cook — a fresh TiXL instance's input DirtyFlag starts DIRTY, so frame 1 fires TRUE;
//   the primed flag reproduces that initial-dirty).
//
// FORKS (named):
//   ★fork-once-dirtyflag-as-value-edge (the ExecuteOnce family fork): TiXL gates on
//     Trigger.DirtyFlag.IsDirty — a self-clearing invalidation latch that fires on ANY set of the
//     input (an edit to the SAME value marks dirty too, and an animated/wired Trigger marks dirty
//     every eval). sw's resolved-Float seam sees only VALUES, so the latch is modeled as a
//     VALUE-CHANGE edge: fire on the first cook (initial-dirty) and whenever Trigger != prior
//     Trigger (both 0→1 and 1→0 fire — faithful: any change marks the TiXL flag dirty). Divergence:
//     re-setting the SAME value / a wired always-dirty upstream fires TiXL but not sw. Same class as
//     ExecuteOnce's named DirtyFlag fork (deferred until the seam carries a dirty channel).
//   - fork-once-dedup-guard-dropped: the |LocalFxTime-_lastUpdateTime|<0.0001 guard prevents a
//     same-frame double-eval from eating the latch; frame_cook cooks each node exactly ONCE per
//     frame (Damp/Ease/AnimValue precedent) → guard unnecessary. No _lastUpdateTime stored.
//   - fork-once-output-dirty-plumbing-dropped: OutputTrigger.DirtyFlag.Trigger=Always/None (:26-33)
//     is TiXL's pull-graph invalidation bookkeeping; sw cooks stateful nodes every frame → no-op.
//
// Once TEETH hook (file-local; 0 = production, set ONLY by --selftest-once via setOnceBug):
//   1 = DROP the state write (s[0]/s[1] never advance) → the op never primes → OutputTrigger stuck
//       TRUE → every want=0 frame bites (proving the cross-frame latch write is load-bearing).
//   2 = FREEZE the edge to 0 (ignore the comparison) → every want=1 frame bites (the complementary
//       tooth — an independent defect from bug 1).
// Expected values are computed from the TiXL semantics and are INDEPENDENT of this flag.
//
// runtime leaf: pure computation, no hardware, no UI.
#include <map>
#include <string>

#include "runtime/graph.h"             // NodeSpec, PortSpec, Widget (the Once spec row)
#include "runtime/math_op_registry.h"  // MathOp (NodeSpec self-registration sink)
#include "runtime/stateful_value_op_registry.h"
#include "runtime/stateful_value_ops.h"
#include "runtime/stateful_value_ops_internal.h"  // getIn

namespace sw {
namespace {

int g_onceBug = 0;

void stepOnce(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
              StatefulValueState& st, float out[8], const TransportSnapshot&, ContextVarMap*,
              const std::string&) {
  const float trigger = getIn(in, "Trigger", 0.0f);  // .t3 DefaultValue=false
  const bool primed = st.s[1] != 0.0f;
  const bool fired = !primed || (trigger != st.s[0]);  // initial-dirty OR value-change edge
  if (g_onceBug != 1) {  // bug 1: DROP the state write (real defect — the latch never primes)
    st.s[0] = trigger;
    st.s[1] = 1.0f;
  }
  // bug 2: FREEZE the edge low (real defect — the comparison is ignored).
  out[0] = (g_onceBug == 2) ? 0.0f : (fired ? 1.0f : 0.0f);  // OutputTrigger (bool → Float)
}

}  // namespace

// Once teeth hook setter (the global lives in the anonymous namespace above; handle for the app-side
// --selftest-once golden to flip around the REAL production cook — the setAnimValueBug pattern).
void setOnceBug(int mode) { g_onceBug = mode; }

static const StatefulOpReg _reg_Once{"Once", stepOnce};

// Once NodeSpec (outputs FIRST; evaluate==nullptr — cooked by frame_cook's stateful-value seam,
// dispatched by type name; the AnimValue registration pattern). Trigger is a Widget::Bool on the
// Float rail (bool dissolve); .t3 DefaultValue=false (★ overrides the .cs field default true).
static const MathOp _reg_OnceSpec{
    {"Once", "Once",
     {{"OutputTrigger", "OutputTrigger", "Float", false},
      {"Trigger", "Trigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
     nullptr,
     "flow"}};

}  // namespace sw
