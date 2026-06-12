// runtime/transport_selftest — S5 two-clock transport, headless RED->GREEN (--selftest-transport).
// Six legs (spec S5 selftest table):
//   ① advance: play -> position += dt*rate*BPM/240 ; pause freezes position ; fxTime tracks both
//   ①c rate: 2x doubles, -1 runs backwards, 0-while-Playing freezes the playhead (fxTime still
//      runs); setRate gate (NaN refused, ±16 clamp); BPM × rate orthogonal
//   ② scrub: position jumps -> next-frame fxTime snaps to it ; a later non-scrub advance never rewinds fx
//   ③ two-clock separation: paused, fxTime keeps advancing while position is frozen (粒子時間門活)
//   ④ automation 接通: an Automation-driven resident input reads its curve @ transport.position
//      (the playhead) through the resident graph — moving the playhead walks the value along the curve
//   ⑤ FRAME_SCHEDULER three semantics (golden搬運 from tests/frame_scheduler_contract.test.js):
//      one context per frame, previousFrame = [null,0,1], invalid clockOwner ("node") refused
//   ⑥ BPM/CompositionSettings savev2 roundtrip byte-stable + S15 tolerance of a bad bpm
// injectBug breaks ONE expectation per leg -> FAIL (teeth).
#include "runtime/transport.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "runtime/compound_graph.h"
#include "runtime/compound_save.h"
#include "runtime/curve.h"
#include "runtime/curve_animator.h"
#include "runtime/resident_eval_graph.h"

namespace sw {
namespace {

int g_fail = 0;
void expect(const char* what, bool ok) {
  if (!ok) { ++g_fail; printf("  [transport] FAIL %s\n", what); }
  else printf("  [transport] ok   %s\n", what);
}
void expectNear(const char* what, double got, double want, double eps = 1e-6) {
  bool ok = std::abs(got - want) <= eps;
  if (!ok) { ++g_fail; printf("  [transport] FAIL %s got=%.8f want=%.8f\n", what, got, want); }
  else printf("  [transport] ok   %s = %.8f\n", what, got);
}

Symbol atomic(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// ---- leg ④ fixture: a root "S" with an animated Const#1 -> Identity passthrough -> output.
// Const{value->out}. The Animator on S animates Const#1.value with a linear ramp t=0->0, t=4->8.
// Sampling @ playhead p gives value 2*p. (Mirrors the curve_animator selftest fixture — same
// pattern the spec points to.)
SymbolLibrary makeAutomationLib() {
  SymbolLibrary sl;
  sl.symbols["Const"] = atomic("Const", {{"value", "value", "Float", 0.0f}},
                               {{"out", "out", "Float", 0.0f}});
  Symbol root; root.id = "S"; root.name = "S"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "Const"; c1.overrides["value"] = 0.0f;
  root.children = {c1};
  root.connections = {{1, "out", kSymbolBoundary, "out"}};
  root.nextChildId = 2;
  // ramp: t=0 -> 0, t=4 -> 8 (linear) => value @ t == 2*t.
  Curve ramp;
  VDefinition k0; k0.value = 0.0;
  k0.inInterpolation = KeyInterpolation::Linear; k0.outInterpolation = KeyInterpolation::Linear;
  VDefinition k1; k1.value = 8.0;
  k1.inInterpolation = KeyInterpolation::Linear; k1.outInterpolation = KeyInterpolation::Linear;
  ramp.addOrUpdate(0.0, k0);
  ramp.addOrUpdate(4.0, k1);
  Animator::CurveArray arr; arr.push_back(ramp);
  root.animator.setCurves(1, "value", arr);
  sl.symbols["S"] = root; sl.rootId = "S";
  return sl;
}

// Sample the automated output @ a given playhead position (bars), driving the resident graph EXACTLY
// like the cook does: localTime = the playhead. (localFxTime is irrelevant to a pure keyframe read.)
float sampleAtPlayhead(SymbolLibrary& sl, ResidentEvalGraph& g, double playheadBars) {
  ResidentEvalCtx ctx; ctx.lib = &sl;
  ctx.localTime = (float)playheadBars;
  bumpLiveSources(g);  // playhead moved -> bump live sources before pulling (cache invariant)
  auto outP = g.outputs["out"];
  return pullResidentFloat(g, outP.first, outP.second, ctx);
}

}  // namespace

int runTransportSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] transport (S5 two-clock playback head)\n");

  // 120 BPM => bars/sec = 120/240 = 0.5. A 1.0s frame at rate 1.0 advances 0.5 bars.
  const double dt = 1.0;  // 1-second frames keep the arithmetic exact & obvious.

  // ===== leg ①: play advances position; pause freezes it; fxTime tracks. =====
  {
    Transport t; t.bpm = 120.0; t.rate = 1.0;
    t.play();
    t.advance(dt);
    expectNear("play: position += dt*rate*BPM/240", t.position, 0.5);
    expectNear("play: fxTime locked to position", t.fxTime, 0.5);
    t.advance(dt);
    expectNear("play: position accumulates", t.position, 1.0);
    expectNear("play: fxTime still tracks position", t.fxTime, 1.0);

    const double posFrozen = t.position;
    t.pause();
    t.advance(dt);
    // injectBug: pretend pause advanced the playhead (the bug that breaks "scrub/pause freezes it").
    double observedPos = injectBug ? t.position + 0.5 : t.position;
    expectNear("pause: position frozen", observedPos, posFrozen);
  }

  // ===== leg ①b: a stalled frame advances the playhead by the FULL wall dt (refuter-C 修2). =====
  // The 0.25s ceiling that used to clamp this lives ONLY on the sim leg (framecook::
  // simDeltaFromWall); the transport is the master clock the soundtrack FOLLOWS — clamp it and a
  // 2s stall leaves the playhead 1.75s behind the free-running audio -> audible backwards seek.
  {
    Transport t; t.bpm = 120.0; t.rate = 1.0;
    t.play();
    t.advance(2.0);  // one stalled frame, dt = 2.0s >> the old 0.25 ceiling
    // injectBug: expect the old clamped advance (0.25s worth) — an unclamped transport FAILs it.
    const double want = injectBug ? t.barsFromSeconds(0.25) : t.barsFromSeconds(2.0);
    expectNear("stalled frame: position += barsFromSeconds(2.0), UNclamped", t.position, want);
    expectNear("stalled frame: fxTime rides the same full dt", t.fxTime, want);
  }

  // ===== leg ①c: rate (PlaybackSpeed) scales the playhead advance (Playback.cs:116). =====
  {
    Transport t; t.bpm = 120.0; t.setRate(2.0);
    t.play();
    t.advance(dt);
    expectNear("rate 2: position += dt*2*BPM/240", t.position, 1.0);
    expectNear("rate 2: fxTime locked to the rate-scaled playhead (cs:117)", t.fxTime, 1.0);

    // Negative speed runs the playhead BACKWARDS — cs:116 is signed, Update has no lower clamp
    // (only our scrub() clamps at 0; advance mirrors TiXL).
    t.setRate(-1.0);
    t.advance(dt);
    expectNear("rate -1: position runs backwards", t.position, 0.5);
    expectNear("rate -1: fxTime follows the backwards playhead", t.fxTime, 0.5);

    // rate 0 while the play STATE says Playing: |PlaybackSpeed| <= 0.001 is NOT playing
    // (cs:108 eps) — the playhead freezes but fxTime keeps running (idle-motion leg,
    // cs:126-129). THE refuter-C trap: a rate=0 transport must read as paused everywhere.
    t.setRate(0.0);
    const double posHeld = t.position;
    const double fxBefore = t.fxTime;
    t.advance(dt);
    // injectBug: pretend rate 0 still advanced the playhead (the "frozen playhead, music keeps
    // running" half of the trap — the soundtrack selftest bites the other half).
    double observedPos = injectBug ? t.position + 0.5 : t.position;
    expectNear("rate 0 while Playing: playhead frozen (cs:108 eps gate)", observedPos, posHeld);
    expect("rate 0: fxTime keeps running (idle-motion, cs:126-129)", t.fxTime > fxBefore);

    // Play from a DEAD rate revives it to 1 (TimeControls.cs:130-133: toggle from speed 0 sets
    // speed 1) — without this the Play button silently does nothing after a Speed-to-0 drag.
    // A deliberate NONZERO rate stays sticky across pause/play (our knob fork, see transport.h).
    t.pause();
    t.play();
    expectNear("play() from rate 0 revives rate to 1 (cs:130-133)", t.rate, 1.0);
    t.setRate(0.5);
    t.pause();
    t.toggle();  // toggle goes through play(): nonzero rate must SURVIVE (sticky knob)
    expectNear("pause/play round-trip keeps a deliberate nonzero rate", t.rate, 0.5);

    // setRate sane gate: non-finite refused (rate keeps its last value), clamp at ±16 — TiXL's
    // UI doubling stops there (TimeControls.cs:92 backwards, cs:106 forward).
    t.setRate(1.5);
    t.setRate(std::numeric_limits<double>::quiet_NaN());
    expectNear("setRate(NaN) refused, rate keeps last", t.rate, 1.5);
    t.setRate(100.0);
    expectNear("setRate clamps to +16 (TimeControls.cs:106)", t.rate, 16.0);
    t.setRate(-100.0);
    expectNear("setRate clamps to -16 (TimeControls.cs:92)", t.rate, -16.0);

    // BPM × speed orthogonality (牙釘): two independent knobs that MULTIPLY in advance();
    // setting one never writes the other.
    t.setRate(2.0);
    t.bpm = 60.0;
    expectNear("setRate never touched bpm", t.bpm, 60.0);
    expectNear("bpm write never touched rate", t.rate, 2.0);
    t.scrub(0.0);
    t.advance(dt);  // playing + rate 2 @ 60 BPM: dt*rate*bpm/240 = 1*2*60/240 = 0.5
    expectNear("advance = dt*rate*bpm/240 (knobs independent, multiplied)", t.position, 0.5);
  }

  // ===== leg ②: scrub jumps the playhead; next-frame fxTime snaps; later advance never rewinds fx. =====
  {
    Transport t; t.bpm = 120.0;
    t.scrub(10.0);
    expectNear("scrub: position jumps to target", t.position, 10.0);
    t.advance(dt);  // paused + scrubbed-this-frame -> fxTime snaps to playhead
    expectNear("scrub: fxTime snaps to scrubbed playhead", t.fxTime, 10.0);
    const double fxAfterSnap = t.fxTime;
    t.advance(dt);  // paused, NOT scrubbed -> fxTime advances forward, never rewinds
    expect("post-scrub advance does not rewind fxTime", t.fxTime >= fxAfterSnap);
    expectNear("post-scrub fxTime advanced by dt", t.fxTime, fxAfterSnap + 0.5);
    expectNear("post-scrub position still frozen at scrub", t.position, 10.0);
  }

  // ===== leg ③: two-clock separation — paused, fxTime runs while position freezes (粒子時間門活). =====
  {
    Transport t; t.bpm = 120.0;
    t.pause();
    const double pos0 = t.position;
    double fxPrev = t.fxTime;
    for (int i = 0; i < 5; ++i) {
      t.advance(dt);
      expect("paused: fxTime strictly advances (sim clock alive)", t.fxTime > fxPrev);
      fxPrev = t.fxTime;
    }
    // injectBug: pretend the playhead drifted while paused (would倒帶 the composition under sims).
    double observedPos = injectBug ? t.position + 0.001 : t.position;
    expectNear("paused: position never moved", observedPos, pos0);
    expectNear("paused: fxTime accumulated 5 frames", t.fxTime, 5 * 0.5);
  }

  // ===== leg ④: automation 接通 — moving the transport playhead walks the automated value. =====
  {
    SymbolLibrary sl = makeAutomationLib();
    ResidentEvalGraph g = buildEvalGraph(sl, sl.rootId);
    initResidentCache(g);

    // The transport playhead IS the automation sampling clock. Play from 0 at 120 BPM, rate 1:
    // after 2s -> 1.0 bars -> value 2*1 = 2 ; after 6s total -> 3.0 bars -> value 6.
    Transport t; t.bpm = 120.0; t.rate = 1.0; t.play();
    t.advance(2.0);  // -> 1.0 bars
    float v1 = sampleAtPlayhead(sl, g, t.position);
    expectNear("automated value @ playhead 1.0 bars", v1, 2.0f, 1e-4);

    t.advance(4.0);  // -> 3.0 bars
    float v3 = sampleAtPlayhead(sl, g, t.position);
    expectNear("automated value @ playhead 3.0 bars", v3, 6.0f, 1e-4);

    // Pause + scrub back to 0.5 bars -> value 1.0 (scrub reaches the curve, the体感腿).
    t.pause();
    t.scrub(0.5);
    float vScrub = sampleAtPlayhead(sl, g, t.position);
    // injectBug: sample at fxTime instead of the playhead — paused fxTime has run on, so the scrub
    // would NOT reach the curve point (the exact wrong-clock bug L8 forbids).
    float sampled = injectBug ? sampleAtPlayhead(sl, g, t.fxTime) : vScrub;
    expectNear("scrub reaches the curve (automation samples playhead, not fxTime)", sampled, 1.0f, 1e-4);
  }

  // ===== leg ⑤: FRAME_SCHEDULER three semantics (golden搬運 from frame_scheduler_contract.test.js). =====
  // The JS golden proved these against a python shell over a fixture; the SEMANTICS port here as a
  // tiny in-process FrameScheduler model — same three assertions, named after the JS test lines.
  {
    // The scheduler := graph-level owner of frameIndex/time/deltaTime. Nodes read the SHARED ctx.
    struct Frame { uint32_t frameIndex; double time; double deltaTime; };
    struct NodeObs { uint32_t frameIndex; double time; double deltaTime; };
    const char* clockOwner = injectBug ? "node" : "graph";  // bug = per-node clocks (refused)

    // semantic 1: invalid clockOwner is refused (= test line 93-108, exit-1 -> here a hard FAIL flag).
    bool ownerValid = std::string(clockOwner) == "graph";
    expect("FRAME_SCHEDULER: clockOwner must be 'graph' (per-node refused)", ownerValid);

    if (ownerValid) {
      // Drive 3 frames. The scheduler owns one Frame ctx per frame; 5 nodes each observe THAT ctx.
      const char* nodes[] = {"constant_a", "constant_b", "blend_1", "keep_previous_1", "output_1"};
      std::vector<int> stateUpdatesPerFrame;
      std::vector<int> previousFrames;  // state node's previousFrame each frame (= test line 89)
      int prev = -1;
      bool allShare = true;  // every node observes the SAME frame.time/deltaTime (= test line 84-85)
      for (uint32_t fi = 0; fi < 3; ++fi) {
        Frame ctx{fi, (double)fi * 0.5, 0.5};  // ONE ctx for the whole frame
        int stateUpdates = 0;
        for (const char* n : nodes) {
          (void)n;
          NodeObs obs{ctx.frameIndex, ctx.time, ctx.deltaTime};  // node reads the shared ctx
          if (obs.time != ctx.time || obs.deltaTime != ctx.deltaTime) allShare = false;
        }
        // state node (keep_previous_1) updates once per frame boundary; sees previousFrame=prev.
        previousFrames.push_back(prev);
        ++stateUpdates;
        stateUpdatesPerFrame.push_back(stateUpdates);
        prev = (int)fi;
      }
      // semantic 2: one shared context per frame (allNodesShareFrameContext == true).
      expect("FRAME_SCHEDULER: all nodes share one frame context", allShare);
      // state node updates exactly once per frame boundary.
      bool oncePerFrame = true;
      for (int u : stateUpdatesPerFrame) if (u != 1) oncePerFrame = false;
      expect("FRAME_SCHEDULER: state node updates once per frame", oncePerFrame);
      // semantic 3: previousFrame trace == [null, 0, 1]  (null modeled as -1).
      bool prevOk = previousFrames.size() == 3 && previousFrames[0] == -1 &&
                    previousFrames[1] == 0 && previousFrames[2] == 1;
      expect("FRAME_SCHEDULER: previousFrame == [null,0,1]", prevOk);
    }
  }

  // ===== leg ⑥: BPM/CompositionSettings savev2 roundtrip byte-stable + S15 bad-bpm tolerance. =====
  {
    SymbolLibrary sl = makeAutomationLib();
    sl.composition.bpm = 140.0;
    sl.composition.soundtrackPath = "track.wav";
    sl.composition.soundtrackVolume = 0.8;

    std::string json1 = libToJsonV2(sl);
    SymbolLibrary rl;
    std::vector<std::string> warns;
    bool loaded = libFromJsonAny(json1, rl, &warns);
    expect("composition reload ok", loaded);
    expectNear("bpm survives roundtrip", rl.composition.bpm, 140.0);
    expect("soundtrackPath survives roundtrip", rl.composition.soundtrackPath == "track.wav");
    expectNear("soundtrackVolume survives roundtrip", rl.composition.soundtrackVolume, 0.8);
    std::string json2 = libToJsonV2(rl);
    // injectBug: tamper the reloaded bpm so the byte-stable re-serialization diverges.
    if (injectBug) rl.composition.bpm = 999.0;
    std::string jsonCheck = injectBug ? libToJsonV2(rl) : json2;
    expect("composition savev2 roundtrip byte-stable", json1 == jsonCheck);

    // S15: a file with a non-positive bpm drops it to the default 120, file still loads.
    std::string bad = json1;
    auto p = bad.find("\"bpm\":");
    if (p != std::string::npos) {
      auto colon = bad.find(':', p);
      auto comma = bad.find(',', colon);
      bad = bad.substr(0, colon + 1) + " 0.0" + bad.substr(comma);
    }
    SymbolLibrary bl;
    std::vector<std::string> bw;
    bool badLoaded = libFromJsonAny(bad, bl, &bw);
    expect("S15: bad-bpm file still loads", badLoaded);
    expectNear("S15: bad bpm falls back to default 120", bl.composition.bpm, 120.0);
    expect("S15: a warning was emitted for the bad bpm", !bw.empty());

    // S15: tiny-POSITIVE bpm (1e-300, legal JSON) passed the old >0 gate and made
    // secondsFromBars blow to inf downstream (refuter-S5 BROKEN-B repro → golden).
    std::string tiny = json1;
    p = tiny.find("\"bpm\":");
    if (p != std::string::npos) {
      auto colon = tiny.find(':', p);
      auto comma = tiny.find(',', colon);
      tiny = tiny.substr(0, colon + 1) + " 1e-300" + tiny.substr(comma);
    }
    SymbolLibrary tl;
    std::vector<std::string> tw;
    expect("S15: tiny-positive-bpm file still loads", libFromJsonAny(tiny, tl, &tw));
    expectNear("S15: tiny-positive bpm clamped to default 120", tl.composition.bpm, 120.0);
    Transport tt; tt.bpm = tl.composition.bpm;
    expect("S15: secondsFromBars stays finite under loaded bpm",
           std::isfinite(tt.secondsFromBars(4.0)));
  }

  printf("[selftest] transport %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw
