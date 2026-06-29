// runtime/stateful_value_ops_setplayback — the [SetPlaybackTime] / [SetPlaybackSpeed] anim operators:
// transport-WRITE ops that arm the PlaybackProvider mailbox; frame_cook pulls it onto g_transport
// (mirroring the [SetBpm] → BpmProvider → frame_cook chain). Same "stateful in the COOK sense"
// shape as SetBpm: evaluate==nullptr, cooked once per frame, cross-frame channel = per-instance edge
// state, product = a provider mutation (out[0] echoes a golden probe).
//
// = TiXL Operators/Lib/numbers/anim/time/SetPlaybackTime.cs + SetPlaybackSpeed.cs (read-only authority).
//
// runtime leaf: pure computation + ONE side-effect into the runtime PlaybackProvider singleton (a
// runtime sibling — runtime→runtime is legal). No hardware, no UI, no app dependency.
#include <cmath>
#include <map>
#include <string>

#include "runtime/playback_provider.h"
#include "runtime/stateful_value_ops.h"
#include "runtime/stateful_value_op_registry.h"
#include "runtime/stateful_value_ops_internal.h"  // getIn

namespace sw {
namespace {

// --- SetPlaybackTime (TiXL Lib/numbers/anim/time/SetPlaybackTime.cs:24-58) ---
// Inputs (decl order MINUS the dropped Command SubGraph — no Command sub-tree in the value rail,
// NAMED FORK, same as SetBpm/SetFloatVar): TimeInBars(Float), TriggerMode(int enum
// 0=OnceEnabledGetsTrue / 1=Continuously), Enabled(Bool as Float), ShowLogMessages(Bool, telemetry-
// only → DROPPED, no value effect, same as SetBpm's Playback==null warn leg). State: s[0]=prevEnabled.
// Output: out[0] echoes the bars this cook would write (golden probe; the Command carries no value).
//
// SetPlaybackTime.cs:28-56 verbatim:
//   newTime = TimeInBars.GetValue(context)                                           (cs:28)
//   mode = TriggerMode.GetEnumValue<Modes>(context)                                  (cs:29)
//   enabled = Enabled.GetValue(context)                                              (cs:31)
//   wasTriggered = MathUtils.WasTriggered(enabled, ref _wasEnabled)                  (cs:32, RISING edge)
//   if (IsNaN||IsInfinity(newTime)) newTime = 0                                       (cs:34-37)
//   if (wasTriggered || (enabled && mode==Continuously)):                            (cs:39)
//       Playback.Current.TimeInBars = newTime                                         (cs:54)
// The Playback==null warn leg (cs:41-45) is editor-only (no value effect) → dropped; the
// Playback.Current.TimeInBars write (cs:54) is the faithful product we route through the provider.
void stepSetPlaybackTime(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                         StatefulValueState& st, float out[8], const TransportSnapshot&, ContextVarMap*,
                         const std::string&) {
  float newTime = getIn(in, "TimeInBars", 0.0f);
  const int mode = (int)std::lround(getIn(in, "TriggerMode", 0.0f));  // 0=OnceEnabledGetsTrue (.t3)
  const bool enabled = getIn(in, "Enabled", 0.0f) > 0.5f;

  // MathUtils.WasTriggered(enabled, ref _wasEnabled): rising edge, then store current (SetBpm precedent).
  const bool prevEnabled = st.s[0] > 0.5f;
  const bool wasTriggered = enabled && !prevEnabled;  // false→true only
  st.s[0] = enabled ? 1.0f : 0.0f;

  if (std::isnan(newTime) || std::isinf(newTime)) newTime = 0.0f;  // cs:34-37

  // cs:39: fire on the edge (OnceEnabledGetsTrue) OR every frame while enabled (Continuously, mode==1).
  const bool fire = wasTriggered || (enabled && mode == 1);
  if (fire) {
    PlaybackProvider::instance().setNewTime(newTime);  // cs:54 Playback.Current.TimeInBars = newTime
  }

  out[0] = newTime;  // golden probe (the Command has no value); the real product is the provider arm
}

// --- SetPlaybackSpeed (TiXL Lib/numbers/anim/time/SetPlaybackSpeed.cs:24-47) ---
// Inputs (decl order MINUS the dropped Command SubGraph): SpeedFactor(Float), TriggerUpdate(Bool as
// Float). State: NONE used (TiXL's _triggerUpdate WasTriggered is COMMENTED OUT — cs:24 — so this is
// LEVEL, not edge: it writes every frame `triggered` is true). We keep st unused.
// Output: out[0] echoes the snap-adjusted speed this cook would write.
//
// SetPlaybackSpeed.cs:26-43 verbatim:
//   speedFactor = SpeedFactor.GetValue(context)                                      (cs:26)
//   triggered = TriggerUpdate.GetValue(context)                                      (cs:27)   ← LEVEL
//   if (triggered):                                                                  (cs:31)
//       if (speedFactor > 0.95 && < 1.05): speedFactor = 1                            (cs:39-42, snap-to-1)
//       else if (speedFactor > 0 && < 0.03): speedFactor = 0.0001                     (cs:43-46, near-stop)
//       Playback.Current.PlaybackSpeed = speedFactor                                  (cs:48)
// The Playback==null warn leg (cs:33-37) is editor-only → dropped. Note WasTriggered is COMMENTED OUT
// in the .cs (cs:24 + cs:50) — TiXL ships SetPlaybackSpeed as LEVEL-gated (writes every triggered
// frame), NOT edge-gated; we mirror the SHIPPED behaviour (a held-true trigger keeps writing).
void stepSetPlaybackSpeed(const std::map<std::string, float>& in, float /*dt*/, float /*time*/,
                          StatefulValueState&, float out[8], const TransportSnapshot&, ContextVarMap*,
                          const std::string&) {
  float speedFactor = getIn(in, "SpeedFactor", 1.0f);
  const bool triggered = getIn(in, "TriggerUpdate", 0.0f) > 0.5f;  // LEVEL (WasTriggered commented out)

  if (triggered) {
    // cs:39-46 snap: near-1 → exactly 1 ; small-positive → 0.0001 (not quite stopping playback).
    if (speedFactor > 0.95f && speedFactor < 1.05f) speedFactor = 1.0f;
    else if (speedFactor > 0.0f && speedFactor < 0.03f) speedFactor = 0.0001f;
    PlaybackProvider::instance().setNewSpeed(speedFactor);  // cs:48 Playback.Current.PlaybackSpeed
  }

  out[0] = speedFactor;  // golden probe (snap-adjusted); the real product is the provider arm
}

}  // namespace

static const StatefulOpReg _reg_SetPlaybackTime{"SetPlaybackTime", stepSetPlaybackTime};
static const StatefulOpReg _reg_SetPlaybackSpeed{"SetPlaybackSpeed", stepSetPlaybackSpeed};

}  // namespace sw
