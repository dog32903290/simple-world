// runtime/playback_provider — the transport-WRITE hand-off singleton for the [SetPlaybackTime] /
// [SetPlaybackSpeed] anim operators (the position/speed twin of bpm_provider.h's BpmProvider).
//
// = TiXL Operators/Lib/numbers/anim/time/SetPlaybackTime.cs (writes Playback.Current.TimeInBars) +
//   SetPlaybackSpeed.cs (writes Playback.Current.PlaybackSpeed). In TiXL each op's Update writes the
//   process-global Playback.Current DIRECTLY (Playback.Current.TimeInBars = newTime, cs:54;
//   Playback.Current.PlaybackSpeed = speedFactor, cs:43). simple_world's stateful value ops receive
//   the transport only as a READ-ONLY TransportSnapshot (stateful_value_ops.h:54, const&) — they
//   CANNOT touch the mutable g_transport from inside cookStatefulValueOp. So this singleton is the ONE
//   write channel between the op (the writer, a runtime stateful-value op) and frame_cook (the puller,
//   which owns g_transport): exactly the bridge BpmProvider is for [SetBpm].
//
// NAMED FORK fork-playbackwrite-via-provider: TiXL writes Playback.Current inline in the op's Update;
//   sw routes the write through this provider + a per-frame frame_cook pull (the same indirection
//   BpmProvider takes, for the same reason — runtime→app cannot reach down into the app-owned
//   transport from a runtime leaf). The observable result is identical: a write that would land on
//   Playback.Current.TimeInBars/PlaybackSpeed this frame lands on g_transport.position/rate instead.
//
// PER-FRAME MAILBOX semantics (NOT BpmProvider's cross-frame clear-on-read): SetPlaybackTime/Speed
//   write EVERY frame their gate is open (Continuously mode / triggered==true), not just on a rising
//   edge — TiXL re-assigns Playback.Current every such Update. So this is a single-frame "pending
//   write" box: the op arms the channel during the stateful cook; the pull (run AFTER the stateful
//   cook, same frame) applies it to g_transport and clears the armed flag. An un-armed frame leaves
//   the transport UNTOUCHED (the make-or-break: a frame with no write must NOT clobber the playhead /
//   speed the user is otherwise driving). The two channels (time, speed) are independent.
//
// Zone: runtime leaf — pure host state, no hardware / no UI. SetPlaybackTime/Speed (runtime stateful-
// value ops) write it; frame_cook (app) pulls it. runtime→runtime and app→runtime are both legal.
#pragma once

namespace sw {

class PlaybackProvider {
 public:
  // The one process-global (mirror of BpmProvider::instance()).
  static PlaybackProvider& instance();

  // --- TIME channel (= SetPlaybackTime.cs:54 Playback.Current.TimeInBars = newTime). The op arms it
  // with the new playhead position in BARS whenever its gate fires this frame. Last write wins within
  // a frame (TiXL re-assigns; if two SetPlaybackTime ops fire, the later-cooked one is the live one).
  void setNewTime(float bars);

  // --- SPEED channel (= SetPlaybackSpeed.cs:43 Playback.Current.PlaybackSpeed = speedFactor). Armed
  // with the (already snap-adjusted) speed factor whenever the op's triggered gate is true this frame.
  void setNewSpeed(float speed);

  // Pull the TIME write (frame_cook, once per frame, AFTER the stateful cook). Returns true + writes
  // `out` = the armed bars and CLEARS the armed flag when a write is pending; returns false + leaves
  // the transport untouched otherwise. Clear-each-frame (a fresh arm is required next frame).
  bool tryGetNewTime(float& out);

  // Pull the SPEED write (same contract as tryGetNewTime, the speed channel).
  bool tryGetNewSpeed(float& out);

  // Test seam only: clear both channels between golden cases (the process-global persists for the
  // app's life otherwise — same reset BpmProvider exposes).
  void resetForTest();

 private:
  PlaybackProvider() = default;
  bool timeArmed_ = false;
  float newTime_ = 0.0f;
  bool speedArmed_ = false;
  float newSpeed_ = 1.0f;
};

}  // namespace sw
