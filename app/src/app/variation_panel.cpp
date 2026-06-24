// app/variation_panel — the FULL Variation panel state + wiring (P2). Owns the snapshot pool (document-
// vocab DocVariation per slot), the per-slot mix weights, and the full 2-way crossfader. Drives g_lib
// through the document-override bridge (variation_apply.h) exactly like P1's live pipe, extended to the
// whole pool. The selftest (--selftest-variation-panel) lives at the tail (one file, one responsibility).
#include "app/variation_panel.h"

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "app/command.h"            // g_commands (settle commit / activate push)
#include "app/document.h"           // bumpLibRevision (live preview must dirty the projection)
#include "app/variation_apply.h"    // DocVariation / buildBlendTowardsVariationCommand / buildNWayMixCommand
#include "runtime/compound_graph.h"  // effectiveInput / SymbolLibrary / SlotDef / childById
#include "runtime/variation_crossfader.h"     // VariationCrossfader (the 1/60 spring)
#include "runtime/variation_pool.h"           // VariationPool / SnapshotChildState

namespace sw::varpanel {
namespace {

// The panel's snapshot pool, in document vocabulary (fork-pool-docvocab): per slotIndex the captured
// DocVariation (parameterSets[childId][slotId]=float) + its title. This is what the document-override
// bridge consumes. The runtime VariationPool below is driven in parallel purely as the snapshot_actions
// state machine + the crossfader's value-agnostic spring (one trivial scalar per slot) — same as P1.
struct StoredSnapshot {
  DocVariation doc;       // the document-vocab capture (what gets applied / mixed / crossfaded)
  std::string title;      // "Slot N" by default
  int paramCount = 0;     // tracked Float-param count (cell readout)
};

std::map<int, StoredSnapshot> g_pool;        // slotIndex -> stored snapshot (filled slots only)
std::map<int, float> g_weights;              // slotIndex -> N-way mix weight (0 = excluded)

// The runtime spring/active-state substrate. g_xfPool mirrors which slots are filled (a trivial scalar
// per slot) so VariationCrossfader has a valid left/right pair to spring between; g_xf owns the active
// snapshot + the damped weight. Reconstructed lazily when the crossfader is armed.
VariationPool g_xfPool;
std::unique_ptr<VariationCrossfader> g_xf;   // bound to g_xfPool (constructed on arm)

// Crossfader endpoint state (the document-vocab A/B, like P1's SliceState). The crossfader springs a
// scalar weight; the real per-param blend goes through the document bridge below.
struct CrossfadeState {
  bool armed = false;
  int leftSlot = -1;          // fader 0 (left/active)
  int rightSlot = -1;         // fader 127 (right/target)
  std::string compositionId;  // the composition the snapshots were captured against
  bool committed = false;     // a settle already pushed the undoable entry for the current move
};
CrossfadeState g_xfade;

// Capture the live composition's Float-slot effective values into a DocVariation (fork-pool-docvocab).
// = TiXL CreateOrUpdateSnapshotVariation, but in sw's float-per-slot vocabulary: every Float input of
// every child contributes (childId, slotId) -> effectiveInput. (sw has no per-child EnabledForSnapshots
// flag yet; all children participate — a named simplification of the TiXL filter, every node enabled.)
StoredSnapshot captureLive(SymbolLibrary& lib, const std::string& compositionId) {
  StoredSnapshot snap;
  Symbol* comp = lib.find(compositionId);
  if (!comp) return snap;
  for (const SymbolChild& child : comp->children) {
    const Symbol* def = lib.find(child.symbolId);
    if (!def) continue;
    for (const SlotDef& in : def->inputDefs) {
      if (in.dataType != "Float") continue;
      const float v = effectiveInput(lib, child, in.id, in.def);
      snap.doc.parameterSets[child.id][in.id] = v;
      ++snap.paramCount;
    }
  }
  return snap;
}

// Mirror a slot into the runtime VariationPool as a trivial scalar snapshot, so the crossfader's
// state machine has a real left/right pair (its blend value is read off dampedWeight() only; the doc
// blend is applied separately). One composition-bucket value per slot — value is irrelevant (the
// spring is value-agnostic), only the existence/activationIndex matters.
void mirrorSlotToXfPool(int slotIndex) {
  std::vector<SnapshotChildState> s(1);
  s[0].childId = kCompositionNode;
  s[0].values[1] = VariationValue::makeFloat(static_cast<float>(slotIndex));
  g_xfPool.createOrUpdateSnapshot(slotIndex, s, g_pool.count(slotIndex) ? g_pool[slotIndex].title : "");
}

// Apply a DocVariation fully (weight 1) to the live graph as undo-able overrides (= ACTIVATE). Reuses
// buildBlendTowardsVariationCommand at weight=1: Lerp(current, snapshotValue, 1) == snapshotValue, so
// every tracked param snaps to the stored value. Pushed on the command stack (undo-able). Returns
// false if nothing applied.
bool applyDocFully(SymbolLibrary& lib, const std::string& compositionId, const DocVariation& doc) {
  auto macro = buildBlendTowardsVariationCommand(lib, compositionId, doc, 1.0f);
  if (macro->empty()) return false;
  g_commands.push(std::move(macro));  // push() doIt()s + bumps revision + clears redo
  return true;
}

// The crossfade target snapshot in document vocab: the RIGHT endpoint's stored DocVariation. (Left is
// the anchor — we seed the override to the left value before each preview, so weight 0 == left exactly,
// like P1's anchor discipline.)
const DocVariation* rightDoc() {
  if (!g_xfade.armed) return nullptr;
  auto it = g_pool.find(g_xfade.rightSlot);
  return it == g_pool.end() ? nullptr : &it->second.doc;
}

// Seed the live graph to the LEFT endpoint's values (the weight-0 anchor). Writes each tracked Float
// param of the left snapshot as a direct override. Mirrors P1's applyLive anchor: weight 0 == left.
void anchorLeft(SymbolLibrary& lib) {
  auto it = g_pool.find(g_xfade.leftSlot);
  if (it == g_pool.end()) return;
  Symbol* comp = lib.find(g_xfade.compositionId);
  if (!comp) return;
  for (const auto& [childId, slots] : it->second.doc.parameterSets) {
    SymbolChild* c = childById(*comp, childId);
    if (!c) continue;
    for (const auto& [slotId, value] : slots) c->overrides[slotId] = value;
  }
}

// Live preview (non-settled): anchor left, then lerp toward the right snapshot by `weight`, applied as
// a transient override (NOT on the undo stack — live preview must not pollute undo, like P1).
void crossfadePreview(SymbolLibrary& lib, float weight) {
  const DocVariation* target = rightDoc();
  if (!target) return;
  anchorLeft(lib);  // weight 0 == left
  auto macro = buildBlendTowardsVariationCommand(lib, g_xfade.compositionId, *target, weight);
  if (macro->empty()) return;
  macro->doIt();
  doc::bumpLibRevision();  // live preview is a g_lib write outside the stack -> must dirty projection
}

}  // namespace

bool grabSnapshot(SymbolLibrary& lib, int slotIndex) {
  if (slotIndex < 1 || slotIndex > kSlotCount) return false;
  const std::string compId = doc::currentSymbolId();
  Symbol* comp = lib.find(compId);
  if (!comp) return false;
  StoredSnapshot snap = captureLive(lib, compId);
  snap.title = "Slot " + std::to_string(slotIndex);
  g_pool[slotIndex] = std::move(snap);
  mirrorSlotToXfPool(slotIndex);  // keep the spring pool's slot set in sync
  return true;
}

bool activateSnapshot(SymbolLibrary& lib, int slotIndex) {
  auto it = g_pool.find(slotIndex);
  if (it == g_pool.end()) return false;  // empty slot -> no-op (TiXL ActivateOrCreate would CAPTURE,
                                         // but the panel's Grab button owns capture; Activate applies)
  const std::string compId = doc::currentSymbolId();
  const bool applied = applyDocFully(lib, compId, it->second.doc);
  // Set this slot active on the crossfader state machine (left = index, right cleared). Lazily build
  // the spring pool if needed so setActiveSnapshot has a valid snapshot to point at.
  if (g_xfPool.tryGetSnapshot(slotIndex) == nullptr) mirrorSlotToXfPool(slotIndex);
  if (!g_xf) g_xf = std::make_unique<VariationCrossfader>(g_xfPool);
  g_xf->setActiveSnapshot(slotIndex);  // = SnapshotActions: left=index, active=left, right cleared
  g_xfade.armed = false;               // activating a single slot disarms any 2-way crossfade
  return applied;
}

bool deleteSnapshot(int slotIndex) {
  auto it = g_pool.find(slotIndex);
  if (it == g_pool.end()) return false;  // TiXL "No preset to delete" no-op
  g_pool.erase(it);
  g_weights.erase(slotIndex);
  g_xfPool.removeSnapshot(slotIndex);
  // If a crossfade endpoint was deleted, disarm (the endpoint dangles).
  if (g_xfade.armed && (g_xfade.leftSlot == slotIndex || g_xfade.rightSlot == slotIndex))
    g_xfade.armed = false;
  return true;
}

std::vector<SlotInfo> slots() {
  std::vector<SlotInfo> out;
  out.reserve(kSlotCount);
  const int activeIdx = g_xf ? g_xf->activeSnapshotIndex() : -1;
  for (int i = 1; i <= kSlotCount; ++i) {
    SlotInfo si;
    si.index = i;
    auto it = g_pool.find(i);
    if (it != g_pool.end()) {
      si.filled = true;
      si.title = it->second.title;
      si.paramCount = it->second.paramCount;
    }
    si.active = si.filled && i == activeIdx;
    out.push_back(std::move(si));
  }
  return out;
}

void setMixWeight(int slotIndex, float weight) {
  if (slotIndex < 1 || slotIndex > kSlotCount) return;
  g_weights[slotIndex] = weight < 0.0f ? 0.0f : weight;
}
float mixWeight(int slotIndex) {
  auto it = g_weights.find(slotIndex);
  return it == g_weights.end() ? 0.0f : it->second;
}

bool applyMix(SymbolLibrary& lib) {
  const std::string compId = doc::currentSymbolId();
  // Gather one DocMixNeighbour per FILLED slot with weight>0 (each snapshot at its weight). A slot with
  // weight 0 is excluded (it would contribute nothing once normalized — but more importantly the VJ
  // intent is "this snapshot is not in the mix").
  std::vector<DocMixNeighbour> neighbours;
  for (const auto& [slotIndex, snap] : g_pool) {
    const float w = mixWeight(slotIndex);
    if (w <= 0.0f) continue;
    neighbours.push_back(DocMixNeighbour{snap.doc, w});
  }
  if (neighbours.empty()) return false;  // nothing weighted -> nothing to mix
  auto macro = buildNWayMixCommand(lib, compId, neighbours);
  if (macro->empty()) return false;
  g_commands.push(std::move(macro));  // one undoable "Mix snapshots" edit
  return true;
}

bool armCrossfade(SymbolLibrary& lib, int leftSlot, int rightSlot) {
  if (g_pool.find(leftSlot) == g_pool.end() || g_pool.find(rightSlot) == g_pool.end()) return false;
  if (leftSlot == rightSlot) return false;  // a 2-way crossfade needs two distinct endpoints
  g_xfade.armed = true;
  g_xfade.leftSlot = leftSlot;
  g_xfade.rightSlot = rightSlot;
  g_xfade.compositionId = doc::currentSymbolId();
  g_xfade.committed = false;

  // Arm the spring: left active (fader 0), right is the blend target (fader 127). The spring is value-
  // agnostic — it only damps a scalar weight; the A/B blend lives in the document apply.
  if (g_xfPool.tryGetSnapshot(leftSlot) == nullptr) mirrorSlotToXfPool(leftSlot);
  if (g_xfPool.tryGetSnapshot(rightSlot) == nullptr) mirrorSlotToXfPool(rightSlot);
  g_xf = std::make_unique<VariationCrossfader>(g_xfPool);
  g_xf->setActiveSnapshot(leftSlot);       // left active (fader 0)
  g_xf->startBlendingTowards(rightSlot);   // right is the blend target (fader 127)
  (void)lib;
  return true;
}

bool crossfadeArmed() { return g_xfade.armed; }
int crossfadeLeft() { return g_xfade.armed ? g_xfade.leftSlot : -1; }
int crossfadeRight() { return g_xfade.armed ? g_xfade.rightSlot : -1; }

void updateCrossfade(float midiValue) {
  if (!g_xfade.armed || !g_xf) return;
  g_xf->updateFader(midiValue);
  g_xfade.committed = false;  // a fresh fader move re-opens the settle window
}

void tickCrossfade(SymbolLibrary& lib) {
  if (!g_xfade.armed || !g_xf) return;

  // Advance the spring ONE frame at the FIXED 1/60 step (NOT the real wall dt). The throwaway side-map
  // is unused; we read the dampedWeight and apply the blend through the document bridge below.
  LiveParams scratch;
  const float w = g_xf->tick(scratch);

  const bool settled = std::fabs(g_xf->dampingVelocity()) < VariationCrossfader::kSettleVelocity;

  if (settled && !g_xfade.committed) {
    // SETTLE COMMIT (= TiXL ApplyCurrentBlend): push ONE undoable entry capturing the final blend.
    const DocVariation* target = rightDoc();
    if (target) {
      anchorLeft(lib);
      auto macro = buildBlendTowardsVariationCommand(lib, g_xfade.compositionId, *target, w);
      if (!macro->empty()) g_commands.push(std::move(macro));
    }
    g_xfade.committed = true;
    return;
  }

  if (!g_xfade.committed) crossfadePreview(lib, w);
}

float crossfadeWeight() { return g_xf ? g_xf->dampedWeight() : 0.0f; }

void reset() {
  g_pool.clear();
  g_weights.clear();
  g_xfPool = VariationPool{};
  g_xf.reset();
  g_xfade = CrossfadeState{};
}

}  // namespace sw::varpanel
