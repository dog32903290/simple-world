// app/frame_cook_keyframes_selftest — FindKeyframes / SetKeyframes PRODUCTION-PATH pin
// (--selftest-keyframes). These ops REFLECT across the AnimatedOp connection into a CONNECTED UPSTREAM
// operator's keyframe curves. R-2 IRON RULE: they cook on the RESIDENT stateful-value seam, so the
// golden drives the REAL production cook (framecook::cookStatefulValueNodes) with a REAL two-child
// graph — an upstream Sine whose `x` input is animated (def-layer Animator curve installed), wired
// into a FindKeyframes/SetKeyframes `AnimatedOp` — and reads the results via extOut.
//
// This is the DIFFERENTIATING probe vs EaseKeys: EaseKeys reads its OWN input's curve; here the curve
// lives on the UPSTREAM node and is reached only by following the connection. bug 1 SEVERS that
// cross-connection reach — every value below collapses → RED — proving the reflection-through-connection
// seam (not a self-sourced constant) is load-bearing.
//
// Expected values hand-derived from TiXL FindKeyframes.cs / SetKeyframes.cs + Curve.cs:
//   Upstream Sine.x curve = keys (u=0,v=1) → (u=4,v=3) → (u=8,v=7), Linear. 3 keyframes, duration
//   seg0=4. GetVDefinitions order = ascending u (Curve.cs:16-19).
//   FindKeyframes (cs:66-95):
//     KeyframeCount = 3 (cs:50) — the count-of-keys channel (extOut[2]).
//     Mode=Index(0), IndexOrTime=1 → keyframes[1] = (u=4,v=3): Time=4, Value=3 (cs:73-75). ← MID key,
//         not endpoint 0/2 (P2: picks the wrong key if the ordering/index is wrong).
//     Mode=Index(0), IndexOrTime=2 → keyframes[2] = (u=8,v=7): Time=8, Value=7.
//     Mode=Nearest(1), IndexOrTime=3 → |3-0|=3,|3-4|=1,|3-8|=5 → closest key u=4: Time=4, Value=3 (cs:79-83).
//     Mode=SampleAndDistance(2), IndexOrTime=2 → closest key (tie 0 vs 4, `<` false → next=4):
//         Time = 4 - 2 = 2 (cs:90); Value = GetSampledValue(2) = Lerp(1,3, 2/4) = 2.0 (cs:91). ← reads
//         the INTERPOLATED sampler, off any keyframe (P2: a swapped Time/Value channel or bad sample bites).
//   SetKeyframes (cs:18-67), same upstream curve, mutated through the SAME cross-connection reach:
//     TriggerSet rising edge (false→true) @ LocalFxTime=6, Value=42 → AddOrUpdateV(6,{42}) (cs:52):
//         the curve gains a key → count 3→4, and FindKeyframes(Index, IndexOrTime pointing at u=6)
//         reads Value=42. A HELD-high trigger (true→true) does NOT re-fire (WasTriggered, MathUtils.cs).
//     TriggerClear rising edge → every key removed (cs:56-65) → count → 0.
//
// injectBug (REAL production-term corruption via setKeyframesBug, NOT flipped expectations):
//   bug 1 = SEVER the cross-connection curve query → Find collapses to 0s, Set writes nothing → RED.
//   bug 2 = DROP the value read (Find returns the keyframe TIME in the Value channel) → the Value
//           probes bite while the Time/Count probes stay green, isolating the value-vs-time selection.
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
#include "runtime/stateful_value_ops.h"   // StatefulValueState / ContextVarMap / setKeyframesBug
#include "runtime/transport.h"            // Transport

namespace sw::framecook {
namespace {

int g_fail = 0;
void expectClose(const char* what, float got, float want) {
  const bool ok = std::fabs(got - want) < 1e-4f;
  if (!ok) { ++g_fail; printf("  [keyframes] FAIL %s got=%.7f want=%.7f\n", what, got, want); }
  else printf("  [keyframes] ok   %s = %.7f\n", what, got);
}

// The 3-key upstream curve (0,1)→(4,3)→(8,7), Linear.
Animator::CurveArray upstreamCurves() {
  Curve c;
  VDefinition k0; k0.u = 0; k0.value = 1;
  VDefinition k1; k1.u = 4; k1.value = 3;
  VDefinition k2; k2.u = 8; k2.value = 7;
  c.addOrUpdate(0, k0);
  c.addOrUpdate(4, k1);
  c.addOrUpdate(8, k2);
  Animator::CurveArray arr; arr.push_back(std::move(c));
  return arr;
}

// Build a lib: child 1 = upstream Sine with `x` animated (curve on the root Animator + the flatten's
// Automation-driver projection are one truth), child 2 = `readerType` (FindKeyframes/SetKeyframes) with
// its AnimatedOp wired FROM child 1's `out`. `overrides` seeds child 2's params.
SymbolLibrary makeLib(const char* readerType, const std::map<std::string, float>& overrides,
                      bool animate = true) {
  SymbolLibrary lib;
  if (const NodeSpec* s = findSpec("Sine")) lib.symbols["Sine"] = atomicSymbolFromSpec(*s);
  if (const NodeSpec* s = findSpec(readerType)) lib.symbols[readerType] = atomicSymbolFromSpec(*s);
  Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
  SymbolChild up; up.id = 1; up.symbolId = "Sine";
  SymbolChild rd; rd.id = 2; rd.symbolId = readerType; rd.overrides = overrides;
  root.children = {up, rd};
  root.nextChildId = 3;
  // Wire child1.out → child2.AnimatedOp (the connection FindKeyframes follows upstream).
  root.connections.push_back({1, "out", 2, "AnimatedOp"});
  if (animate) root.animator.setCurves(1, "x", upstreamCurves());  // animate the UPSTREAM input
  lib.symbols["R"] = root; lib.rootId = "R";
  return lib;
}

// Cook one production frame at wall clock `fxBars` (SetKeyframes writes @ LocalFxTime); return the
// reader child's extOut[idx]. `state`/`lib` are held across calls by the caller for the trigger latch +
// the persistent curve store; a fresh graph is built each cook (production rebuilds every libRevision).
float cookAt(SymbolLibrary& lib, std::map<std::string, StatefulValueState>& state, double fxBars,
             int outIdx, bool* libDirtied = nullptr) {
  ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
  initResidentCache(g);
  ContextVarMap vars;
  Transport t; t.bpm = 120.0; t.rate = 1.0; t.play();
  t.position = 0.0;
  t.fxTime = fxBars;  // = context.LocalFxTime (bars) — SetKeyframes' write clock
  cookStatefulValueNodes(g, 1.0f / 60.0f, 0.0f, 0.0, t, 0, &lib, state, vars, /*ctxVarBug=*/0,
                         /*mutableLib=*/&lib, libDirtied);
  const ResidentNode* n = g.node("2");
  return n ? n->extOut[outIdx] : -999.0f;
}

void runAllGoldens() {
  // --- FindKeyframes: read the upstream Sine.x curve across the connection (extOut: 0=Time,1=Value,2=Count). ---
  {
    // Mode=Index(0). IndexOrTime picks the keyframe by ordinal.
    std::map<std::string, StatefulValueState> st;
    SymbolLibrary lib = makeLib("FindKeyframes", {{"Mode", 0.0f}, {"IndexOrTime", 1.0f}});
    expectClose("Find count", cookAt(lib, st, 0.0, 2), 3.0f);
    expectClose("Find Index[1] Time", cookAt(lib, st, 0.0, 0), 4.0f);
    expectClose("Find Index[1] Value", cookAt(lib, st, 0.0, 1), 3.0f);
  }
  {
    std::map<std::string, StatefulValueState> st;
    SymbolLibrary lib = makeLib("FindKeyframes", {{"Mode", 0.0f}, {"IndexOrTime", 2.0f}});
    expectClose("Find Index[2] Time", cookAt(lib, st, 0.0, 0), 8.0f);
    expectClose("Find Index[2] Value", cookAt(lib, st, 0.0, 1), 7.0f);
  }
  {
    // Mode=Nearest(1): closest key to IndexOrTime=3 is u=4.
    std::map<std::string, StatefulValueState> st;
    SymbolLibrary lib = makeLib("FindKeyframes", {{"Mode", 1.0f}, {"IndexOrTime", 3.0f}});
    expectClose("Find Nearest(3) Time", cookAt(lib, st, 0.0, 0), 4.0f);
    expectClose("Find Nearest(3) Value", cookAt(lib, st, 0.0, 1), 3.0f);
  }
  {
    // Mode=SampleAndDistance(2), IndexOrTime=2: Time=closestKey.U - t = 2; Value=sample(2)=2.0 (interp).
    std::map<std::string, StatefulValueState> st;
    SymbolLibrary lib = makeLib("FindKeyframes", {{"Mode", 2.0f}, {"IndexOrTime", 2.0f}});
    expectClose("Find Sample(2) Distance", cookAt(lib, st, 0.0, 0), 2.0f);
    expectClose("Find Sample(2) sampled Value", cookAt(lib, st, 0.0, 1), 2.0f);
  }
  {
    // NOT connected-to-animated: no curve on the upstream → count 0, Time/Value 0 (cs:52-58).
    std::map<std::string, StatefulValueState> st;
    SymbolLibrary lib = makeLib("FindKeyframes", {{"Mode", 0.0f}, {"IndexOrTime", 1.0f}}, /*animate=*/false);
    expectClose("Find no-curve count", cookAt(lib, st, 0.0, 2), 0.0f);
    expectClose("Find no-curve Value", cookAt(lib, st, 0.0, 1), 0.0f);
  }
  // --- SetKeyframes: WRITE the upstream curve across the connection, then READ it back with FindKeyframes. ---
  {
    // One lib carried across cooks: the curve store persists, the trigger latch lives in `st`.
    std::map<std::string, StatefulValueState> st;
    SymbolLibrary lib = makeLib("SetKeyframes", {{"Value", 42.0f}});
    // Frame 1: TriggerSet LOW → no write. (edge latch initialises to 0/false.)
    cookAt(lib, st, 6.0, 0);
    // Frame 2: TriggerSet HIGH (rising edge false→true) @ fx=6 → AddOrUpdateV(6,{42}).
    lib.symbols["R"].children[1].overrides["TriggerSet"] = 1.0f;
    bool dirtied = false;
    cookAt(lib, st, 6.0, 0, &dirtied);
    expectClose("Set rising-edge dirtied lib", dirtied ? 1.0f : 0.0f, 1.0f);
    // The upstream curve now has 4 keys; verify via the store directly (the SAME def-layer Animator).
    // Safe lookup (no .at throw): under a severed-write bug the key @6 is absent → want-mismatch, not a crash.
    const Curve* c = lib.symbols["R"].animator.resolveRef(Animator::makeRef(1, "x", 0));
    expectClose("Set key count after set", c ? (float)c->count() : -1.0f, 4.0f);
    float v6 = -1.0f;
    if (c) { auto it = c->table().find(6.0); if (it != c->table().end()) v6 = (float)it->second.value; }
    expectClose("Set key value @6", v6, 42.0f);
    // Frame 3: TriggerSet HELD high (true→true) → WasTriggered false → NO re-fire (still 4 keys, no new dirty).
    bool dirtied3 = false;
    cookAt(lib, st, 7.0, 0, &dirtied3);
    expectClose("Set held-high no re-fire", dirtied3 ? 1.0f : 0.0f, 0.0f);
    const Curve* c3 = lib.symbols["R"].animator.resolveRef(Animator::makeRef(1, "x", 0));
    expectClose("Set held-high still 4 keys", c3 ? (float)c3->count() : -1.0f, 4.0f);
  }
  {
    // TriggerClear rising edge → remove every keyframe.
    std::map<std::string, StatefulValueState> st;
    SymbolLibrary lib = makeLib("SetKeyframes", {{"Value", 0.0f}});
    cookAt(lib, st, 0.0, 0);  // frame 1: clear LOW.
    lib.symbols["R"].children[1].overrides["TriggerClear"] = 1.0f;
    cookAt(lib, st, 0.0, 0);  // frame 2: clear rising edge → wipe.
    const Curve* c = lib.symbols["R"].animator.resolveRef(Animator::makeRef(1, "x", 0));
    expectClose("Set clear → 0 keys", c ? (float)c->count() : 0.0f, 0.0f);
  }
}

}  // namespace

int runKeyframesSelfTest(bool injectBug) {
  g_fail = 0;
  printf("[selftest] keyframes (FindKeyframes/SetKeyframes on the PRODUCTION cook, cross-connection)\n");

  setKeyframesBug(0);
  runAllGoldens();
  const int cleanFail = g_fail;
  printf("[selftest] keyframes clean pass: %d failure(s)\n", cleanFail);

  if (injectBug) {
    setKeyframesBug(1);
    int before1 = g_fail;
    runAllGoldens();
    printf("[selftest] keyframes bug1(sever-cross-connection-query) added %d failure(s)\n", g_fail - before1);
    setKeyframesBug(2);
    int before2 = g_fail;
    runAllGoldens();
    printf("[selftest] keyframes bug2(drop-value-read) added %d failure(s)\n", g_fail - before2);
    setKeyframesBug(0);  // restore production
  }

  printf("[selftest] keyframes %s (%d failures)\n", g_fail ? "FAIL" : "PASS", g_fail);
  return g_fail ? 1 : 0;
}

}  // namespace sw::framecook
