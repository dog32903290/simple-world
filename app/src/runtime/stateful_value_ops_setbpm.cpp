// runtime/stateful_value_ops_setbpm — the [SetBpm] VJ operator: on a TriggerUpdate rising edge,
// hand a clamped user BpmRate to the triggered-pull BpmProvider singleton. The per-frame consumer
// (frame_cook) pulls it onto g_transport.bpm (mirroring PlaybackUtils.cs:74-78).
//
// = TiXL Operators/Lib/numbers/anim/vj/SetBpm.cs (read-only authority). SetBpm outputs a Command
// (pure side-effect, no value); in the value rail it is a "stateful" op in the COOK sense
// (evaluate==nullptr, cooked once per frame), whose cross-frame channel is the per-instance edge
// state s[0]=prevTrigger and whose product is the BpmProvider mutation — exactly the SetFloatVar
// pattern (side-effect op, out[0] echoes a golden probe; the real product is the singleton write).
//
// EDGE, NOT LEVEL (confirmed from source): SetBpm.cs:22 uses MathUtils.WasTriggered(TriggerUpdate,
// ref _triggerUpdate) — a false→true RISING edge (MathUtils.cs:531-538: returns newState only when
// it CHANGED, then stores it). Holding TriggerUpdate=true a 2nd frame does NOT re-fire. We mirror it
// with the established sw bool-edge precedent (FreezeValue/StopWatch: prev = s[i]>0.5; edge = cur &&
// !prev; store cur). prevTrigger = s[0] (0/1).
//
// runtime leaf: pure computation + ONE side-effect into the runtime BpmProvider singleton (a runtime
// sibling — runtime→runtime is legal). No hardware, no UI, no app dependency.
#include <cmath>
#include <map>
#include <string>

#include "runtime/bpm_provider.h"
#include "runtime/stateful_value_ops.h"
#include "runtime/stateful_value_op_registry.h"
#include "runtime/stateful_value_ops_internal.h"  // getIn

namespace sw {
namespace {

// --- SetBpm (TiXL Lib/numbers/anim/vj/SetBpm.cs:18-43) ---
// Inputs (TiXL InputSlot decl order MINUS the dropped Command SubGraph — no Command sub-tree in the
// value rail, NAMED FORK, same as SetFloatVar dropping its SubGraph): BpmRate(Float), TriggerUpdate
// (Bool as Float, read >0.5). State: s[0]=prevTrigger(0/1). Output: out[0] echoes the bpm the
// provider holds AFTER this cook (golden probe; SetBpm.cs has no value output — the Command does).
//
// SetBpm.cs:20-39 verbatim:
//   bpm = BpmRate.GetValue(context)                                              (cs:20)
//   wasTriggered = WasTriggered(TriggerUpdate, ref _triggerUpdate)               (cs:22, RISING edge)
//   clampedRate = bpm.Clamp(54, 240)                                             (cs:24, computed FIRST)
//   if (wasTriggered && bpm > 1):  BpmProvider.NewBpmRate = clampedRate;
//                                  BpmProvider.SetBpmTriggered = true            (cs:25, 38-39)
// NOTE the guard is on the RAW bpm (> 1, cs:25), the value WRITTEN is the CLAMPED rate (cs:38). The
// Playback==null warn leg (cs:27-30) is editor-only (no value effect) → dropped; the BpmProvider
// write (cs:38-39, the "picked up by PlaybackUtils" path) is the faithful product we keep.
void stepSetBpm(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                StatefulValueState& st, float out[3], const TransportSnapshot&, ContextVarMap*,
                const std::string&) {
  const float bpm = getIn(in, "BpmRate", 0.0f);
  const bool trigger = getIn(in, "TriggerUpdate", 0.0f) > 0.5f;

  // MathUtils.WasTriggered(TriggerUpdate, ref _triggerUpdate): rising edge, then store current.
  const bool prevTrigger = st.s[0] > 0.5f;
  const bool wasTriggered = trigger && !prevTrigger;  // false→true only
  st.s[0] = trigger ? 1.0f : 0.0f;

  // bpm.Clamp(54, 240) — TiXL clamps to [54,240] (SetBpm.cs:24). Computed unconditionally, written
  // only on the gated edge.
  float clampedRate = bpm;
  if (clampedRate < 54.0f) clampedRate = 54.0f;
  else if (clampedRate > 240.0f) clampedRate = 240.0f;

  if (wasTriggered && bpm > 1.0f) {
    BpmProvider::instance().setNewBpmRate(clampedRate);  // cs:38-39 (NewBpmRate + SetBpmTriggered)
  }

  out[0] = clampedRate;  // golden probe (Command has no value); the real product is the singleton write
}

}  // namespace

static const StatefulOpReg _reg_SetBpm{"SetBpm", stepSetBpm};

}  // namespace sw
