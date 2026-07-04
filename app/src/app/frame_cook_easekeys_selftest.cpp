// app/frame_cook_easekeys_selftest — EaseKeys family PRODUCTION-PATH pin (--selftest-easekeys).
// R-2 IRON RULE: the EaseKeys ops cook on the RESIDENT stateful-value seam (they read the keyframe
// curves on their OWN Value input through the node's Automation drivers), so the golden drives the
// REAL production seam (framecook::cookStatefulValueNodes — the exact once-per-frame cook run()
// calls) with REAL def-layer Animator curves installed on the root Symbol, varying the transport
// playhead (t.position = context.LocalTime, bars) per probe, and reads Result via extOut.
//
// Expected values hand-derived from TiXL EaseKeys.cs (Update cs:18-61) + EasingFunctions.cs (exact
// formulas, cited per probe) + Curve.cs FindIndexBefore (cs:160-185, prev = last key STRICTLY < t):
//   curve A = keys (u=0,v=1) → (u=4,v=3), Linear key interpolation (the eased SEGMENT VALUES matter,
//   not the curve's own interpolation — EaseKeys REPLACES it). duration=4.
//     t=1, InOut Expo (the .t3 defaults, Direction=2/Interpolation=6): progress=0.25 →
//       InOutExpo(0.25) = 2^(20*0.25-10)/2 = 2^-5/2 = 1/64 (EasingFunctions.cs:99-107) →
//       1 + 2*(1/64) = 1.03125          ← MID-SEGMENT probe (off both keys, non-identity params)
//     t=3, InOut Expo: progress=0.75 → (2-2^-5)/2 = 63/64 → 1+2*(63/64) = 2.96875
//     t=1, In Quad (0/2): 0.25² = 0.0625 (cs:27-30) → 1.125
//     t=1, Out Bounce (1/10): OutBounce(0.25) = 7.5625*0.25² = 0.47265625 (cs:190-211, first band
//       0.25 < 1/2.75) → 1.9453125
//     t=1, Linear (0): ApplyEasing default arm `_ => progress` (cs:228-244) → lerp = 1.5
//     t=5 (past last key): next=null (cs:48-52) → Result = inputValue = the SAMPLED curve value =
//       3.0 (post-range Constant hold) — the unauthored fallback path
//     t=-1 (before first key): prev=null → inputValue = 1.0
//     key0.OutInterpolation=Constant (cs:39-40): t=1 → inputValue = curve.sample(1) = 1.0 (hold)
//     NOT animated (no curves): Value override 7.5 → passthrough 7.5 (cs:22-26)
//   EaseVec2Keys (per-channel curves, PER-CHANNEL durations — cs:32-62): ch0 = curve A,
//   ch1 = (0,10)→(2,20) (duration=2), defaults InOut Expo:
//     t=0.5 → ch0 progress=0.125: InOutExpo(0.125)=2^-7.5/2 → 1+2*(2^-7.5/2) = 1.0055242717
//             ch1 progress=0.25:  1/64 → 10+10/64 = 10.15625
//     t=3   → ch0 = 2.96875 ; ch1 past its last key → inputValue = sample(3) = 20.0 (independence)
//   EaseVec3Keys (In Quad 0/2): ch0=A, ch1=(0,10)→(2,20), ch2=(0,-5)→(8,5):
//     t=1 → (1 + 2*0.0625, 10 + 10*0.25, -5 + 10*(0.125²)) = (1.125, 12.5, -4.84375)
//
// injectBug (REAL production-term corruption via setEaseKeysBug, NOT flipped expectations):
//   bug 1 = DROP the easing shaping (eased := raw progress) → every non-Linear mid-segment probe
//           reads the linear lerp (1.5 ≠ 1.03125 …) → RED; the Linear + fallback probes stay green,
//           isolating the bite to the shaping term.
//   bug 2 = SEVER the curve query → every eased probe collapses to the sampled passthrough → RED,
//           proving the def-layer Automation curve read is load-bearing.
// Wants are INDEPENDENT of the flag. did-not-trip → 0 added failures → exit 0 → --bite NO-BITE.
#include "app/frame_cook.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "runtime/curve.h"                // Curve / VDefinition / KeyInterpolation
#include "runtime/curve_animator.h"       // Animator
#include "runtime/graph.h"                // findSpec
#include "runtime/graph_bridge.h"         // atomicSymbolFromSpec
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / initResidentCache / ResidentNode
#include "runtime/stateful_value_ops.h"   // StatefulValueState / ContextVarMap / setEaseKeysBug
#include "runtime/transport.h"            // Transport

namespace sw::framecook {
namespace {

int g_fail = 0;
void expectClose(const char* what, float got, float want) {
  const bool ok = std::fabs(got - want) < 1e-4f;
  if (!ok) { ++g_fail; printf("  [easekeys] FAIL %s got=%.7f want=%.7f\n", what, got, want); }
  else printf("  [easekeys] ok   %s = %.7f\n", what, got);
}

// A 2-key Linear curve (u0,v0) → (u1,v1); constOut flags key0's OutInterpolation=Constant.
Curve makeCurve(double u0, double v0, double u1, double v1, bool constOut = false) {
  Curve c;
  VDefinition k0; k0.u = u0; k0.value = v0;
  if (constOut) k0.outInterpolation = KeyInterpolation::Constant;
  VDefinition k1; k1.u = u1; k1.value = v1;
  c.addOrUpdate(u0, k0);
  c.addOrUpdate(u1, k1);
  return c;
}

// Build a lib with ONE child (id 1) of `opType` + the given animator curves installed under
// `headSlot` on the root definition (= the Inspector "animate input" act: curves + the flatten's
// Automation-driver projection are one truth, resident_eval_flatten.cpp).
SymbolLibrary makeLib(const char* opType, const std::map<std::string, float>& overrides,
                      const std::string& headSlot, Animator::CurveArray curves) {
  SymbolLibrary lib;
  if (const NodeSpec* s = findSpec(opType)) lib.symbols[opType] = atomicSymbolFromSpec(*s);
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild c; c.id = 1; c.symbolId = opType; c.overrides = overrides;
  root.children = {c};
  root.nextChildId = 2;
  if (!curves.empty()) root.animator.setCurves(1, headSlot, std::move(curves));
  lib.symbols["R"] = root; lib.rootId = "R";
  return lib;
}

// Cook one production frame at playhead `bars`; return extOut[idx] of child "1".
float cookAt(SymbolLibrary& lib, double bars, int outIdx) {
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  ContextVarMap vars;
  std::map<std::string, StatefulValueState> state;
  Transport t; t.bpm = 120.0; t.rate = 1.0; t.play();
  t.position = bars;  // = context.LocalTime (bars) — the clock EaseKeys and the sampler share
  cookStatefulValueNodes(g, 1.0f / 60.0f, 0.0f, 0.0, t, 0, &lib, state, vars);
  const ResidentNode* n = g.node("1");
  return n ? n->extOut[outIdx] : -999.0f;
}

// The full golden body (clean or under a bug mode). Accumulates into g_fail.
void runAllGoldens() {
  // --- A: EaseKeys scalar, curve (0,1)→(4,3). ---
  {
    // .t3 defaults InOut(2) Expo(6) — mid-segment probes.
    Animator::CurveArray arr; arr.push_back(makeCurve(0, 1, 4, 3));
    SymbolLibrary lib = makeLib("EaseKeys", {{"Direction", 2.0f}, {"Interpolation", 6.0f}}, "Value", arr);
    expectClose("A InOutExpo t=1", cookAt(lib, 1.0, 0), 1.03125f);
    expectClose("A InOutExpo t=3", cookAt(lib, 3.0, 0), 2.96875f);
    expectClose("A past-last t=5 (fallback=sampled 3)", cookAt(lib, 5.0, 0), 3.0f);
    expectClose("A before-first t=-1 (fallback=sampled 1)", cookAt(lib, -1.0, 0), 1.0f);
  }
  {
    Animator::CurveArray arr; arr.push_back(makeCurve(0, 1, 4, 3));
    SymbolLibrary lib = makeLib("EaseKeys", {{"Direction", 0.0f}, {"Interpolation", 2.0f}}, "Value", arr);
    expectClose("A InQuad t=1", cookAt(lib, 1.0, 0), 1.125f);
  }
  {
    Animator::CurveArray arr; arr.push_back(makeCurve(0, 1, 4, 3));
    SymbolLibrary lib = makeLib("EaseKeys", {{"Direction", 1.0f}, {"Interpolation", 10.0f}}, "Value", arr);
    expectClose("A OutBounce t=1", cookAt(lib, 1.0, 0), 1.9453125f);
  }
  {
    Animator::CurveArray arr; arr.push_back(makeCurve(0, 1, 4, 3));
    SymbolLibrary lib = makeLib("EaseKeys", {{"Direction", 2.0f}, {"Interpolation", 0.0f}}, "Value", arr);
    expectClose("A Linear t=1 (default arm)", cookAt(lib, 1.0, 0), 1.5f);
  }
  {
    // key0.OutInterpolation=Constant → cs:39-40 fallback to the sampled hold value.
    Animator::CurveArray arr; arr.push_back(makeCurve(0, 1, 4, 3, /*constOut=*/true));
    SymbolLibrary lib = makeLib("EaseKeys", {{"Direction", 2.0f}, {"Interpolation", 6.0f}}, "Value", arr);
    expectClose("A Constant-out t=1 (hold 1)", cookAt(lib, 1.0, 0), 1.0f);
  }
  {
    // NOT animated → passthrough (cs:22-26).
    SymbolLibrary lib = makeLib("EaseKeys", {{"Value", 7.5f}}, "", {});
    expectClose("A not-animated passthrough", cookAt(lib, 1.0, 0), 7.5f);
  }
  // --- B: EaseVec2Keys — per-channel curves + per-channel DURATIONS (defaults InOut Expo). ---
  {
    Animator::CurveArray arr;
    arr.push_back(makeCurve(0, 1, 4, 3));    // ch0 (duration 4)
    arr.push_back(makeCurve(0, 10, 2, 20));  // ch1 (duration 2)
    SymbolLibrary lib =
        makeLib("EaseVec2Keys", {{"Direction", 2.0f}, {"Interpolation", 6.0f}}, "Value.x", arr);
    expectClose("B ch0 InOutExpo t=0.5", cookAt(lib, 0.5, 0), 1.0055243f);   // 1+2*(2^-7.5/2)
    expectClose("B ch1 InOutExpo t=0.5", cookAt(lib, 0.5, 1), 10.15625f);
    expectClose("B ch0 InOutExpo t=3", cookAt(lib, 3.0, 0), 2.96875f);
    expectClose("B ch1 past-last t=3 (fallback=20)", cookAt(lib, 3.0, 1), 20.0f);
  }
  // --- C: EaseVec3Keys — In Quad, three independent channels. ---
  {
    Animator::CurveArray arr;
    arr.push_back(makeCurve(0, 1, 4, 3));    // ch0: p=0.25 → 1+2*0.0625
    arr.push_back(makeCurve(0, 10, 2, 20));  // ch1: p=0.5  → 10+10*0.25
    arr.push_back(makeCurve(0, -5, 8, 5));   // ch2: p=0.125→ -5+10*0.015625
    SymbolLibrary lib =
        makeLib("EaseVec3Keys", {{"Direction", 0.0f}, {"Interpolation", 2.0f}}, "Value.x", arr);
    expectClose("C ch0 InQuad t=1", cookAt(lib, 1.0, 0), 1.125f);
    expectClose("C ch1 InQuad t=1", cookAt(lib, 1.0, 1), 12.5f);
    expectClose("C ch2 InQuad t=1", cookAt(lib, 1.0, 2), -4.84375f);
  }
}

}  // namespace

int runEaseKeysSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] easekeys (EaseKeys/EaseVec2Keys/EaseVec3Keys on the PRODUCTION cook)\n");

  // Always run the full golden CLEAN first (production behavior must pass regardless of injectBug).
  setEaseKeysBug(0);
  runAllGoldens();
  const int cleanFail = g_fail;
  printf("[selftest] easekeys clean pass: %d failure(s)\n", cleanFail);

  if (injectBug) {
    // Same fixed-want goldens under each real-term corruption — they must FAIL (the teeth).
    setEaseKeysBug(1);
    int before1 = g_fail;
    runAllGoldens();
    printf("[selftest] easekeys bug1(drop-easing-shaping) added %d failure(s)\n", g_fail - before1);
    setEaseKeysBug(2);
    int before2 = g_fail;
    runAllGoldens();
    printf("[selftest] easekeys bug2(sever-curve-query) added %d failure(s)\n", g_fail - before2);
    setEaseKeysBug(0);  // restore production
  }

  printf("[selftest] easekeys %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw::framecook
