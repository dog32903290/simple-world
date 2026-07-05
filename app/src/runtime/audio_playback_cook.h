// runtime/audio_playback_cook — per-frame cook for the io/audio PLAYBACK operator nodes (AudioPlayer,
// PlayAudioClip). The io-family sibling of io_node_cook / audio_reaction: these nodes have NO pure
// evaluate() (they carry per-instance edge memory across frames and their product is a side-effect on the
// audio mixer, not a pulled value), so frame_cook cooks them once per frame from Node::params + the
// per-node state, APPENDS the resulting stream commands to the runtime AudioPlaybackBus (drained to the
// platform AudioMixer by the app), and writes the node's value outputs (IsPlaying/GetLevel) onto extOut.
//
// TiXL authority (per-node file:line at each cook fn below):
//   AudioPlayer.cs — ADSR-modulated volume + play/stop/pause edges + seek → UpdateStereoOperatorPlayback
//                    (cs:85-164); envelope via Core/Audio/AdsrCalculator frame-based Update (already ported
//                    faithfully in stateful_value_ops_anim2.cpp — reused here).
//   PlayAudioClip.cs — time-driven soundtrack scrub: targetTime (loop-modulo) + clip volume (cs:47-74).
//
// runtime leaf: pure computation, reads Node params + the mixer level from the AudioPlaybackBus feedback
// channel, no hardware / no UI / no upward dep. The bus IS the seam; the golden reads it directly.
#pragma once
#include <map>
#include <string>

namespace sw {

struct ResidentEvalGraph;   // resident_eval_graph.h

// Per-instance memory for an AudioPlayer node across frames (TiXL AudioPlayer private fields + the reused
// ADSR calculator state). One per AudioPlayer instance, keyed by resident path.
struct AudioPlayerState {
  // Edge latches (AudioPlayer.cs:118 _previousPlayTrigger / :142 _wasPaused).
  bool prevPlayTrigger = false;
  bool prevPause = false;
  std::string lastPath;     // to detect a file change (emit Ensure only on change / first cook)
  bool haveEnsured = false;
  // ADSR frame-based calculator state (mirror of AdsrCalculator via stateful_value_ops_anim2 layout):
  //   stage(0=Idle..4=Release), stageTime, totalTime, prevGate, releaseStart, lastTime, value, init.
  int   stage = 0;
  float stageTime = 0.0f;
  float totalTime = 0.0f;
  bool  adsrPrevGate = false;
  float releaseStart = 0.0f;
  float lastTime = 0.0f;
  float envValue = 0.0f;
  bool  adsrInit = false;
};

// Per-instance memory for a PlayAudioClip node (TiXL _startRunTimeInSecs anchor, cs:54-57).
struct PlayAudioClipState {
  double startRunTimeSecs = 0.0;
  bool   haveAnchor = false;
  std::string lastPath;
  bool   haveEnsured = false;
};

// Cook every AudioPlayer node once this frame. `timeSecs` = the app's LocalFxTime seconds clock (drives the
// frame-based ADSR); `mixerLevelFor`/`mixerPlayingFor` = read-backs from the platform mixer for this node's
// key (0 / false in a golden that drives no mixer). Emits Ensure/Play/Stop/Pause/Resume/Seek/SetGain/SetPan/
// SetRate commands to the AudioPlaybackBus and writes IsPlaying→extOut[<idx>], GetLevel→extOut[<idx>].
void cookAudioPlayerNodes(ResidentEvalGraph& g, float timeSecs,
                          std::map<std::string, AudioPlayerState>& state);

// Cook every PlayAudioClip node once this frame. `runTimeSecs` = TiXL Playback.RunTimeInSecs (process
// wall accumulator). Emits Ensure + Seek(targetTime) + SetGain(volume) + Play/Stop per IsPlaying. The
// observable value is the closed-form targetTime (with loop-modulo) — the golden asserts it.
void cookPlayAudioClipNodes(ResidentEvalGraph& g, double runTimeSecs,
                            std::map<std::string, PlayAudioClipState>& state);

// PlayAudioClip closed-form targetTime (PlayAudioClip.cs:61-68): the wall-anchored (or connected) time,
// with the loop-modulo when looping past the clip length. Pure — exposed so the golden asserts it directly
// (no mixer needed for the length). connectedTime = the resolved TimeInSecs when a wire drives it (nonzero
// → direct); anchoredTime = runTimeSecs - startAnchor (the unconnected branch). lengthSecs<=0 → no wrap.
double playAudioClipTargetTime(bool connected, double connectedTime, double anchoredTime,
                               bool looping, double lengthSecs);

// The single frame_cook entry point: cook every audio-playback node (AudioPlayer + PlayAudioClip). Owns
// the per-instance state maps (function-local statics keyed by resident path). The golden drives the
// per-family cook* directly (its own state map), not this wrapper.
void cookAudioPlaybackNodes(ResidentEvalGraph& g, float timeSecs, double runTimeSecs);

// ── Golden TEETH hook (--selftest-audioplayer). 0 = production; 1 = DROP the ADSR modulation (volume =
// raw Volume, ignoring the envelope → the envelope-modulated-gain assertion goes RED); 2 = DROP the
// play/stop edge (emit Play every frame regardless of trigger edge → the edge-command assertion goes RED).
// Sticky module switch; the golden restores 0. Mirrors setIoNodeBug's convention.
void setAudioPlaybackBug(int mode);
int  audioPlaybackBug();

// The frame-based ADSR envelope value for one AudioPlayer cook step — the SAME math as
// stateful_value_ops_anim2's stepAdsrEnvelope (AdsrCalculator.cs frame-based Update), exposed so the golden
// can hand-derive the expected envelope-modulated volume. Mutates `st` (advances the stage machine unless
// bug 1 is set). gate = shouldPlay; a/d/s/r already extracted; mode 0=Gate,1=Trigger; duration in Trigger.
float advanceAudioPlayerEnvelope(AudioPlayerState& st, bool gate, float timeSecs,
                                 float attack, float decay, float sustain, float release,
                                 int mode, float duration);

}  // namespace sw
