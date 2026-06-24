// variation_mix_selftest — --selftest-variation-mix. Harness-first golden for the N-WAY per-type Mix
// (TiXL ExplorationVariation.cs:66-191) and its document-override apply path (buildNWayMixCommand).
//
// This is the N-way cousin of --selftest-variation's 2-way crossfader golden: where the crossfader is
// Lerp(a,b,t) between TWO snapshots, Mix is the normalized weighted average Σ(v·w)/Σw over N neighbours
// with the missing-neighbour fallback (a neighbour that does not carry a parameter contributes the
// CURRENT value AT its weight, NOT 0). Two golden families, both pure-CPU / no-GPU / no-cook-core:
//
//   GOLDEN MIX — closed-form per type (float / vec2 / vec3 / vec4 / truncating-int): N=3 neighbours,
//     hand-chosen weights {1,2,1} (sumWeight=4) → assert Σ(v·w)/Σw EXACTLY. Plus the missing-neighbour
//     leg: one neighbour absent → assert it contributes currentValue at its weight (verbatim TiXL
//     matchingParam = param.InputSlot.Input.Value, ExplorationVariation.cs:94/115/140/166).
//     -bug forgets the 1/sumWeight normalize (uses the raw Σ(v·w)) → the assertions bite.
//
//   GOLDEN DOC-APPLY — buildNWayMixCommand emits a MacroCommand of SetOverrideCommands; read each slot
//     back through effectiveInput (the GRAPH, not a side map), assert the weighted result landed as a
//     real override, then undo → prior overrides restored. -bug tampers the expected so the tooth bites.
//
// TiXL ground-truth: ExplorationVariation.cs Mix branches (float :87-106, Vector2 :108-131, Vector3
// :133-157, Vector4 :159-184 — all structurally identical Σ(v·w)/Σw + missing=current fallback),
// ValueUtils.cs:72-80 (int BlendMethod = (int)Lerp, truncating). The int Mix is a named faithful
// extension (TiXL's Mix() has no int branch; we carry the truncation from the int BlendMethod) — see
// variation_mix.h mixInt note.
#include <cmath>
#include <cstdio>
#include <vector>

#include "app/variation_apply.h"          // buildNWayMixCommand / DocMixNeighbour / DocVariation
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/variation_mix.h"        // mixFloat / mixVec2/3/4 / mixInt / MixNeighbour(Vec)

namespace sw {
namespace {

constexpr float kTol = 1e-4f;

// ── GOLDEN MIX — per-type closed form + missing-neighbour fallback ──────────────────────────────
// Weights {1,2,1}, sumWeight = 4.
bool runMixClosedForm(bool injectBug) {
  bool ok = true;

  // FLOAT: values {10,20,40} w{1,2,1} → (10*1 + 20*2 + 40*1)/4 = 90/4 = 22.5.
  {
    std::vector<MixNeighbour> nbs = {{10.0f, 1.0f, true}, {20.0f, 2.0f, true}, {40.0f, 1.0f, true}};
    const float got = mixFloat(nbs, /*currentValue=*/-999.0f);  // current unused (all present)
    // -bug forgets the 1/sumWeight normalize → raw Σ(v·w) = 90 (not 22.5).
    const float buggy = injectBug ? (10.0f * 1 + 20.0f * 2 + 40.0f * 1) : got;
    const float want = 22.5f;
    const bool legOk = std::fabs(buggy - want) < kTol;
    ok = legOk && ok;
    std::printf("[selftest-variation-mix] FLOAT {10,20,40} w{1,2,1} -> %.4f (want 22.5) -> %s\n",
                buggy, legOk ? "ok" : "BAD");
  }

  // FLOAT MISSING-NEIGHBOUR: neighbour-2 (weight 2) absent, currentValue=100 →
  //   (10*1 + 100*2 + 40*1)/4 = 250/4 = 62.5 (missing contributes CURRENT at its weight, not 0).
  {
    std::vector<MixNeighbour> nbs = {{10.0f, 1.0f, true},
                                     {0.0f, 2.0f, false},   // missing → uses currentValue (100)
                                     {40.0f, 1.0f, true}};
    const float got = mixFloat(nbs, /*currentValue=*/100.0f);
    // -bug uses 0 for the missing neighbour (the classic wrong fallback): (10+0+40)/4 = 12.5.
    const float buggy = injectBug ? ((10.0f * 1 + 0.0f * 2 + 40.0f * 1) / 4.0f) : got;
    const float want = 62.5f;
    const bool legOk = std::fabs(buggy - want) < kTol;
    ok = legOk && ok;
    std::printf("[selftest-variation-mix] FLOAT missing-nb cur=100 -> %.4f (want 62.5) -> %s\n",
                buggy, legOk ? "ok" : "BAD");
  }

  // VEC2: {(1,2),(3,4),(5,6)} w{1,2,1} → x=(1+6+5)/4=3, y=(2+8+6)/4=4 → (3,4).
  {
    std::vector<MixNeighbourVec> nbs = {{{1, 2, 0, 0}, 1.0f, true},
                                        {{3, 4, 0, 0}, 2.0f, true},
                                        {{5, 6, 0, 0}, 1.0f, true}};
    const float cur[2] = {-999.0f, -999.0f};
    float out[2];
    mixVec2(nbs, cur, out);
    const bool legOk = std::fabs(out[0] - 3.0f) < kTol && std::fabs(out[1] - 4.0f) < kTol;
    ok = legOk && ok;
    std::printf("[selftest-variation-mix] VEC2 -> (%.4f,%.4f) want (3,4) -> %s\n",
                out[0], out[1], legOk ? "ok" : "BAD");
  }

  // VEC3: {(0,0,0),(4,8,12),(0,0,0)} w{1,2,1} → (8/4,16/4,24/4) = (2,4,6).
  {
    std::vector<MixNeighbourVec> nbs = {{{0, 0, 0, 0}, 1.0f, true},
                                        {{4, 8, 12, 0}, 2.0f, true},
                                        {{0, 0, 0, 0}, 1.0f, true}};
    const float cur[3] = {-999.0f, -999.0f, -999.0f};
    float out[3];
    mixVec3(nbs, cur, out);
    const bool legOk = std::fabs(out[0] - 2.0f) < kTol && std::fabs(out[1] - 4.0f) < kTol &&
                       std::fabs(out[2] - 6.0f) < kTol;
    ok = legOk && ok;
    std::printf("[selftest-variation-mix] VEC3 -> (%.4f,%.4f,%.4f) want (2,4,6) -> %s\n",
                out[0], out[1], out[2], legOk ? "ok" : "BAD");
  }

  // VEC4: {(2,2,2,2),(6,6,6,6),(10,10,10,10)} w{1,2,1} → x=(2+12+10)/4=6 → (6,6,6,6).
  {
    std::vector<MixNeighbourVec> nbs = {{{2, 2, 2, 2}, 1.0f, true},
                                        {{6, 6, 6, 6}, 2.0f, true},
                                        {{10, 10, 10, 10}, 1.0f, true}};
    const float cur[4] = {-999.0f, -999.0f, -999.0f, -999.0f};
    float out[4];
    mixVec4(nbs, cur, out);
    const bool legOk = std::fabs(out[0] - 6.0f) < kTol && std::fabs(out[1] - 6.0f) < kTol &&
                       std::fabs(out[2] - 6.0f) < kTol && std::fabs(out[3] - 6.0f) < kTol;
    ok = legOk && ok;
    std::printf("[selftest-variation-mix] VEC4 -> (%.4f,..) want (6,6,6,6) -> %s\n",
                out[0], legOk ? "ok" : "BAD");
  }

  // INT (truncating): values {1,2,4} w{1,1,1} → (1+2+4)/3 = 7/3 = 2.333 → trunc → 2.
  {
    std::vector<MixNeighbour> nbs = {{1.0f, 1.0f, true}, {2.0f, 1.0f, true}, {4.0f, 1.0f, true}};
    const int got = mixInt(nbs, /*currentValue=*/-999);
    // -bug rounds instead of truncating (2.333 → 2 here is a no-op; pick a rounding case): use
    //   {1,2,5} → 8/3 = 2.667 → trunc 2, round 3. Assert truncation (2), bug uses round (3).
    std::vector<MixNeighbour> nbs2 = {{1.0f, 1.0f, true}, {2.0f, 1.0f, true}, {5.0f, 1.0f, true}};
    const int got2 = mixInt(nbs2, -999);
    const int buggy2 = injectBug ? (int)std::lround(8.0f / 3.0f) : got2;  // bug rounds → 3
    const bool legOk = (got == 2) && (buggy2 == 2);
    ok = legOk && ok;
    std::printf("[selftest-variation-mix] INT trunc {1,2,4}->%d(want 2) {1,2,5}->%d(want 2 trunc) "
                "-> %s\n", got, buggy2, legOk ? "ok" : "BAD");
  }

  std::printf("[selftest-variation-mix] CLOSED-FORM -> %s\n", ok ? "PASS" : "FAIL");
  return ok;
}

// ── GOLDEN DOC-APPLY — buildNWayMixCommand readback through effectiveInput + undo ────────────────
// One composition "comp" with one child (id 1) of "Op" {a(def 0), b(def 0), c(def 7)}.
// Current: a overridden to 100 (has override). N=3 neighbours, weights {1,2,1} (sumWeight=4):
//   slot a — tracked by all: nb0=0, nb1=40 (w2), nb2=80 → (0*1 + 40*2 + 80*1)/4 = 160/4 = 40.
//   slot b — tracked only by nb1 (=20, w2); nb0/nb2 MISSING → use current b (default 0):
//             (0*1 + 20*2 + 0*1)/4 = 40/4 = 10.
//   slot c — tracked by NONE, no override (at default 7) → SKIPPED (no command).
//   Undo: a restored to 100 (had override), b override ERASED (had none) → back to default 0.
SymbolLibrary buildLib() {
  SymbolLibrary lib;
  Symbol op;
  op.id = "Op"; op.name = "Op"; op.atomic = true;
  op.inputDefs = {{"a", "A", "Float", 0.0f, "", 0, 0},
                  {"b", "B", "Float", 0.0f, "", 0, 0},
                  {"c", "C", "Float", 7.0f, "", 0, 0}};
  lib.symbols["Op"] = op;
  Symbol comp;
  comp.id = "comp"; comp.name = "comp"; comp.atomic = false;
  SymbolChild child; child.id = 1; child.symbolId = "Op";
  comp.children.push_back(child);
  comp.nextChildId = 2;
  lib.symbols["comp"] = comp;
  lib.rootId = "comp";
  return lib;
}

float readBack(SymbolLibrary& lib, const std::string& slotId) {
  Symbol* s = lib.find("comp");
  SymbolChild* c = s ? childById(*s, 1) : nullptr;
  return c ? effectiveInput(lib, *c, slotId, -999.0f) : -999.0f;
}
bool hasKey(SymbolLibrary& lib, const std::string& slotId) {
  Symbol* s = lib.find("comp");
  SymbolChild* c = s ? childById(*s, 1) : nullptr;
  return c && c->overrides.count(slotId) > 0;
}

bool runDocApply(bool injectBug) {
  SymbolLibrary lib = buildLib();
  childById(*lib.find("comp"), 1)->overrides["a"] = 100.0f;  // a has a manual override

  // Three neighbours, weights {1,2,1}.
  DocMixNeighbour n0, n1, n2;
  n0.weight = 1.0f; n1.weight = 2.0f; n2.weight = 1.0f;
  n0.snapshot.parameterSets[1]["a"] = 0.0f;
  n1.snapshot.parameterSets[1]["a"] = 40.0f;
  n2.snapshot.parameterSets[1]["a"] = 80.0f;
  n1.snapshot.parameterSets[1]["b"] = 20.0f;  // only nb1 tracks b → nb0/nb2 missing for b

  auto macro = buildNWayMixCommand(lib, "comp", {n0, n1, n2});
  macro->doIt();

  const float gotA = readBack(lib, "a");
  const float gotB = readBack(lib, "b");
  const float gotC = readBack(lib, "c");
  // -bug expects a=100 (the pre-mix override) → bites the real N-way override (40).
  const float wantA = injectBug ? 100.0f : 40.0f;
  const bool applyOk = std::fabs(gotA - wantA) < kTol &&
                       std::fabs(gotB - 10.0f) < kTol &&   // missing-nb fallback to current (0)
                       std::fabs(gotC - 7.0f) < kTol &&     // c untouched (default)
                       !hasKey(lib, "c");                    // c got no command

  macro->undo();
  const float undoA = readBack(lib, "a");
  const float undoB = readBack(lib, "b");
  const bool undoOk = std::fabs(undoA - 100.0f) < kTol &&  // a restored to its prior override
                      std::fabs(undoB - 0.0f) < kTol &&     // b back to default
                      hasKey(lib, "a") && !hasKey(lib, "b");

  const bool ok = applyOk && undoOk;
  std::printf("[selftest-variation-mix] DOC-APPLY N=3 w{1,2,1} a=%.4f(want %.4f) b=%.4f c=%.4f | "
              "undo a=%.4f b=%.4f -> %s\n",
              gotA, wantA, gotB, gotC, undoA, undoB, ok ? "PASS" : "FAIL");
  return ok;
}

}  // namespace

int runVariationMixSelfTest(bool injectBug) {
  bool ok = true;
  ok = runMixClosedForm(injectBug) && ok;
  ok = runDocApply(injectBug) && ok;
  std::printf("[selftest-variation-mix] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/305, {"variation-mix", runVariationMixSelfTest});

}  // namespace sw
