// app/variation_live — the LIVE PERFORMANCE PIPE driver (P1). Owns the single P1 crossfader, advances
// its spring every frame with the FIXED 1/60 step (faithful TiXL BlendActions.cs:244), and writes the
// damped blend onto the graph override through the document bridge (variation_apply.h). The selftest
// (--selftest-variation-live) lives at the tail (one file, one responsibility; the seam's own harness).
#include "app/variation_live.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>

#include "app/command.h"            // g_commands (settle commit)
#include "app/document.h"           // bumpLibRevision (live preview must dirty the projection)
#include "app/variation_apply.h"    // buildBlendTowardsVariationCommand / DocVariation
#include "runtime/compound_graph.h"  // effectiveInput / SymbolLibrary / SlotDef
#include "runtime/variation_crossfader.h"  // VariationCrossfader (the 1/60 spring)
#include "runtime/variation_mix.h"         // springDamp (golden reference step)
#include "runtime/variation_pool.h"        // VariationPool / SnapshotChildState
#include "runtime/selftest_registry.h"     // REGISTER_SELFTESTS

namespace sw::varlive {
namespace {

// The single P1 slice state (one live pipe; the full pool is P2). The pool holds two trivial endpoints
// (A @ index 1, B @ index 2) and the crossfader springs between them. We use the crossfader purely as
// the FAITHFUL spring engine (its tick() hard-codes kTimeStep = 1/60); the BLEND VALUE we read off
// dampedWeight() and apply to the document — so the picture-facing half goes through the real
// buildBlendTowardsVariationCommand bridge, not the runtime side-map.
struct SliceState {
  bool armed = false;
  std::string compositionId;
  int childId = -1;
  std::string slotId;
  float valueA = 0.0f;   // A endpoint (fader 0)
  float valueB = 0.0f;   // B endpoint (fader 127)
  bool committed = false;  // a settle already pushed the undoable entry for the current target
};

VariationPool g_pool;
std::unique_ptr<VariationCrossfader> g_xf;  // bound to g_pool (constructed on seed)
SliceState g_slice;

// Build the document-vocabulary target snapshot for the B endpoint: (childId, slotId) -> valueB.
// (A is the starting point — Lerp(current, B, w) sweeps current→B as w: 0→1.)
DocVariation makeBTarget() {
  DocVariation t;
  t.parameterSets[g_slice.childId][g_slice.slotId] = g_slice.valueB;
  return t;
}

// Apply the blend at `weight` as a LIVE graph override (non-stacked preview, = TiXL
// BeginBlendTowardsSnapshot). buildBlendTowardsVariationCommand computes Lerp(current, B, weight) for
// the Float target and writes the override; doIt() mutates g_lib directly, so we bump the revision
// ourselves (the command stack would do it, but the live preview deliberately does NOT pollute undo).
//
// FAITHFUL FORK NOTE: TiXL's per-frame BeginBlendTowardsSnapshot lerps from the ACTIVE snapshot value
// (A), not the running override. We blend current→B; to keep A as the true weight-0 anchor we reset
// the override to A before each preview, so weight 0 == A and weight 1 == B exactly (the crossfade
// endpoints the golden pins). One write per frame, idempotent.
void applyLive(SymbolLibrary& lib, float weight) {
  // Anchor weight 0 at A: seed the override to A, then lerp toward B by weight.
  if (Symbol* comp = lib.find(g_slice.compositionId)) {
    if (SymbolChild* c = childById(*comp, g_slice.childId)) c->overrides[g_slice.slotId] = g_slice.valueA;
  }
  auto macro = buildBlendTowardsVariationCommand(lib, g_slice.compositionId, makeBTarget(), weight);
  if (macro->empty()) return;
  macro->doIt();
  doc::bumpLibRevision();  // live preview is a g_lib write outside the stack -> must dirty projection
}

}  // namespace

bool seedSliceTarget(SymbolLibrary& lib, const std::string& compositionSymbolId, int childId,
                     const std::string& slotId, float valueA, float valueB) {
  // Validate the target is a Float input of the child (only Float slots blend, = TiXL BlendMethods).
  Symbol* comp = lib.find(compositionSymbolId);
  if (!comp) return false;
  SymbolChild* child = childById(*comp, childId);
  if (!child) return false;
  const Symbol* def = lib.find(child->symbolId);
  if (!def) return false;
  bool isFloat = false;
  for (const SlotDef& in : def->inputDefs)
    if (in.id == slotId && in.dataType == "Float") { isFloat = true; break; }
  if (!isFloat) return false;

  g_slice.armed = true;
  g_slice.compositionId = compositionSymbolId;
  g_slice.childId = childId;
  g_slice.slotId = slotId;
  g_slice.valueA = valueA;
  g_slice.valueB = valueB;
  g_slice.committed = false;

  // Arm the crossfader's spring with two trivial endpoints (the spring is value-agnostic — it only
  // damps a scalar weight; the actual A/B blend lives in the document apply above).
  g_pool = VariationPool{};
  std::vector<SnapshotChildState> a(1), b(1);
  a[0].childId = kCompositionNode; a[0].values[1] = VariationValue::makeFloat(0.0f);
  b[0].childId = kCompositionNode; b[0].values[1] = VariationValue::makeFloat(1.0f);
  g_pool.createOrUpdateSnapshot(1, a, "A");
  g_pool.createOrUpdateSnapshot(2, b, "B");
  g_xf = std::make_unique<VariationCrossfader>(g_pool);
  g_xf->setActiveSnapshot(1);    // A active (fader 0)
  g_xf->startBlendingTowards(2);  // B is the blend target (fader 127)
  return true;
}

bool armed() { return g_slice.armed; }

void updateFader(float midiValue) {
  if (!g_slice.armed || !g_xf) return;
  g_xf->updateFader(midiValue);
  g_slice.committed = false;  // a fresh fader move re-opens the settle window
}

void tick(SymbolLibrary& lib) {
  if (!g_slice.armed || !g_xf) return;

  // Advance the spring ONE frame. tick() damps dampedWeight_ toward targetWeight_ with the FIXED
  // kTimeStep = 1/60 (variation_crossfader.h:122 / BlendActions.cs:244) — the REAL wall dt is NEVER
  // fed in here. We pass a throwaway side-map because we only want the spring's dampedWeight; the
  // graph-facing apply goes through the document bridge below.
  LiveParams scratch;
  const float w = g_xf->tick(scratch);

  // Has the spring settled this frame? (|vel| < kSettleVelocity — the crossfader's tick committed
  // any pending endpoint completion internally; here we mirror it for the undoable document commit.)
  const bool settled = std::fabs(g_xf->dampingVelocity()) < VariationCrossfader::kSettleVelocity;

  if (settled && !g_slice.committed) {
    // SETTLE COMMIT (= TiXL ApplyCurrentBlend): push ONE undoable entry capturing the final weight.
    // The macro re-applies the same Lerp(current, B, w); current is already A (live preview anchored
    // it), so this is the durable "blend towards snapshot" document edit. Skip empty (nothing blends).
    if (Symbol* comp = lib.find(g_slice.compositionId))
      if (SymbolChild* c = childById(*comp, g_slice.childId)) c->overrides[g_slice.slotId] = g_slice.valueA;
    auto macro = buildBlendTowardsVariationCommand(lib, g_slice.compositionId, makeBTarget(), w);
    if (!macro->empty()) g_commands.push(std::move(macro));  // push() bumps revision + clears redo
    g_slice.committed = true;
    return;
  }

  // Live preview (non-settled): apply the damped blend as a transient override + dirty the projection.
  if (!g_slice.committed) applyLive(lib, w);
}

float dampedWeight() { return g_xf ? g_xf->dampedWeight() : 0.0f; }
float targetWeight() { return g_xf ? g_xf->targetWeight() : 0.0f; }

float sliceOverrideValue(SymbolLibrary& lib) {
  if (!g_slice.armed) return -999.0f;
  Symbol* comp = lib.find(g_slice.compositionId);
  SymbolChild* c = comp ? childById(*comp, g_slice.childId) : nullptr;
  if (!c) return -999.0f;
  return effectiveInput(lib, *c, g_slice.slotId, -999.0f);
}

void reset() {
  g_slice = SliceState{};
  g_xf.reset();
  g_pool = VariationPool{};
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
//  --selftest-variation-live — the live-pipe golden (harness-first).
// ─────────────────────────────────────────────────────────────────────────────────────────────────
namespace {

constexpr float kTol = 1e-4f;

// One composition "comp" with one child (id 1) instancing an "Op" that has a Float input "amount"
// (def 0). The slice blends amount A=0 -> B=10.
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

// GOLDEN A — the FIXED 1/60 step (the parity LANDMINE): the per-tick spring delta must be the value of
// springDamp(target, 0, vel=0, 20, 1/60) — the production tick() must NEVER feed the real wall dt into
// the step. We pin the EXACT one-step dampedWeight against a direct springDamp call at timeStep = 1/60.
// injectBug recomputes the want with a 10x timeStep (= the divergence a real-dt feed of 10/60 causes);
// the production tick() — locked to 1/60 — then mismatches that bug want, so the bug leg FAILS.
bool goldenFixedStep(bool injectBug) {
  VariationPool pool;
  std::vector<SnapshotChildState> a(1), b(1);
  a[0].childId = kCompositionNode; a[0].values[1] = VariationValue::makeFloat(0.0f);
  b[0].childId = kCompositionNode; b[0].values[1] = VariationValue::makeFloat(1.0f);
  pool.createOrUpdateSnapshot(1, a, "A");
  pool.createOrUpdateSnapshot(2, b, "B");

  VariationCrossfader xf(pool);
  xf.setActiveSnapshot(1);
  xf.startBlendingTowards(2);
  const float tgt = xf.updateFader(127.0f * 0.5f);  // raw target weight (= 0.5)

  LiveParams scratch;
  const float got = xf.tick(scratch);  // production: exactly one springDamp at the FIXED 1/60

  // The reference one-step value, recomputed independently from springDamp. The fixed (correct) want
  // uses 1/60; the bug want uses 10/60 (real-dt-fed) — a strictly larger step. The production tick is
  // hard-locked to 1/60, so it equals the fixed want and DIFFERS from the bug want.
  float vel = 0.0f;
  const float wantFixed = springDamp(tgt, 0.0f, vel, VariationCrossfader::kSpringConstant,
                                     VariationCrossfader::kTimeStep);
  float velBug = 0.0f;
  const float wantBug = springDamp(tgt, 0.0f, velBug, VariationCrossfader::kSpringConstant,
                                   VariationCrossfader::kTimeStep * 10.0f);  // real-dt feed of 10/60
  const float want = injectBug ? wantBug : wantFixed;
  const bool ok = std::fabs(got - want) < kTol;
  std::printf("[selftest-variation-live] FIXED-1/60-STEP tick=%.6f want=%.6f (%s step) -> %s\n",
              got, want, injectBug ? "10x-dt BUG" : "fixed 1/60", ok ? "PASS" : "FAIL");
  return ok;
}

// GOLDEN B+C — settle + writeback: drive the fader to B (127) and tick until the spring settles; the
// slice override must reach the B endpoint (10) AND be readable back through effectiveInput (the GRAPH,
// not a side map). injectBug skips the writeback -> the override stays at A (0) -> bites.
bool goldenSettleWriteback(bool injectBug) {
  SymbolLibrary lib = buildLib();
  reset();
  const bool seeded = seedSliceTarget(lib, "comp", 1, "amount", /*A=*/0.0f, /*B=*/10.0f);

  const float before = sliceOverrideValue(lib);  // A side (0)
  updateFader(127.0f * 0.9f);  // strong move toward B (mid, so the spring chases without instant snap)

  // Tick many frames (1/60 each); the spring damps to ~0.9 then settles. We assert the final override
  // is near Lerp(0,10,~0.9)=~9 and STRICTLY greater than the A start — the pipe is live.
  float ov = before;
  for (int i = 0; i < 600; ++i) {
    if (injectBug) {
      // BUG: tick the spring but NEVER write back (the dark pre-P1 state) -> override frozen at A.
      LiveParams scratch; g_xf->tick(scratch);
    } else {
      tick(lib);
    }
    ov = sliceOverrideValue(lib);
  }

  const float after = sliceOverrideValue(lib);
  const bool changed = std::fabs(after - before) > 1.0f;        // C: the override actually moved
  const bool reachedB = !injectBug && after > 8.0f && after <= 10.0f + kTol;  // B: near the B endpoint
  const bool ok = injectBug ? (std::fabs(after - 0.0f) < kTol)   // bug: frozen at A -> detected
                            : (seeded && changed && reachedB);
  std::printf("[selftest-variation-live] SETTLE+WRITEBACK before=%.4f after=%.4f (want ~9..10, "
              "changed=%s) -> %s\n",
              before, after, changed ? "y" : "n", ok ? "PASS" : "FAIL");
  reset();
  return ok;
}

}  // namespace

int runVariationLiveSelfTest(bool injectBug) {
  bool ok = true;
  ok = goldenFixedStep(injectBug) && ok;
  ok = goldenSettleWriteback(injectBug) && ok;
  std::printf("[selftest-variation-live] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw::varlive

namespace sw {
// Self-register into the --selftest router. orderBase 305 appends right after variation-apply (304).
REGISTER_SELFTESTS(/*orderBase=*/305,
    {"variation-live", sw::varlive::runVariationLiveSelfTest});
}  // namespace sw
