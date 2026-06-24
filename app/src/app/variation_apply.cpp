// app/variation_apply — document-override bridge impl (Lane L1). Builds the "Blend towards snapshot"
// MacroCommand of SetOverrideCommands, faithful to TiXL TryCreateBlendTowardsVariationCommand. The
// selftest (--selftest-variation-apply) lives at the tail (one file, one responsibility; the selftest
// is this seam's own harness so it stays co-located like the runtime goldens).
#include "app/variation_apply.h"

#include <cmath>
#include <cstdio>

#include <vector>

#include "app/graph_commands.h"     // SetOverrideCommand
#include "runtime/variation_crossfader.h"  // VariationCrossfader (NIT 2 proof)
#include "runtime/variation_mix.h"         // mixFloat / MixNeighbour (N-way Mix)
#include "runtime/variation_pool.h"        // VariationPool / SnapshotChildState (NIT 2 proof)
#include "runtime/selftest_registry.h"     // REGISTER_SELFTESTS

namespace sw {
namespace {

// TiXL MathUtils.Lerp(a,b,t)=a+(b-a)*t — the same per-float blend the runtime VariationValue uses.
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

}  // namespace

std::unique_ptr<MacroCommand> buildBlendTowardsVariationCommand(SymbolLibrary& lib,
                                                                const std::string& compositionSymbolId,
                                                                const DocVariation& target, float weight) {
  auto macro = std::make_unique<MacroCommand>("Blend towards snapshot");
  Symbol* comp = lib.find(compositionSymbolId);
  if (!comp) return macro;  // missing composition -> empty macro, caller skips push

  // foreach child in composition.Children (TiXL SymbolVariationPool.cs:604).
  for (const SymbolChild& child : comp->children) {
    const Symbol* def = lib.find(child.symbolId);
    if (!def) continue;  // unresolved instance -> no inputs to blend

    // foreach inputSlot in child.Inputs (TiXL .cs:609). sw's inputs ARE the referenced symbol's
    // inputDefs; only Float slots carry a BlendMethod (the float value rail) — String / Points /
    // Texture2D / Command slots have no numeric blend, so they are skipped (= TiXL skipping a type
    // with no ValueUtils.BlendMethods entry).
    for (const SlotDef& in : def->inputDefs) {
      if (in.dataType != "Float") continue;  // no BlendMethod for non-float slots

      const float current = effectiveInput(lib, child, in.id, in.def);  // input.Value
      // input.IsDefault == the child has NO override for this slot (sparse-override model).
      const bool hasOverride = child.overrides.count(in.id) > 0;
      const bool hadOld = hasOverride;
      const float oldV = hasOverride ? child.overrides.at(in.id) : in.def;

      float mixed;
      if (const float* tracked = target.find(child.id, in.id)) {
        // TRACKED input: Lerp(current, snapshotValue, weight) (TiXL .cs:620).
        mixed = lerp(current, *tracked, weight);
      } else if (hasOverride) {
        // NIT 1 — UNTRACKED but NON-DEFAULT (input.IsDefault==false): Lerp(current, DefaultValue,
        // weight) (TiXL .cs:625-627). Pull a manually-overridden input back toward its definition
        // default by the blend weight. An input already at default (no override) is skipped entirely.
        mixed = lerp(current, in.def, weight);
      } else {
        continue;  // untracked AND at default -> no command (TiXL emits nothing for this input)
      }

      macro->add(std::make_unique<SetOverrideCommand>(lib, compositionSymbolId, child.id, in.id,
                                                      hadOld, oldV, mixed));
    }
  }
  return macro;
}

std::unique_ptr<MacroCommand> buildNWayMixCommand(SymbolLibrary& lib,
                                                  const std::string& compositionSymbolId,
                                                  const std::vector<DocMixNeighbour>& neighbours) {
  auto macro = std::make_unique<MacroCommand>("Mix snapshots");
  Symbol* comp = lib.find(compositionSymbolId);
  if (!comp) return macro;  // missing composition -> empty macro, caller skips push

  // foreach child / foreach Float input slot — same traversal as the 2-way cousin (only Float slots
  // carry a BlendMethod; vec inputs are already split into per-component Float slots, fork-vec-as-
  // float-slots). For each slot we build the N-way mixFloat over the neighbours and emit one override.
  for (const SymbolChild& child : comp->children) {
    const Symbol* def = lib.find(child.symbolId);
    if (!def) continue;

    for (const SlotDef& in : def->inputDefs) {
      if (in.dataType != "Float") continue;

      const float current = effectiveInput(lib, child, in.id, in.def);  // missing-neighbour fallback

      // Gather one MixNeighbour per neighbour: present iff that snapshot tracks (childId, slotId);
      // missing → present=false → mixFloat substitutes `current` AT this neighbour's weight (faithful
      // to TiXL Mix's matchingParam = param.InputSlot.Input.Value fallback). `anyTracked` records
      // whether at least one neighbour carries this slot — drives the skip below.
      std::vector<MixNeighbour> nbs;
      nbs.reserve(neighbours.size());
      bool anyTracked = false;
      for (const DocMixNeighbour& dn : neighbours) {
        const float* tracked = dn.snapshot.find(child.id, in.id);
        if (tracked) anyTracked = true;
        nbs.push_back(MixNeighbour{tracked ? *tracked : current, dn.weight, tracked != nullptr});
      }

      const bool hasOverride = child.overrides.count(in.id) > 0;
      // Skip a slot NO neighbour tracks AND that has no override: every neighbour contributes `current`
      // at its weight → the normalized average is exactly `current` → writing it would be a no-op
      // override (and on a degenerate all-zero-weight set mixFloat returns current too). No dead command
      // — same discipline as the 2-way cousin skipping untracked-at-default inputs.
      if (!anyTracked && !hasOverride) continue;

      const float mixed = mixFloat(nbs, current);
      const bool hadOld = hasOverride;
      const float oldV = hasOverride ? child.overrides.at(in.id) : in.def;
      macro->add(std::make_unique<SetOverrideCommand>(lib, compositionSymbolId, child.id, in.id,
                                                      hadOld, oldV, mixed));
    }
  }
  return macro;
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
//  --selftest-variation-apply — the document-override golden (harness-first).
// ─────────────────────────────────────────────────────────────────────────────────────────────────
namespace {

constexpr float kTol = 1e-4f;

// Build a tiny lib: one composition symbol "comp" containing one child (id 1) that instantiates an
// atomic "Op" with three Float inputs {a(def 0), b(def 0), c(def 7)}. Returns the lib by value.
SymbolLibrary buildLib() {
  SymbolLibrary lib;
  Symbol op;
  op.id = "Op";
  op.name = "Op";
  op.atomic = true;
  op.inputDefs = {{"a", "A", "Float", 0.0f, "", 0, 0},
                  {"b", "B", "Float", 0.0f, "", 0, 0},
                  {"c", "C", "Float", 7.0f, "", 0, 0}};
  lib.symbols["Op"] = op;

  Symbol comp;
  comp.id = "comp";
  comp.name = "comp";
  comp.atomic = false;
  SymbolChild child;
  child.id = 1;
  child.symbolId = "Op";
  comp.children.push_back(child);
  comp.nextChildId = 2;
  lib.symbols["comp"] = comp;
  lib.rootId = "comp";
  return lib;
}

float readOverride(SymbolLibrary& lib, const std::string& slotId) {
  Symbol* s = lib.find("comp");
  SymbolChild* c = s ? childById(*s, 1) : nullptr;
  if (!c) return -999.0f;
  return effectiveInput(lib, *c, slotId, -999.0f);
}

bool hasOverrideKey(SymbolLibrary& lib, const std::string& slotId) {
  Symbol* s = lib.find("comp");
  SymbolChild* c = s ? childById(*s, 1) : nullptr;
  return c && c->overrides.count(slotId) > 0;
}

// GOLDEN A — document-override blend at weight=0.5 + undo restores prior.
// Current: a overridden to 10 (so it has an override), b at default 0, c at default 7.
// Target snapshot tracks a=100, b=20. (c untracked, at default -> skipped.)
//   weight=0.5:
//     a (tracked):       Lerp(10, 100, .5)        = 55   -> override written
//     b (tracked):       Lerp(0,  20,  .5)        = 10   -> override written
//     c (untracked,def): skipped (no override)
//   All readable back through effectiveInput (the GRAPH), not a side map.
//   Undo: a restored to 10 (had old), b override ERASED (no old) -> back to default 0.
bool goldenDocOverride(bool injectBug) {
  SymbolLibrary lib = buildLib();
  {
    Symbol* s = lib.find("comp");
    childById(*s, 1)->overrides["a"] = 10.0f;  // a has a manual override = 10
  }
  DocVariation target;
  target.parameterSets[1]["a"] = 100.0f;
  target.parameterSets[1]["b"] = 20.0f;

  auto macro = buildBlendTowardsVariationCommand(lib, "comp", target, 0.5f);
  macro->doIt();

  const float gotA = readOverride(lib, "a");
  const float gotB = readOverride(lib, "b");
  const float gotC = readOverride(lib, "c");
  // ★ injectBug expects a=10 (the pre-blend value) -> bites the real document override (now 55).
  const float wantA = injectBug ? 10.0f : 55.0f;
  const bool blendOk = std::fabs(gotA - wantA) < kTol &&
                       std::fabs(gotB - 10.0f) < kTol &&
                       std::fabs(gotC - 7.0f) < kTol &&            // c untouched (still default)
                       !hasOverrideKey(lib, "c");                   // c got no override command

  macro->undo();
  const float undoA = readOverride(lib, "a");
  const float undoB = readOverride(lib, "b");
  const bool undoOk = std::fabs(undoA - 10.0f) < kTol &&          // a restored to its prior override
                      std::fabs(undoB - 0.0f) < kTol &&            // b back to default
                      hasOverrideKey(lib, "a") &&                  // a's override key restored
                      !hasOverrideKey(lib, "b");                   // b's override key erased (had none)

  const bool ok = blendOk && undoOk;
  std::printf("[selftest-variation-apply] DOC-OVERRIDE w=0.5 a=%.4f(want %.4f) b=%.4f c=%.4f | "
              "undo a=%.4f b=%.4f -> %s\n",
              gotA, wantA, gotB, gotC, undoA, undoB, ok ? "PASS" : "FAIL");
  return ok;
}

// GOLDEN B — NIT 1: an UNTRACKED but NON-DEFAULT input pulls toward its DefaultValue.
// c has def 7. Override c to 27 (non-default). Target tracks NOTHING for c. weight=0.5.
//   c (untracked, !IsDefault): Lerp(27, 7, .5) = 17.
// If NIT 1 were not closed, c would be left at 27 (untouched).
bool goldenNit1(bool injectBug) {
  SymbolLibrary lib = buildLib();
  {
    Symbol* s = lib.find("comp");
    childById(*s, 1)->overrides["c"] = 27.0f;  // c manually overridden to 27 (def is 7)
  }
  DocVariation target;  // tracks nothing for child 1

  auto macro = buildBlendTowardsVariationCommand(lib, "comp", target, 0.5f);
  macro->doIt();

  const float gotC = readOverride(lib, "c");
  // ★ injectBug expects 27 (the "leaf-left-untouched" behavior) -> bites the real NIT-1 pull (17).
  const float wantC = injectBug ? 27.0f : 17.0f;
  // a and b were at default (no override, untracked) -> must remain unwritten.
  const bool ok = std::fabs(gotC - wantC) < kTol &&
                  !hasOverrideKey(lib, "a") && !hasOverrideKey(lib, "b");
  std::printf("[selftest-variation-apply] NIT1 untracked-non-default c: Lerp(27,7,.5)=%.4f "
              "(want %.4f) a/b untouched=%s -> %s\n",
              gotC, wantC, (!hasOverrideKey(lib, "a") && !hasOverrideKey(lib, "b")) ? "y" : "n",
              ok ? "PASS" : "FAIL");
  return ok;
}

// GOLDEN C — NIT 2: the crossfader does NOT zero dampingVelocity when re-targeting. Drive the spring
// a few frames so velocity is non-zero, then call startBlendingTowards AGAIN (same target). Faithful
// TiXL: velocity is NEVER zeroed in StartBlendTo -> the spring keeps its momentum. The pre-NIT2 leaf
// zeroed velocity -> a discontinuous re-jolt.
bool goldenNit2(bool injectBug) {
  VariationPool pool;
  // Two trivial snapshots so the crossfader has a valid left/right pair.
  std::vector<SnapshotChildState> a(1), b(1);
  a[0].childId = kCompositionNode; a[0].values[1] = VariationValue::makeFloat(0.0f);
  b[0].childId = kCompositionNode; b[0].values[1] = VariationValue::makeFloat(100.0f);
  pool.createOrUpdateSnapshot(1, a, "A");
  pool.createOrUpdateSnapshot(2, b, "B");

  VariationCrossfader xf(pool);
  xf.setActiveSnapshot(1);
  xf.startBlendingTowards(2);
  xf.updateFader(64.0f);  // mid fader -> spring has a target to chase

  LiveParams live;
  live[kCompositionNode][1] = VariationValue::makeFloat(0.0f);
  for (int i = 0; i < 5; ++i) xf.tick(live);  // spin up the spring -> velocity != 0
  const float velBefore = xf.dampingVelocity();

  // Re-target the SAME index (2). NIT 2: velocity must survive (NOT be zeroed).
  xf.startBlendingTowards(2);
  const float velAfter = xf.dampingVelocity();

  // ★ injectBug emulates the OLD (pre-NIT2) behavior expectation: velocity zeroed -> velAfter==0.
  const bool velCarried = std::fabs(velAfter - velBefore) < 1e-9f && std::fabs(velBefore) > 1e-6f;
  const bool ok = injectBug ? false : velCarried;
  std::printf("[selftest-variation-apply] NIT2 re-target keeps velocity: before=%.6f after=%.6f "
              "carried=%s -> %s\n",
              velBefore, velAfter, velCarried ? "y" : "n", ok ? "PASS" : "FAIL");
  return ok;
}

}  // namespace

int runVariationApplySelfTest(bool injectBug) {
  bool ok = true;
  ok = goldenDocOverride(injectBug) && ok;
  ok = goldenNit1(injectBug) && ok;
  ok = goldenNit2(injectBug) && ok;
  std::printf("[selftest-variation-apply] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw

namespace sw {
// Self-register into the --selftest router. orderBase 304 appends right after variation (303).
REGISTER_SELFTESTS(/*orderBase=*/304,
    {"variation-apply", runVariationApplySelfTest});
}  // namespace sw
