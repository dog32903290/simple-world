// app/variation_panel — the FULL Variation panel state + wiring (P2 + multi-pool). Owns the
// PER-COMPOSITION snapshot pools (document-vocab DocSnapshot per slot, keyed by composition symbol —
// TiXL's one-SymbolVariationPool-per-symbol model), the per-composition mix weights, the full 2-way
// crossfader, and the .swproj "variationPools" embed/adopt persistence hooks document_io calls. Drives
// g_lib through the document-override bridge (variation_apply.h) exactly like P1's live pipe.
#include "app/variation_panel.h"

#include <cmath>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "app/command.h"            // g_commands (settle commit / activate push)
#include "app/document.h"           // bumpLibRevision (live preview must dirty the projection)
#include "app/variation_apply.h"    // DocVariation / buildBlendTowardsVariationCommand / buildNWayMixCommand
#include "crude_json.h"              // .swproj embed/adopt (the same serializer the doc rail uses)
#include "runtime/compound_graph.h"  // effectiveInput / SymbolLibrary / SlotDef / childById
#include "runtime/variation_crossfader.h"     // VariationCrossfader (the 1/60 spring)
#include "runtime/variation_pool.h"           // VariationPool / SnapshotChildState
#include "runtime/variation_pool_set.h"       // VariationPoolSet / DocSnapshot (per-composition pools)

namespace sw::varpanel {
namespace {

// The panel's snapshot pools — ONE PER COMPOSITION SYMBOL, exactly TiXL's model: entering a
// composition switches which pool the grid reads (VariationHandling.cs:76 `ActivePoolForSnapshots =
// GetOrLoadVariations(activeCompositionInstance.Symbol.Id)`). Each pool stores document-vocab
// DocSnapshots (parameterSets[childId][slotId]=float — fork-pool-docvocab); the keyed set lives in
// runtime (variation_pool_set.h) and is what persists into the .swproj ("variationPools" root key).
VariationPoolSet g_pools;

// Per-composition N-way mix weights (UI state, NOT persisted): [compositionSymbolId][slotIndex]=w.
std::map<std::string, std::map<int, float>> g_weights;

// The runtime spring/active-state substrate, per composition. Each mirror pool holds one trivial
// scalar per FILLED slot so VariationCrossfader has a valid left/right pair to spring between; g_xf
// owns the active snapshot + damped weight and is bound to ONE composition's mirror at a time
// (g_xfComp — the sw shape of TiXL's single ActivePoolForSnapshots). std::map node stability keeps
// the crossfader's pool reference valid while other compositions' mirrors grow.
std::map<std::string, VariationPool> g_xfPools;
std::unique_ptr<VariationCrossfader> g_xf;   // bound to g_xfPools[g_xfComp]
std::string g_xfComp;                        // which composition's pool g_xf is bound to

// Selftest seams (--selftest-variation-multipool 真注入; production never flips these).
bool g_debugSinglePoolKey = false;  // tooth 1 bug: every composition shares ONE pool key (混池)
bool g_debugSkipPoolAdopt = false;  // tooth 2 bug: load skips the variation section (池空)

// WHICH pool the panel reads/writes right now = the current composition symbol (TiXL's active-pool
// switch). The tooth-1 seam degenerates this to a constant — the exact "flat single pool" bug the
// multi-pool tooth must bite.
std::string poolKey() {
  return g_debugSinglePoolKey ? std::string("<single-pool-bug>") : doc::currentSymbolId();
}

// Crossfader endpoint state (the document-vocab A/B, like P1's SliceState). The crossfader springs a
// scalar weight; the real per-param blend goes through the document bridge below.
struct CrossfadeState {
  bool armed = false;
  int leftSlot = -1;          // fader 0 (left/active)
  int rightSlot = -1;         // fader 127 (right/target)
  std::string compositionId;  // the composition the snapshots were captured against
  std::string poolKey;        // the pool the endpoints live in (== compositionId in production)
  bool committed = false;     // a settle already pushed the undoable entry for the current move
};
CrossfadeState g_xfade;

// A stored DocSnapshot lifted into the document bridge's vocabulary (same map shape, direct copy).
DocVariation docOf(const DocSnapshot& s) {
  DocVariation d;
  d.parameterSets = s.parameterSets;
  return d;
}

// TiXL SymbolUi.Child.cs:50-56 — EnabledForSnapshots == (SnapshotGroupIndex == 1). 0 = not relevant
// (skip), 1 = used for snapshots, >1 = ParameterCollections (NOT the snapshot path). The snapshot
// filter (VariationHandling.cs:159) captures ONLY EnabledForSnapshots children, so only ==1 participates.
constexpr int kSnapshotGroupEnabled = 1;

// Capture the live composition's Float-slot effective values into a DocVariation (fork-pool-docvocab).
// = TiXL CreateOrUpdateSnapshotVariation, but in sw's float-per-slot vocabulary: every Float input of
// every SNAPSHOT-ENABLED child contributes (childId, slotId) -> effectiveInput. The EnabledForSnapshots
// filter (VariationHandling.cs:159: `if (!symbolChildUi.EnabledForSnapshots) continue;`) is honoured
// here via SymbolChild.snapshotGroupIndex (backing field, already persisted) — a child with
// snapshotGroupIndex != 1 is skipped entirely, exactly like TiXL's AddSnapshotEnabledChildrenToList.
DocSnapshot captureLive(SymbolLibrary& lib, const std::string& compositionId) {
  DocSnapshot snap;
  Symbol* comp = lib.find(compositionId);
  if (!comp) return snap;
  for (const SymbolChild& child : comp->children) {
    if (child.snapshotGroupIndex != kSnapshotGroupEnabled) continue;  // TiXL: skip !EnabledForSnapshots
    const Symbol* def = lib.find(child.symbolId);
    if (!def) continue;
    for (const SlotDef& in : def->inputDefs) {
      if (in.dataType != "Float") continue;
      const float v = effectiveInput(lib, child, in.id, in.def);
      snap.parameterSets[child.id][in.id] = v;
    }
  }
  return snap;
}

// Mirror a slot into `key`'s runtime VariationPool as a trivial scalar snapshot, so the crossfader's
// state machine has a real left/right pair (its blend value is read off dampedWeight() only; the doc
// blend is applied separately). One composition-bucket value per slot — value is irrelevant (the
// spring is value-agnostic), only the existence/activationIndex matters.
void mirrorSlotToXfPool(const std::string& key, int slotIndex) {
  std::vector<SnapshotChildState> s(1);
  s[0].childId = kCompositionNode;
  s[0].values[1] = VariationValue::makeFloat(static_cast<float>(slotIndex));
  std::string title;
  if (const DocSnapshotPool* pool = g_pools.find(key)) {
    auto it = pool->find(slotIndex);
    if (it != pool->end()) title = it->second.title;
  }
  g_xfPools[key].createOrUpdateSnapshot(slotIndex, s, title);
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

// The crossfade target snapshot: the RIGHT endpoint's stored DocSnapshot, out of the pool the
// crossfade was ARMED on (g_xfade.poolKey — not the pool of wherever the user navigated to since).
// (Left is the anchor — we seed the override to the left value before each preview, so weight 0 ==
// left exactly, like P1's anchor discipline.)
const DocSnapshot* rightSnap() {
  if (!g_xfade.armed) return nullptr;
  const DocSnapshotPool* pool = g_pools.find(g_xfade.poolKey);
  if (!pool) return nullptr;
  auto it = pool->find(g_xfade.rightSlot);
  return it == pool->end() ? nullptr : &it->second;
}

// Seed the live graph to the LEFT endpoint's values (the weight-0 anchor). Writes each tracked Float
// param of the left snapshot as a direct override. Mirrors P1's applyLive anchor: weight 0 == left.
void anchorLeft(SymbolLibrary& lib) {
  const DocSnapshotPool* pool = g_pools.find(g_xfade.poolKey);
  if (!pool) return;
  auto it = pool->find(g_xfade.leftSlot);
  if (it == pool->end()) return;
  Symbol* comp = lib.find(g_xfade.compositionId);
  if (!comp) return;
  for (const auto& [childId, slots] : it->second.parameterSets) {
    SymbolChild* c = childById(*comp, childId);
    if (!c) continue;
    for (const auto& [slotId, value] : slots) c->overrides[slotId] = value;
  }
}

// Live preview (non-settled): anchor left, then lerp toward the right snapshot by `weight`, applied as
// a transient override (NOT on the undo stack — live preview must not pollute undo, like P1).
void crossfadePreview(SymbolLibrary& lib, float weight) {
  const DocSnapshot* target = rightSnap();
  if (!target) return;
  anchorLeft(lib);  // weight 0 == left
  auto macro = buildBlendTowardsVariationCommand(lib, g_xfade.compositionId, docOf(*target), weight);
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
  DocSnapshot snap = captureLive(lib, compId);
  snap.title = "Slot " + std::to_string(slotIndex);
  const std::string key = poolKey();
  g_pools.poolFor(key)[slotIndex] = std::move(snap);  // the CURRENT composition's pool (TiXL active pool)
  mirrorSlotToXfPool(key, slotIndex);  // keep the spring pool's slot set in sync
  return true;
}

bool activateSnapshot(SymbolLibrary& lib, int slotIndex) {
  const std::string key = poolKey();
  const DocSnapshotPool* pool = g_pools.find(key);
  auto it = pool ? pool->find(slotIndex) : DocSnapshotPool::const_iterator();
  if (!pool || it == pool->end()) return false;  // empty slot -> no-op (TiXL ActivateOrCreate would
                                                 // CAPTURE, but the panel's Grab button owns capture)
  const std::string compId = doc::currentSymbolId();
  const bool applied = applyDocFully(lib, compId, docOf(it->second));
  // Set this slot active on the crossfader state machine (left = index, right cleared). The crossfader
  // rebinds when the composition changed — the sw shape of TiXL's ActivePoolForSnapshots switch.
  VariationPool& xfp = g_xfPools[key];
  if (xfp.tryGetSnapshot(slotIndex) == nullptr) mirrorSlotToXfPool(key, slotIndex);
  if (!g_xf || g_xfComp != key) {
    g_xf = std::make_unique<VariationCrossfader>(xfp);
    g_xfComp = key;
  }
  g_xf->setActiveSnapshot(slotIndex);  // = SnapshotActions: left=index, active=left, right cleared
  g_xfade.armed = false;               // activating a single slot disarms any 2-way crossfade
  return applied;
}

bool deleteSnapshot(int slotIndex) {
  const std::string key = poolKey();
  DocSnapshotPool& pool = g_pools.poolFor(key);
  auto it = pool.find(slotIndex);
  if (it == pool.end()) return false;  // TiXL "No preset to delete" no-op
  pool.erase(it);
  g_weights[key].erase(slotIndex);
  g_xfPools[key].removeSnapshot(slotIndex);
  // If a crossfade endpoint IN THIS POOL was deleted, disarm (the endpoint dangles). Another
  // composition's same-numbered slot must not disarm this pool's crossfade.
  if (g_xfade.armed && g_xfade.poolKey == key &&
      (g_xfade.leftSlot == slotIndex || g_xfade.rightSlot == slotIndex))
    g_xfade.armed = false;
  return true;
}

std::vector<SlotInfo> slots() {
  std::vector<SlotInfo> out;
  out.reserve(kSlotCount);
  const std::string key = poolKey();
  const DocSnapshotPool* pool = g_pools.find(key);
  // The active-dot only paints on the composition the crossfader is bound to (g_xfComp) — slot 1
  // active in composition A must not light slot 1 in composition B's grid.
  const int activeIdx = (g_xf && g_xfComp == key) ? g_xf->activeSnapshotIndex() : -1;
  for (int i = 1; i <= kSlotCount; ++i) {
    SlotInfo si;
    si.index = i;
    if (pool) {
      auto it = pool->find(i);
      if (it != pool->end()) {
        si.filled = true;
        si.title = it->second.title;
        si.paramCount = it->second.paramCount();
      }
    }
    si.active = si.filled && i == activeIdx;
    out.push_back(std::move(si));
  }
  return out;
}

void setMixWeight(int slotIndex, float weight) {
  if (slotIndex < 1 || slotIndex > kSlotCount) return;
  g_weights[poolKey()][slotIndex] = weight < 0.0f ? 0.0f : weight;
}
float mixWeight(int slotIndex) {
  auto wit = g_weights.find(poolKey());
  if (wit == g_weights.end()) return 0.0f;
  auto it = wit->second.find(slotIndex);
  return it == wit->second.end() ? 0.0f : it->second;
}

bool applyMix(SymbolLibrary& lib) {
  const std::string compId = doc::currentSymbolId();
  const DocSnapshotPool* pool = g_pools.find(poolKey());
  if (!pool) return false;
  // Gather one DocMixNeighbour per FILLED slot with weight>0 (each snapshot at its weight). A slot with
  // weight 0 is excluded (it would contribute nothing once normalized — but more importantly the VJ
  // intent is "this snapshot is not in the mix").
  std::vector<DocMixNeighbour> neighbours;
  for (const auto& [slotIndex, snap] : *pool) {
    const float w = mixWeight(slotIndex);
    if (w <= 0.0f) continue;
    neighbours.push_back(DocMixNeighbour{docOf(snap), w});
  }
  if (neighbours.empty()) return false;  // nothing weighted -> nothing to mix
  auto macro = buildNWayMixCommand(lib, compId, neighbours);
  if (macro->empty()) return false;
  g_commands.push(std::move(macro));  // one undoable "Mix snapshots" edit
  return true;
}

bool armCrossfade(SymbolLibrary& lib, int leftSlot, int rightSlot) {
  const std::string key = poolKey();
  const DocSnapshotPool* pool = g_pools.find(key);
  if (!pool || pool->find(leftSlot) == pool->end() || pool->find(rightSlot) == pool->end())
    return false;
  if (leftSlot == rightSlot) return false;  // a 2-way crossfade needs two distinct endpoints
  g_xfade.armed = true;
  g_xfade.leftSlot = leftSlot;
  g_xfade.rightSlot = rightSlot;
  g_xfade.compositionId = doc::currentSymbolId();
  g_xfade.poolKey = key;
  g_xfade.committed = false;

  // Arm the spring: left active (fader 0), right is the blend target (fader 127). The spring is value-
  // agnostic — it only damps a scalar weight; the A/B blend lives in the document apply.
  VariationPool& xfp = g_xfPools[key];
  if (xfp.tryGetSnapshot(leftSlot) == nullptr) mirrorSlotToXfPool(key, leftSlot);
  if (xfp.tryGetSnapshot(rightSlot) == nullptr) mirrorSlotToXfPool(key, rightSlot);
  g_xf = std::make_unique<VariationCrossfader>(xfp);
  g_xfComp = key;
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
    const DocSnapshot* target = rightSnap();
    if (target) {
      anchorLeft(lib);
      auto macro = buildBlendTowardsVariationCommand(lib, g_xfade.compositionId, docOf(*target), w);
      if (!macro->empty()) g_commands.push(std::move(macro));
    }
    g_xfade.committed = true;
    return;
  }

  if (!g_xfade.committed) crossfadePreview(lib, w);
}

float crossfadeWeight() { return g_xf ? g_xf->dampedWeight() : 0.0f; }

void reset() {
  g_pools.clear();
  g_weights.clear();
  g_xfPools.clear();
  g_xf.reset();
  g_xfComp.clear();
  g_xfade = CrossfadeState{};
}

// ── Multi-pool persistence (.swproj "variationPools" root key; variation_pool_set.h format) ─────
// TiXL persists one variations/{symbolId}.var per composition pool (SymbolVariationPool.cs:148-150);
// sw's single-file document embeds every NON-EMPTY pool under one additive root key instead
// (fork-poolset-single-file). document_io keeps one-line hooks; the persistence meat lives here.
bool writeProjectFileWithPools(const std::string& path, const std::string& libJson) {
  std::string out = libJson;
  if (g_pools.anyNonEmpty()) {  // pool-less project: bytes unchanged vs the old writer
    crude_json::value root = crude_json::value::parse(libJson);
    if (root.is_object()) {
      root["variationPools"] = variationPoolSetToJsonValue(g_pools);
      out = root.dump(2);  // same indent as libToJsonV2 (compound_save.cpp dump(2))
    }
  }
  std::ofstream f(path);
  if (!f) return false;
  f << out;
  return f.good();
}

void adoptPoolsFromProjectFile(const std::string& path) {
  if (g_debugSkipPoolAdopt) return;  // tooth-2 seam: the "loader forgot the pools" bug
  // Second read of the file — load-time only; loadLibFromFile owns the (tolerant, two-phase) lib
  // parse and this seam must not fork it.
  std::ifstream f(path, std::ios::binary);
  if (!f) return;
  std::ostringstream ss;
  ss << f.rdbuf();
  crude_json::value root = crude_json::value::parse(ss.str());
  if (!root.is_object() || !root.contains("variationPools")) return;  // pre-multipool file: legal, no pools
  variationPoolSetFromJson(root["variationPools"], g_pools);
}

void debugForceSinglePoolKeyForTest(bool on) { g_debugSinglePoolKey = on; }
void debugSkipPoolAdoptForTest(bool on) { g_debugSkipPoolAdopt = on; }

}  // namespace sw::varpanel
