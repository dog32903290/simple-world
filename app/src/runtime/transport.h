// runtime/transport — the two-clock playback core (L8 拍板: 兩個時鐘，分層，永不合併).
// Zone: runtime leaf (PURE computation — the advance公式 + bars/secs conversion; no UI, no
// per-frame driver. The driver — who hands it a real deltaTime each frame — lives in
// app/frame_cook). Depends on NOTHING but <cstdint> — a true leaf.
//
// = TiXL Core/Animation/Playback.cs (locked SHA, read-only authority). The two clocks:
//   • position  = TiXL Playback.TimeInBars — the PLAYHEAD (作品位置). Scrub/pause freezes it.
//                 Automation (keyframe) sampling reads THIS (Playback.cs:18 "Time is used for
//                 everything driven by keyframes").
//   • fxTime    = TiXL Playback.FxTimeInBars — the WALL-CLOCK brother. It KEEPS RUNNING while
//                 paused (idle-motion / continued playback, Playback.cs:16) so particles/feedback
//                 don't freeze — THIS is the whole reason the two clocks never merge (L8).
//                 Stateful sims read THIS.
//
// Units: BARS native (P3 拍板 — curve keys are already bars). bars = secs * BPM / 240
// (Playback.cs:147 BarsFromSeconds: secs * Bpm / 240). The /240 is bar=4 beats, secs/beat=60/BPM,
// so secs/bar = 240/BPM.
#pragma once
#include <cstdint>

namespace sw {

// fxTime advance semantics are abstracted out of Playback.cs:Update (lines 97-145) so the
// "暫停續跑" rule is unit-testable headless (the selftest pins these EXACT branches). Idle-motion
// is ALWAYS on here (我們沒有 render-to-file 模式; the "freeze fx while rendering" branch — the
// IsRenderingToFile leg of Playback.cs:110-113 — does not exist in this app, named fork).
struct Transport {
  enum class PlayState { Stopped, Playing };

  double position = 0.0;   // playhead, BARS (= Playback.TimeInBars). scrub/pause freezes it.
  double fxTime = 0.0;     // wall clock, BARS (= Playback.FxTimeInBars). runs while paused.
  double rate = 1.0;       // playback speed (= Playback.PlaybackSpeed when playing). 1.0 = forward.
  double bpm = 120.0;      // (= Playback.Bpm). bars<->secs lives here.
  PlayState playState = PlayState::Stopped;

  // --- bars<->secs (Playback.cs:147-155), exact ---
  double barsFromSeconds(double secs) const { return secs * bpm / 240.0; }
  double secondsFromBars(double bars) const { return bars * 240.0 / bpm; }

  bool playing() const { return playState == PlayState::Playing; }

  // Advance ONE frame by a real wall-clock deltaTime (seconds — FrameScheduler hands the true
  // frame duration; never a hardcoded 1/60). Mirrors Playback.cs:Update with idle-motion ALWAYS
  // on (we have no render-to-file path):
  //   • playing (|rate| > eps): position += dtSecs*rate*BPM/240 ; fxTime = position (locked).
  //   • paused + position was just scrubbed (this frame, via scrub()): fxTime SNAPS to position
  //     (Playback.cs:121-124 timeWasManipulated -> FxTime = TimeInBars). The scrub flag is the
  //     "_previousTimeInBars differs" detection, made explicit instead of inferred.
  //   • paused + NOT scrubbed: fxTime += dtSecs*BPM/240 — the playhead is frozen but fxTime keeps
  //     running (暫停續跑, the L8 reason the clocks don't merge). Idle-motion leg, Playback.cs:126-129.
  void advance(double dtSecs);

  // Set the playhead directly (= timeline drag / numeric scrub). Marks "manipulated this frame"
  // so the NEXT advance() snaps fxTime to it (Playback.cs:121 timeWasManipulated). position is
  // clamped to >= 0 (no negative bars; TiXL has no lower clamp but our timeline starts at 0).
  void scrub(double bars);

  // Set the playback speed (= Playback.PlaybackSpeed). Sane gate, BPM-gate style: non-finite is
  // REFUSED (rate keeps its last value); otherwise clamped to ±16 — TiXL's UI never exceeds it
  // (TimeControls.cs:92 backwards doubling stops at -16; cs:106 forward doubling stops at <16,
  // "Bass can't play much faster anyways"). |rate| <= 0.001 still advances nothing — the
  // isPlaying eps in advance() (Playback.cs:108) treats it as paused; rate 0 IS TiXL's pause.
  // NAMED FORK: TiXL has no separate play/pause state — speed==0 is the pause. We keep
  // playState (established two-field model), so rate is a sticky knob: pause/play round-trips
  // do NOT reset a NONZERO rate to 1 (TiXL's spacebar always writes PlaybackSpeed=1,
  // TimeControls.cs:132 — resetting a knob the user deliberately set would make it useless).
  // The one TiXL branch we keep verbatim: play from a DEAD rate (|rate|<=eps) revives it to 1
  // (TimeControls.cs:130-133) — otherwise the Play button silently does nothing.
  void setRate(double r);

  void play() {
    if (rate > -0.001 && rate < 0.001) rate = 1.0;  // revive from dead rate (cs:130-133)
    playState = PlayState::Playing;
  }
  void pause() { playState = PlayState::Stopped; }
  void toggle() { playing() ? pause() : play(); }  // through play(): dead-rate revive applies

 private:
  bool scrubbedThisFrame_ = false;  // = Playback._previousTimeInBars != TimeInBars detection.
};

// Headless RED->GREEN proof of the two-clock advance (--selftest-transport):
//   ① play advances position by dt*rate*BPM/240 ; pause freezes position ; fxTime tracks both states
//   ①b a stalled frame (dt=2.0) advances position by the FULL barsFromSeconds(2.0) — the transport
//      is never dt-clamped (the 0.25 ceiling is sim-only, framecook::simDeltaFromWall; refuter-C 修2)
//   ①c rate (PlaybackSpeed): position += dt*rate*BPM/240 (rate 2 doubles, rate -1 runs backwards,
//      Playback.cs:116 signed); rate 0 while Playing = paused (cs:108 eps) with fxTime still
//      running; setRate gate (NaN refused, clamp ±16); BPM × rate orthogonal (two knobs multiply)
//   ② scrub jumps position -> fxTime snaps to it ; a later non-scrub advance does NOT rewind fxTime
//   ③ two-clock separation: paused, fxTime keeps advancing while position is frozen (粒子時間門活)
//   ④ automation 接通: an Automation-driven resident input reads its curve @ transport.position
//      (the playhead) through the resident graph — play moves position -> the value walks the curve
//   ⑤ FRAME_SCHEDULER three semantics (golden搬運 from tests/frame_scheduler_contract.test.js):
//      one context per frame (all nodes share frame.time/deltaTime), previousFrame = [null,0,1],
//      invalid clockOwner ("node") -> refused (exit-1 semantic, here a FAIL flag).
//   ⑥ BPM/CompositionSettings savev2 roundtrip byte-stable + S15 tolerance of a bad bpm value.
// injectBug breaks one expectation -> FAIL (teeth).
int runTransportSelfTest(bool injectBug);

}  // namespace sw
