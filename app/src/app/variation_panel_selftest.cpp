// app/variation_panel_selftest — the P2 panel-wiring golden (--selftest-variation-panel), split out of
// variation_panel.cpp (ARCHITECTURE rule 4, one file ≤400 lines). Drives the PUBLIC panel API (grab /
// activate / delete / setMixWeight / applyMix / armCrossfade / updateCrossfade / tickCrossfade) against
// a tiny in-process document, asserting the pool transitions + the N-way mix math + the crossfader
// writeback, each with an injectBug RED leg.
#include "app/variation_panel.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include "app/command.h"            // g_commands (undo in the mix golden)
#include "app/document.h"           // doc::g_lib() / g_compositionPath (the live document the panel reads)
#include "runtime/compound_graph.h"  // SymbolLibrary / SlotDef / childById / effectiveInput
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS

namespace sw::varpanel {
namespace {

constexpr float kTol = 1e-4f;

// One composition "comp" with one child (id 1) instancing "Op" with one Float input "amount" (def 0).
SymbolLibrary buildLib() {
  SymbolLibrary lib;
  Symbol op;
  op.id = "Op"; op.name = "Op"; op.atomic = true;
  op.inputDefs = {{"amount", "Amount", "Float", 0.0f, "", 0, 0}};
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

float readAmount(SymbolLibrary& lib) {
  Symbol* s = lib.find("comp");
  SymbolChild* c = s ? childById(*s, 1) : nullptr;
  return c ? effectiveInput(lib, *c, "amount", -999.0f) : -999.0f;
}
bool hasOverride(SymbolLibrary& lib) {
  Symbol* s = lib.find("comp");
  SymbolChild* c = s ? childById(*s, 1) : nullptr;
  return c && c->overrides.count("amount") > 0;
}
void setAmount(SymbolLibrary& lib, float v) {
  Symbol* s = lib.find("comp");
  if (SymbolChild* c = childById(*s, 1)) c->overrides["amount"] = v;
}

// GOLDEN A — grab / activate / delete state transitions on the real pool.
//   Set amount=5, grab into slot 1 -> slot 1 filled tracking amount=5.
//   Reset amount to 0, activate slot 1 -> graph override == 5 (snapshot applied, readable back).
//   Delete slot 1 -> slot 1 empty; a second delete = no-op.
bool goldenGrabActivateDelete(bool injectBug) {
  reset();
  doc::g_lib() = buildLib();
  SymbolLibrary& lib = doc::g_lib();

  setAmount(lib, 5.0f);
  const bool grabbed = grabSnapshot(lib, 1);
  const auto afterGrab = slots();
  const bool filledAfterGrab = afterGrab[0].filled && afterGrab[0].paramCount == 1;

  setAmount(lib, 0.0f);  // move the live value away from the snapshot
  const bool activated = activateSnapshot(lib, 1);
  const float applied = readAmount(lib);  // must snap back to the snapshot value (5)
  // injectBug expects 0 (the live value pre-activate) -> bites the real apply (5).
  const float wantApplied = injectBug ? 0.0f : 5.0f;
  const bool appliedOk = std::fabs(applied - wantApplied) < kTol;

  const bool deleted = deleteSnapshot(1);
  const auto afterDel = slots();
  const bool emptyAfterDel = !afterDel[0].filled;
  const bool deleteNoOp = !deleteSnapshot(1);  // second delete = no-op (already empty)

  const bool ok = grabbed && filledAfterGrab && activated && appliedOk && deleted &&
                  emptyAfterDel && deleteNoOp;
  std::printf("[selftest-variation-panel] GRAB/ACTIVATE/DELETE grab=%s filled=%s applied=%.4f(want "
              "%.4f) empty=%s -> %s\n",
              grabbed ? "y" : "n", filledAfterGrab ? "y" : "n", applied, wantApplied,
              emptyAfterDel ? "y" : "n", ok ? "PASS" : "FAIL");
  reset();
  return ok;
}

// GOLDEN B — N-way weighted mix wiring. Two snapshots: slot 1 (amount=4), slot 2 (amount=12). Weights
// (1, 3). Expected mix = Σ(v·w)/Σw = (4·1 + 12·3)/(1+3) = (4+36)/4 = 10. Applied as a graph override,
// readable back through effectiveInput, undo-able.
//   injectBug FLATTENS the weights to equal (1,1) -> machine mixes (4+12)/2 = 8 while the assertion
//   still pins the weighted 10 -> got(8) != want(10) -> RED (the weight wiring actually matters).
bool goldenNWayMix(bool injectBug) {
  reset();
  doc::g_lib() = buildLib();
  SymbolLibrary& lib = doc::g_lib();

  setAmount(lib, 4.0f);
  grabSnapshot(lib, 1);    // slot 1 captures amount=4
  setAmount(lib, 12.0f);
  grabSnapshot(lib, 2);    // slot 2 captures amount=12

  // Reset the live value to genuinely the default (0) — the mix must overwrite it (clear the override).
  {
    Symbol* s = lib.find("comp");
    if (SymbolChild* c = childById(*s, 1)) c->overrides.erase("amount");
  }

  setMixWeight(1, 1.0f);
  // injectBug FLATTENS weight 2 (3 -> 1): the machine then mixes (4+12)/2 = 8 while the assertion below
  // still pins the FIXED weighted answer 10 -> got(8) != want(10) -> this golden goes RED on the bug.
  setMixWeight(2, injectBug ? 1.0f : 3.0f);
  const bool applied = applyMix(lib);
  const float got = readAmount(lib);

  // Independent reference from variation_mix.h math (Σv·w/Σw): (4·1+12·3)/4 = 10. NOT recomputed for
  // the bug — the weighted answer is the same target either way; the bug must miss it.
  const float want = (4.0f * 1.0f + 12.0f * 3.0f) / (1.0f + 3.0f);  // = 10
  const bool mixOk = applied && std::fabs(got - want) < kTol && hasOverride(lib);

  // Undo restores the prior (no override -> back to default 0).
  g_commands.undo();
  const float afterUndo = readAmount(lib);
  const bool undoOk = std::fabs(afterUndo - 0.0f) < kTol && !hasOverride(lib);

  const bool ok = mixOk && undoOk;
  std::printf("[selftest-variation-panel] N-WAY-MIX (4·1+12·3)/4 got=%.4f want=%.4f | undo=%.4f -> "
              "%s\n",
              got, want, afterUndo, ok ? "PASS" : "FAIL");
  reset();
  return ok;
}

// GOLDEN C — full 2-way crossfader settles to the RIGHT endpoint (writeback reached the graph).
//   slot 1 (amount=0), slot 2 (amount=20). Arm left=1 right=2, drive fader to ~127, tick to settle.
//   The graph override must reach the RIGHT endpoint value (20), readable back through effectiveInput.
//   injectBug NEVER ticks (= the dark "spring spins but nothing writes back" state) -> the override is
//   frozen at the left (0). The assertion ALWAYS demands the right endpoint, so the bug goes RED.
bool goldenCrossfade(bool injectBug) {
  reset();
  doc::g_lib() = buildLib();
  SymbolLibrary& lib = doc::g_lib();

  setAmount(lib, 0.0f);
  grabSnapshot(lib, 1);    // left endpoint: amount=0
  setAmount(lib, 20.0f);
  grabSnapshot(lib, 2);    // right endpoint: amount=20
  setAmount(lib, 0.0f);    // start the live value at the left

  const bool armed = armCrossfade(lib, 1, 2);
  updateCrossfade(127.0f * 0.95f);  // strong move toward the right (mid, so the spring chases)

  // Fixed: tick the live pipe to settle -> the override damps to the right endpoint. Bug: never tick ->
  // frozen at the left (0).
  if (!injectBug)
    for (int i = 0; i < 600; ++i) tickCrossfade(lib);

  const float after = readAmount(lib);
  // The override must reach the RIGHT endpoint (Lerp(0,20,~0.95) ≈ 19). The bug never ticks -> after=0,
  // which fails this band -> RED. (No bug-specific expectation: the writeback either lands or it doesn't.)
  const bool reachedRight = after > 17.0f && after <= 20.0f + kTol;
  const bool ok = armed && reachedRight;
  std::printf("[selftest-variation-panel] CROSSFADE settle after=%.4f (want ~19..20, armed=%s) -> %s\n",
              after, armed ? "y" : "n", ok ? "PASS" : "FAIL");
  reset();
  return ok;
}

}  // namespace
}  // namespace sw::varpanel

namespace sw::varpanel {

int runVariationPanelSelfTest(bool injectBug) {
  // Preserve / restore the live document — the panel reads doc::g_lib() and the selftest swaps it. The
  // composition path must be at root so currentSymbolId() resolves to our tiny lib's rootId ("comp").
  SymbolLibrary saved = doc::g_lib();
  std::vector<int> savedPath = doc::g_compositionPath;
  doc::g_compositionPath.clear();
  bool ok = true;
  ok = goldenGrabActivateDelete(injectBug) && ok;
  ok = goldenNWayMix(injectBug) && ok;
  ok = goldenCrossfade(injectBug) && ok;
  doc::g_lib() = saved;
  doc::g_compositionPath = savedPath;
  reset();
  std::printf("[selftest-variation-panel] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw::varpanel

namespace sw {
// Self-register into the --selftest router. orderBase 306 appends right after variation-live (305).
REGISTER_SELFTESTS(/*orderBase=*/306,
    {"variation-panel", sw::varpanel::runVariationPanelSelfTest});
}  // namespace sw
