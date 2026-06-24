// bpm_transport_golden — --selftest-bpmtransport. The END-TO-END "auto BPM drives transport" golden:
// the FULL live-performance wire DetectBpm(auto-detected) → SetBpm(edge) → BpmProvider(armed) →
// pullSetBpmRate → composition.bpm. Closes fork-bpm-not-live-driving-transport: proves the detected
// BPM actually REACHES transport.bpm (not just that each leg works in isolation).
//
// = TiXL faithful auto path. In TiXL the DetectBpm operator NEVER auto-overwrites the transport on its
// own (DetectBpm.cs only writes DetectedBpm.Value, a Slot<float>); the detected rate reaches the
// transport ONLY when the user wires DetectBpm.DetectedBpm → SetBpm.BpmRate and fires SetBpm's
// TriggerUpdate, which arms BpmProvider (SetBpm.cs:38-39), pulled by PlaybackUtils.cs:74-78 onto
// settings.Playback.Bpm. There is NO implicit per-frame detect→transport overwrite anywhere in the
// editor (grep Playback.Bpm = : only manual UI / BpmProvider pull / BeatTiming sync). This golden
// drives the EXACT production cookers (cookDetectBpmNodes + cookStatefulValueNodes + pullSetBpmRate)
// over a TWO-NODE resident graph (DetectBpm → SetBpm wired by Connection) so the resident resolver
// walking DetectBpm.extOut[0] into SetBpm.BpmRate is machine-verified, headlessly, no 柏為 in the loop.
//
// The teeth (-bug): there is no production bug hook in this faithful chain, so the -bug is injected at
// the EXPECTATION level — flip the expected transport.bpm to the WRONG-port answer (the unwired
// default, i.e. "detection never reached transport"). A green = the detected rate really arrived at
// comp.bpm (±1, the engine's faithful recovery tolerance); -bug RED = the unwired answer is wrong.
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "app/cook_host_values.h"        // framecook::pullSetBpmRate
#include "app/frame_cook.h"              // framecook::cookStatefulValueNodes
#include "runtime/bpm_provider.h"         // BpmProvider (resetForTest)
#include "runtime/compound_graph.h"       // CompositionSettings
#include "runtime/detect_bpm.h"           // cookDetectBpmNodes, DetectBpm
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / ResidentNode / ResidentInput
#include "runtime/stateful_value_ops.h"   // StatefulValueState, ContextVarMap
#include "runtime/transport.h"            // Transport

namespace sw {
namespace {

// Period (frames) of a beat at `bpm` — mirrors detect_bpm.cpp's beatPeriodFrames so the planted spike
// lands exactly on the autocorrelation lag the operator scans (240/bpm*60/4).
int beatPeriodFrames(float bpm) { return (int)std::lround(240.0f / bpm * 60.0f / 4.0f); }

// FFT frame: `height` band-sum packed into the integer border band [lower, upper), ~0 elsewhere.
void makeFftFrame(std::vector<float>& fft, int bins, int lower, int upper, float height) {
  fft.assign(bins, 0.0f);
  const int span = upper - lower;
  if (span <= 0) return;
  const float per = height / (float)span;
  for (int i = lower; i < upper; i++) fft[i] = per;
}

// Two-node resident graph: DetectBpm (path "1") → SetBpm (path "2"). SetBpm.BpmRate is a Connection
// driven by DetectBpm.DetectedBpm (the production wire). SetBpm.TriggerUpdate is a Constant the golden
// flips frame-to-frame (the edge). LowestBpm/HighestBpm on DetectBpm bracket the planted target (a
// TEST-INPUT choice, not a fork — the operator reads them as params; same as the detectbpm golden).
ResidentEvalGraph makeChainGraph(int lower, int upper, int lowestBpm, int highestBpm, float trigger) {
  ResidentEvalGraph g;

  // --- node "1": DetectBpm ---
  ResidentNode det;
  det.path = "1";
  det.opType = "DetectBpm";
  auto addConst = [](ResidentNode& rn, const char* slot, float v) {
    ResidentInput ri;
    ri.slotId = slot;
    ri.driver = ResidentInput::Driver::Constant;
    ri.constant = v;
    rn.inputs.push_back(ri);
  };
  addConst(det, "LowerLimit", (float)lower);
  addConst(det, "UpperLimit", (float)upper);
  addConst(det, "BufferDurationSec", 15.0f);  // .t3 default
  addConst(det, "LowestBpm", (float)lowestBpm);
  addConst(det, "HighestBpm", (float)highestBpm);
  addConst(det, "LockItFactor", 0.0f);  // .t3 default
  g.nodes.push_back(det);

  // --- node "2": SetBpm, BpmRate ← Connection(DetectBpm.DetectedBpm) ---
  ResidentNode set;
  set.path = "2";
  set.opType = "SetBpm";
  {
    ResidentInput ri;            // BpmRate ← the live detected BPM (the wire under test)
    ri.slotId = "BpmRate";
    ri.driver = ResidentInput::Driver::Connection;
    ri.srcNodePath = "1";
    ri.srcSlotId = "DetectedBpm";
    set.inputs.push_back(ri);
  }
  addConst(set, "TriggerUpdate", trigger);  // the rising edge the golden arms on the final frame
  g.nodes.push_back(set);

  g.byPath["1"] = 0;
  g.byPath["2"] = 1;
  return g;
}

// Drive the FULL chain for `frames` frames with a planted-BPM spike train, fire the SetBpm edge on the
// LAST frame, pull onto comp.bpm. Returns the recovered detected BPM (extOut[0] of DetectBpm) via
// `detectedOut`, and the transport bpm AFTER the pull as the function result.
double driveChain(float bpmTarget, int lower, int upper, int lowestBpm, int highestBpm, int frames,
                  float& detectedOut) {
  const int bins = 1024;
  std::vector<float> spike, quiet;
  makeFftFrame(spike, bins, lower, upper, 1.0f);
  makeFftFrame(quiet, bins, lower, upper, 0.0f);
  const int period = beatPeriodFrames(bpmTarget);

  BpmProvider::instance().resetForTest();
  CompositionSettings comp;  // bpm defaults 120
  Transport t;               // bpm 120 (unused by the chain — SetBpm reads BpmRate, not transport.bpm)

  std::map<std::string, DetectBpm> detState;
  std::map<std::string, StatefulValueState> svState;
  ContextVarMap vars;

  for (int f = 0; f < frames; f++) {
    // TriggerUpdate is LOW for every frame except the last → a single false→true rising edge on the
    // final frame (= the user hitting "set BPM" once the detector has locked). Holding it low first
    // means the edge is genuine (SetBpm is edge-not-level).
    const float trig = (f == frames - 1) ? 1.0f : 0.0f;
    ResidentEvalGraph g = makeChainGraph(lower, upper, lowestBpm, highestBpm, trig);

    // Cook order MIRRORS frame_cook.cpp: DetectBpm (writes extOut[0]) BEFORE the stateful pass (SetBpm
    // reads it via the Connection resolver). A fresh `g` each frame is fine — extOut is recomputed and
    // the cross-frame memory lives in detState/svState (keyed by path), exactly like production.
    const std::vector<float>& frame = ((f % period) == 0) ? spike : quiet;
    cookDetectBpmNodes(g, frame.data(), bins, detState);
    framecook::cookStatefulValueNodes(g, 1.0f / 60.0f, 0.0f, 0.0, t, (uint32_t)f, nullptr, svState,
                                      vars);

    detectedOut = g.nodes[0].extOut[0];

    // The per-frame consumer (PlaybackUtils.cs:74-78): pull whatever SetBpm armed onto comp.bpm. Only
    // the armed (final) frame writes; every other frame is the untouched false leg.
    framecook::pullSetBpmRate(comp);
  }

  BpmProvider::instance().resetForTest();  // leave the singleton clean (no cross-test bleed)
  return comp.bpm;
}

}  // namespace

int runBpmTransportSelfTest(bool injectBug) {
  bool ok = true;
  const int lower = 2, upper = 199;   // DetectBpm.t3 defaults (integer bin borders)
  const int loBpm = 80, hiBpm = 180;  // bracket both planted targets (test inputs)
  const int frames = 900;             // 15s buffer fully filled (detectbpm golden's frame count)

  // Two targets (120, 90) rule out a hard-coded transport write: the value that lands on comp.bpm must
  // track the PLANTED beat, proving it flowed DetectBpm → SetBpm → provider → transport.
  struct { float target; } cases[2] = {{120.0f}, {90.0f}};
  for (int i = 0; i < 2; i++) {
    float detected = -1.0f;
    const double transportBpm = driveChain(cases[i].target, lower, upper, loBpm, hiBpm, frames, detected);

    // The make-or-break: transport.bpm must equal the DETECTED value (±1) — i.e. detection reached the
    // transport. -bug claims the WRONG port: the unwired default (120), i.e. "detection never arrived".
    // (For the 120 target the unwired default coincides with the answer, so its bug-expectation is the
    // 90-target's default-stays — but the 90 case is the decisive tooth: detected 90 vs unwired 120.)
    const double wantBpm = injectBug ? 120.0 : (double)detected;
    const bool detectedTracksTarget = std::fabs(detected - cases[i].target) <= 1.0f;
    const bool transportGotDetected = std::fabs(transportBpm - wantBpm) <= 1.0f;
    const bool pass = detectedTracksTarget && transportGotDetected;
    ok = ok && pass;
    std::printf("[bpmtransport] target=%.0f detected=%.2f transport.bpm=%.2f (want %.2f) "
                "detectTracks=%d arrived=%d -> %s%s\n",
                cases[i].target, detected, transportBpm, wantBpm, detectedTracksTarget,
                transportGotDetected, pass ? "PASS" : "FAIL", injectBug ? "  (injectBug)" : "");
  }

  std::printf("[bpmtransport] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
