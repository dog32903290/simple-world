// app/frame_cook_selftest — AR 時鐘域 pin 牙 (--selftest-arclock, refuter-S5 盲區 3).
//
// 批次7 fixed AudioReaction to eat BARS (TiXL LocalFxTime == Playback.FxTimeInBars,
// EvaluationContext.cs:49; MinTimeBetweenHits debounces in that same domain) but nothing pinned
// the clock DOMAIN — this tooth does, through the REAL production seam
// (framecook::cookAudioReactionNodes, the exact function run() drives every frame):
//   ① hit timestamps land in BARS: after a hit, state.lastHitTime == transport.fxTime (bars),
//      and at BPM != 240 that visibly differs from the seconds value (the two domains split).
//   ② debounce scales with BPM: the same wall-clock pulse train (1.5s spacing) against
//      MinTimeBetweenHits = 1.0 yields EVERY pulse at BPM 240 (1.5s == 1.5 bars >= 1.0) but
//      only every OTHER pulse at BPM 120 (1.5s == 0.75 bars < 1.0) — halve the BPM, halve the
//      hits. Feeding seconds makes the count BPM-invariant (4 everywhere).
//   ③ dt 分流接縫 (refuter-C 修2): simDeltaFromWall clamps a stalled frame to 0.25 (sim
//      integration stability) while the SAME wall dt advances the transport unclamped — the two
//      legs visibly split on a 2s stall (the seam frame_cook::run feeds ctx.deltaTime from).
// injectBug expects the seconds-domain numbers instead -> FAIL (teeth: whoever reverts the
// seam to seconds gets bitten by both legs).
#include "app/frame_cook.h"

#include <cmath>
#include <cstdio>

#include "runtime/graph.h"               // findSpec (AudioReaction NodeSpec)
#include "runtime/graph_bridge.h"        // atomicSymbolFromSpec
#include "runtime/resident_eval_graph.h" // buildEvalGraph / initResidentCache / ResidentNode
#include "runtime/spectrum_analyzer.h"   // SpectrumSnapshot
#include "runtime/transport.h"           // Transport (the bars<->secs authority)

namespace sw::framecook {
namespace {

int g_fail = 0;
void expect(const char* what, bool ok) {
  if (!ok) { ++g_fail; printf("  [arclock] FAIL %s\n", what); }
  else printf("  [arclock] ok   %s\n", what);
}

// Root compound "R" with ONE AudioReaction child (id 1). Overrides pin the algorithm knobs:
// whole-spectrum window, Threshold 0.2, MinTimeBetweenHits 1.0 (BARS — the domain under test),
// Output = Count (extOut[2] counts accepted hits = the debounce observable).
SymbolLibrary makeArLib() {
  SymbolLibrary lib;
  const NodeSpec* spec = findSpec("AudioReaction");
  if (spec) lib.symbols["AudioReaction"] = atomicSymbolFromSpec(*spec);
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild c; c.id = 1; c.symbolId = "AudioReaction";
  c.overrides["InputBand"] = 2.0f;       // FrequencyBands
  c.overrides["WindowCenter"] = 0.5f;
  c.overrides["WindowWidth"] = 1.0f;
  c.overrides["WindowEdge"] = 1.0f;
  c.overrides["Threshold"] = 0.2f;
  c.overrides["MinTimeBetweenHits"] = 1.0f;  // 1.0 in the AR clock domain (bars when correct)
  c.overrides["Output"] = 2.0f;          // Count
  c.overrides["Amplitude"] = 1.0f;
  c.overrides["Bias"] = 1.0f;
  root.children = {c};
  root.nextChildId = 2;
  lib.symbols["R"] = root;
  lib.rootId = "R";
  return lib;
}

// Drive the REAL production seam with a pulse train: 8 frames of dt=0.75s, spectrum LOUD on
// even frames, silent on odd -> pulses every 1.5 wall-seconds (loud frames re-rise each time).
// Returns the accepted-hit count off the node's extOut; optionally reports the first hit's
// stamped time + the transport state at that moment (the domain probe for leg ①).
struct PulseRun { int hits = 0; double firstHitStamp = 0.0; double fxAtFirstHit = 0.0; double bpm = 0.0; };
PulseRun runPulseTrain(double bpm) {
  PulseRun r; r.bpm = bpm;
  SymbolLibrary lib = makeArLib();
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  Transport t; t.bpm = bpm; t.rate = 1.0; t.play();
  std::map<std::string, AudioReactionState> state;
  SpectrumSnapshot loud;  for (auto& b : loud.bands) b = 1.0f;
  SpectrumSnapshot quiet; // default all-zero
  bool gotFirst = false;
  for (uint32_t k = 0; k < 8; ++k) {
    t.advance(0.75);  // the same wall clock at every BPM — only the bars conversion differs
    cookAudioReactionNodes(g, (k % 2 == 0) ? loud : quiet, t, k, &lib, state);
    if (!gotFirst && state["1"].hitCount > 0) {
      gotFirst = true;
      r.firstHitStamp = state["1"].lastHitTime;
      r.fxAtFirstHit = t.fxTime;
    }
  }
  const ResidentNode* n = g.node("1");
  r.hits = n ? (int)n->extOut[2] : -1;
  return r;
}

}  // namespace

int runArClockSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] arclock (AR clock-domain pin: AudioReaction eats BARS, refuter-S5 盲區 3)\n");

  // ===== leg ①: the hit timestamp is stamped in BARS (== transport.fxTime), not seconds. =====
  {
    PulseRun r = runPulseTrain(120.0);  // BPM != 240 so bars and seconds visibly split
    Transport conv; conv.bpm = 120.0;
    const double stampAsSecs = conv.secondsFromBars(r.firstHitStamp);
    // First loud frame: fxTime = 0.75s * 120/240 = 0.375 bars (0.75 in seconds-domain).
    double wantStamp = r.fxAtFirstHit;                       // bars-domain truth
    if (injectBug) wantStamp = conv.secondsFromBars(r.fxAtFirstHit);  // seconds-domain expectation
    expect("hit timestamp == transport.fxTime (BARS) at BPM 120",
           std::abs(r.firstHitStamp - wantStamp) < 1e-9);
    expect("bars and seconds domains actually split at BPM 120 (probe is live)",
           std::abs(r.firstHitStamp - stampAsSecs) > 0.1);
  }

  // ===== leg ②: debounce scales ∝ BPM — same wall pulse train, half BPM -> half the hits. =====
  {
    PulseRun fast = runPulseTrain(240.0);  // 1.5s spacing = 1.5 bars >= 1.0 -> all 4 pulses hit
    PulseRun slow = runPulseTrain(120.0);  // 1.5s spacing = 0.75 bars < 1.0 -> every other pulse
    expect("BPM 240: every pulse accepted (1.5 bars >= MinTimeBetweenHits 1.0)", fast.hits == 4);
    int wantSlow = injectBug ? 4 : 2;  // 4 == the seconds-domain (BPM-invariant) count
    char msg[160];
    snprintf(msg, sizeof msg,
             "BPM 120: debounce window doubled in wall time -> %d of 4 pulses (got %d)",
             wantSlow, slow.hits);
    expect(msg, slow.hits == wantSlow);
    expect("halving BPM halves the accepted hits (proportional scaling, bars domain)",
           injectBug ? (slow.hits * 2 == fast.hits) == false : slow.hits * 2 == fast.hits);
  }

  // ===== leg ③: dt 分流接縫 — sim dt clamps at 0.25s, the transport eats the raw wall dt. =====
  {
    expect("normal frame passes through the sim leg untouched",
           std::abs(simDeltaFromWall(1.0 / 60.0) - 1.0 / 60.0) < 1e-12);
    const double simDt = simDeltaFromWall(2.0);  // a 2s stall (debugger / window drag)
    const double wantSim = injectBug ? 2.0 : 0.25;  // bug = no ceiling -> sim explodes
    expect("2s stall clamps to 0.25 on the SIM leg only", std::abs(simDt - wantSim) < 1e-12);
    Transport t; t.bpm = 240.0; t.rate = 1.0; t.play();
    t.advance(2.0);  // the SAME stall on the transport leg
    expect("the SAME stall advances the transport UNclamped (2s @ 240 BPM = 2 bars)",
           std::abs(t.position - 2.0) < 1e-9);
    expect("the two legs actually split on a stall (seam is live)", simDt < t.secondsFromBars(t.position));
  }

  printf("[selftest] arclock %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw::framecook
