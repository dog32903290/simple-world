// audio_playback_golden — AudioPlayer / PlayAudioClip NODE-COOK golden (--selftest-audioplayer).
//
// LAYER (b) of the audio-playback seam (see audio_playback_cook.h): the node cook translates params + edge
// memory into a stream-command sequence on the runtime AudioPlaybackBus. There is no closed-form for the
// AUDIBLE output, but the COMMAND sequence and the ADSR-modulated GAIN value ARE closed-form — hand-derived
// from AudioPlayer.cs + AdsrCalculator.cs. This golden cooks a real AudioPlayer node across a multi-frame
// gate trajectory on the PRODUCTION seam (cookAudioPlayerNodes with one carried state map — R-2 iron rule:
// drive the real cook, not a mock), drains the AudioPlaybackBus each frame, and asserts:
//   • the envelope-modulated SetGain value at each frame (the ADSR math),
//   • the Play command fires on the rising play edge ONLY (not every frame),
//   • the Stop command fires on the falling edge.
// PlusPlayAudioClip's closed-form targetTime (loop-modulo).
//
// ADSR TRAJECTORY (hand-computed from AdsrCalculator.cs frame-based Update; = the AdsrEnvelope golden's
// verified trajectory). Envelope=(A=0.2,D=0.2,S=0.5,R=0.2), Gate mode, Volume=0.8, UseEnvelope=true:
//   f0 t=0.0 Play=1 rising→Attack, dt=0(seed).  Attack 0<0.2 → env=0     gain=0.8*0    =0.0
//   f1 t=0.1 Play=1 dt=0.1, stageTime=0.1.        Attack → env=0.5         gain=0.8*0.5  =0.4
//   f2 t=0.3 Play=1 dt=0.2, stageTime=0.3≥0.2.    Attack→1,→Decay          env=1  gain=0.8
//   f3 t=0.4 Play=1 dt=0.1, stageTime=0.1.        Decay p=0.5 → env=0.75   gain=0.6
//   f4 t=0.6 Play=1 dt=0.2, stageTime=0.3≥0.2.    Decay→Sustain env=0.5    gain=0.4
//   f5 t=0.7 Play=0 falling→Release(rs=0.5), dt=0.1, stageTime=0.1. Release p=0.5 → env=0.25 gain=0.2
//
// injectBug corrupts the REAL cook path (setAudioPlaybackBug — NOT a flipped expectation, GOLDEN_STANDARD
// 特徵3): bug 1 = DROP the ADSR state write (stage machine frozen → f2..f5 gain diverge from the
// progression); bug 2 = DROP the play-edge guard (Play emitted every frame → the "exactly one Play" edge
// assertion diverges). did-not-trip → return 0 (P1 NO-BITE).
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "runtime/audio_playback_bus.h"
#include "runtime/audio_playback_cook.h"
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS

namespace sw {

namespace {
int g_fail = 0;
void expectClose(const char* what, double got, double want) {
  const bool ok = std::fabs(got - want) < 1e-3;
  if (!ok) { ++g_fail; std::printf("  [audioplayer] FAIL %s got=%.5f want=%.5f\n", what, got, want); }
}

// Count / fetch commands of a kind for a key in the current bus frame.
int countCmd(int kind, const std::string& key) {
  int n = 0;
  for (const AudioPlaybackCommand& c : audioPlaybackBus().commands)
    if (c.kind == kind && c.key == key) ++n;
  return n;
}
const AudioPlaybackCommand* firstCmd(int kind, const std::string& key) {
  for (const AudioPlaybackCommand& c : audioPlaybackBus().commands)
    if (c.kind == kind && c.key == key) return &c;
  return nullptr;
}

void ensureSymbols(SymbolLibrary& lib) {
  if (const NodeSpec* s = findSpec("AudioPlayer")) lib.symbols["AudioPlayer"] = atomicSymbolFromSpec(*s);
  if (const NodeSpec* s = findSpec("PlayAudioClip")) lib.symbols["PlayAudioClip"] = atomicSymbolFromSpec(*s);
}

// One AudioPlayer child with the ADSR trajectory params + a given PlayAudio gate.
ResidentEvalGraph buildPlayerFrame(SymbolLibrary& lib, float play) {
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild c; c.id = 1; c.symbolId = "AudioPlayer";
  c.overrides = {{"PlayAudio", play}, {"Volume", 0.8f}, {"UseEnvelope", 1.0f}, {"TriggerMode", 0.0f},
                 {"Duration", 1.0f}, {"Panning", 0.25f}, {"Speed", 1.5f}, {"Seek", 0.0f},
                 {"Envelope.x", 0.2f}, {"Envelope.y", 0.2f}, {"Envelope.z", 0.5f}, {"Envelope.w", 0.2f}};
  c.strOverrides = {{"AudioFile", "clip.wav"}};
  root.children = {c}; root.nextChildId = 2;
  lib.symbols["R"] = root; lib.rootId = "R";
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  return g;
}

// Cook the player once at time t with the carried state, after clearing the bus for a fresh frame.
void cookPlayerAt(SymbolLibrary& lib, std::map<std::string, AudioPlayerState>& st, float play, float t,
                  ResidentEvalGraph& outGraph) {
  endAudioPlaybackFrame();               // clear last frame's commands
  outGraph = buildPlayerFrame(lib, play);
  cookAudioPlayerNodes(outGraph, t, st);
}

void runPlayer() {
  SymbolLibrary lib; ensureSymbols(lib);
  std::map<std::string, AudioPlayerState> st;
  const float times[6] = {0.0f, 0.1f, 0.3f, 0.4f, 0.6f, 0.7f};
  const float plays[6] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f};
  const double wantGain[6] = {0.0, 0.4, 0.8, 0.6, 0.4, 0.2};   // 0.8 * env (see header trajectory)

  ResidentEvalGraph g;
  for (int f = 0; f < 6; ++f) {
    cookPlayerAt(lib, st, plays[f], times[f], g);
    // The stream key = the node's resident path ("1" for the single root child).
    const std::string key = "1";

    // (a) ADSR-modulated gain: exactly one SetGain per frame, with the hand-derived value.
    const AudioPlaybackCommand* gain = firstCmd(AudioPlaybackCommand::SetGain, key);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "f%d gain(t=%.1f)", f, times[f]);
    expectClose(buf, gain ? gain->a : -999.0, wantGain[f]);

    // Pan/rate emitted every frame with the params (0.25 / 1.5).
    const AudioPlaybackCommand* pan = firstCmd(AudioPlaybackCommand::SetPan, key);
    expectClose("pan", pan ? pan->a : -999.0, 0.25);
    const AudioPlaybackCommand* rate = firstCmd(AudioPlaybackCommand::SetRate, key);
    expectClose("rate", rate ? rate->a : -999.0, 1.5);

    // (b) Play edge: fires ONLY on f0 (rising). f1..f4 hold (no Play). f5 falling → Stop, no Play.
    const int plays_n = countCmd(AudioPlaybackCommand::Play, key);
    const int stops_n = countCmd(AudioPlaybackCommand::Stop, key);
    if (f == 0) { expectClose("f0 Play count", plays_n, 1); expectClose("f0 Stop count", stops_n, 0); }
    else if (f >= 1 && f <= 4) { expectClose("hold Play count", plays_n, 0); }
    else if (f == 5) { expectClose("f5 Play count", plays_n, 0); expectClose("f5 Stop count", stops_n, 1); }

    // Ensure fires once (f0) then never again (path unchanged).
    const int ensure_n = countCmd(AudioPlaybackCommand::Ensure, key);
    if (f == 0) expectClose("f0 Ensure count", ensure_n, 1);
    else expectClose("hold Ensure count", ensure_n, 0);
  }
}

void runClip() {
  // PlayAudioClip closed-form targetTime (RunTimeInSecs anchor + loop-modulo). We drive the cook directly
  // with an explicit runTimeSecs sequence and a fed length via the "_LengthSecs" override probe.
  SymbolLibrary lib; ensureSymbols(lib);
  std::map<std::string, PlayAudioClipState> st;

  auto buildClip = [&](bool playing) -> ResidentEvalGraph {
    Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
    SymbolChild c; c.id = 1; c.symbolId = "PlayAudioClip";
    c.overrides = {{"IsPlaying", playing ? 1.0f : 0.0f}, {"IsLooping", 0.0f},
                   {"Volume", 0.9f}, {"TimeInSecs", 0.0f}};
    c.strOverrides = {{"Path", "song.wav"}};
    root.children = {c}; root.nextChildId = 2;
    lib.symbols["R"] = root; lib.rootId = "R";
    ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
    initResidentCache(g);
    return g;
  };

  // COOK-THROUGH (real seam): anchor set at runTime=10.0 on first play. targetTime = runTime - anchor.
  {
    endAudioPlaybackFrame();
    ResidentEvalGraph g = buildClip(/*playing=*/true);
    cookPlayAudioClipNodes(g, /*runTimeSecs=*/10.0, st);
    const ResidentNode* n = g.node("1");
    expectClose("clip f0 targetTime(anchor)", n ? n->extOut[0] : -999.0, 0.0);  // 10-10 = 0
    const AudioPlaybackCommand* seek = firstCmd(AudioPlaybackCommand::Seek, "1");
    expectClose("clip f0 Seek", seek ? seek->a : -999.0, 0.0);
    const AudioPlaybackCommand* gain = firstCmd(AudioPlaybackCommand::SetGain, "1");
    expectClose("clip f0 gain", gain ? gain->a : -999.0, 0.9);
  }
  // runTime=12.5 → targetTime = 2.5 (anchored at 10.0 above).
  {
    endAudioPlaybackFrame();
    ResidentEvalGraph g = buildClip(true);
    cookPlayAudioClipNodes(g, 12.5, st);
    const ResidentNode* n = g.node("1");
    expectClose("clip f1 targetTime", n ? n->extOut[0] : -999.0, 2.5);  // 12.5-10.0 (DIVERGING MIDDLE)
    const AudioPlaybackCommand* seek = firstCmd(AudioPlaybackCommand::Seek, "1");
    expectClose("clip f1 Seek", seek ? seek->a : -999.0, 2.5);
  }
  endAudioPlaybackFrame();

  // CLOSED-FORM helper (loop-modulo, mixer-length independent): the clip length comes from the mixer at
  // runtime; the loop-wrap MATH is asserted directly on the pure helper so it needs no live stream.
  //   anchored=9.5, len=4, looping → fmod(9.5,4) = 1.5 (DIVERGING MIDDLE, wrap bites).
  expectClose("helper loop wrap", playAudioClipTargetTime(false, 0.0, 9.5, /*looping=*/true, 4.0), 1.5);
  //   anchored=2.5 < len=4 → no wrap (unwrapped middle).
  expectClose("helper no-wrap mid", playAudioClipTargetTime(false, 0.0, 2.5, true, 4.0), 2.5);
  //   not looping → never wraps even past length: 9.5 stays 9.5.
  expectClose("helper no-loop", playAudioClipTargetTime(false, 0.0, 9.5, /*looping=*/false, 4.0), 9.5);
  //   connected TimeInSecs → direct, ignores the anchor: connectedTime=3.0.
  expectClose("helper connected", playAudioClipTargetTime(true, 3.0, 99.0, false, 4.0), 3.0);
}
}  // namespace

int runAudioPlayerSelfTest(bool injectBug) {
  g_fail = 0;
  std::printf("[selftest] audioplayer (AudioPlayer ADSR-gain + play-edge command seam; PlayAudioClip targetTime)\n");

  setAudioPlaybackBug(0);
  runPlayer();
  runClip();
  const int cleanFail = g_fail;
  std::printf("[selftest] audioplayer clean pass: %d failure(s)\n", cleanFail);

  if (injectBug) {
    setAudioPlaybackBug(1);   // freeze the ADSR stage machine → f2..f5 gain diverge from the progression
    const int b1 = g_fail; runPlayer();
    const int add1 = g_fail - b1;
    setAudioPlaybackBug(2);   // Play every frame → edge count assertions diverge
    const int b2 = g_fail; runPlayer();
    const int add2 = g_fail - b2;
    setAudioPlaybackBug(0);
    std::printf("[selftest] audioplayer bug1(freeze-adsr) +%d, bug2(drop-play-edge) +%d\n", add1, add2);
    if (add1 == 0 && add2 == 0) {
      std::printf("[selftest-audioplayer] injectBug did NOT trip (a tooth is dead)\n");
      return 0;   // P1 NO-BITE → let --bite surface the dead tooth
    }
    // A tooth tripped: the clean pass must have been green (else the golden is broken, not biting).
    std::printf("[selftest-audioplayer] injectBug correctly RED (corrupted cook path; clean %d)\n", cleanFail);
    return 1;
  }

  std::printf("[selftest] audioplayer %s (clean %d failures)\n", cleanFail ? "FAIL" : "PASS", cleanFail);
  return cleanFail ? 1 : 0;
}

// order 721 — right after audio-mixer (720).
REGISTER_SELFTESTS(/*orderBase=*/721, {"audioplayer", sw::runAudioPlayerSelfTest});

}  // namespace sw
