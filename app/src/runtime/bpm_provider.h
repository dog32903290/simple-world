// runtime/bpm_provider — the triggered-pull BPM hand-off singleton.
//
// = TiXL Core/IO/BpmProvider.cs (read-only authority). The ONE channel between the [SetBpm] VJ
// operator (the writer) and the per-frame transport-BPM consumer (PlaybackUtils.cs:74-78). It is a
// PROCESS-GLOBAL singleton, exactly like BpmProvider.Instance (BpmProvider.cs:12) — there is no
// per-document / per-graph BpmProvider in TiXL; the operator writes the static Instance and the
// editor's per-frame loop pulls it.
//
// TRIGGERED-PULL semantics (BpmProvider.cs:22-33) — the make-or-break:
//   tryGetNewBpmRate(out) returns true ONLY when a SetBpm edge has armed it; on that one read it
//   CLEARS the trigger (SetBpmTriggered=false) and hands back the rate. Every other frame it returns
//   false and leaves the consumer's BPM UNCHANGED. So transport.bpm is written ONLY on the edge —
//   NOT overwritten every frame. A per-frame overwrite would be the wrong port (see refuter focus).
//
// Zone: runtime leaf — pure host state, no hardware / no UI. SetBpm (a runtime stateful-value op)
// writes it; frame_cook (app) pulls it. runtime→runtime and app→runtime are both legal directions.
#pragma once

namespace sw {

// Mirror of T3.Core.IO.BpmProvider (the singleton + its two fields + the triggered-pull method).
// Not thread-safe (TiXL's isn't either — single editor thread writes & reads). Defaults match the
// C# field zero-init: SetBpmTriggered=false, NewBpmRate=0 (BpmProvider.cs:35-36).
class BpmProvider {
 public:
  // = BpmProvider.Instance (BpmProvider.cs:12). The one process-global.
  static BpmProvider& instance();

  // = SetBpm.cs:38-39 (the operator's write on a TriggerUpdate edge): NewBpmRate = rate;
  // SetBpmTriggered = true. The caller has already clamped (SetBpm.cs:24 .Clamp(54,240)); this
  // stores verbatim, exactly as the .cs assigns the already-clamped clampedRate.
  void setNewBpmRate(float rate);

  // = BpmProvider.cs:22-33 TryGetNewBpmRate. Returns false + leaves `out` = the stored NewBpmRate
  // (cs:26-27, the not-triggered branch still writes bpm=NewBpmRate then returns false) when no edge
  // is armed; returns true + the rate AND clears the trigger (cs:30-32) when one is. Clear-on-read.
  bool tryGetNewBpmRate(float& out);

  // Test seam only: reset the singleton to its zero-init state between golden cases (TiXL has no such
  // reset — the process-global persists for the app's life; the golden needs a clean slate per case).
  void resetForTest();

 private:
  BpmProvider() = default;
  bool setBpmTriggered_ = false;  // = BpmProvider.SetBpmTriggered (cs:35)
  float newBpmRate_ = 0.0f;       // = BpmProvider.NewBpmRate (cs:36)
};

}  // namespace sw
