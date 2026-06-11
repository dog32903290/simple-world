// runtime/resident_eval_patch_lib — slice-3 rest: DEFINITION-level patches. TiXL edits live on the
// Symbol and broadcast to every instance (Symbol.cs:222-330); here the SymbolLibrary is mutated
// first (definition = authority, contract C3), then the resident projection is patched so that
// patch == rebuild (the golden defines correctness).
//
// Two strategies, by edit class:
//  - VALUE edits (change-default) = targeted surgery with the IsDefault filter — cheap, cache on
//    every overriding instance survives untouched.
//  - STRUCTURAL edits (add/remove child, IO change) = re-derive the wiring through the ONE
//    canonical codepath (buildEvalGraph) + CACHE MIGRATION. A second hand-written wiring path
//    would drift from the flattener (意外複雜); TiXL's own broadcast-reconnect is also O(instances)
//    at edit time. Migration rules keep the resident contract honest:
//      1. same path + same opType + equivalent inputs (incl. Connection RESOLVABILITY) ->
//         cache copied wholesale (untouched nodes keep their cachedFloat — the 增量 win).
//      2. same path, anything changed -> fresh cache forced with baseVersion = old sourceVersion + 1
//         (monotonic: a slot's sourceVersion never decreases across an edit, so downstream sums
//         strictly change — no false-clean collisions; generalizes refuter D1/A4).
//      3. new path -> fresh (initially dirty); consumers referencing it see a resolvability flip
//         and are caught by rule 2.
// runtime leaf: resident_eval_graph.h (+ compound_graph.h via it) only.
#include <algorithm>
#include <set>

#include "runtime/resident_eval_graph.h"

namespace sw {
namespace {

// One inlined child occurrence in the resident projection (mirrors buildEvalGraph's traversal,
// including its self-nesting guard, so paths here == paths in the built graph).
struct WalkSite {
  std::string path;            // resident path of this child instance
  std::string parentSymbolId;  // the symbol whose subgraph contains the child
  const SymbolChild* child;    // borrowed from lib — valid only until lib is mutated
};
struct LibWalk {
  std::vector<WalkSite> sites;
};

void walkSym(const SymbolLibrary& lib, const Symbol& sym, const std::string& prefix,
             std::vector<std::string>& onPath, LibWalk& w, int depth) {
  if (depth > 64) return;
  for (const SymbolChild& c : sym.children) {
    const Symbol* def = lib.find(c.symbolId);
    if (!def) continue;
    w.sites.push_back({prefix + std::to_string(c.id), sym.id, &c});
    if (def->atomic) continue;
    bool nested = false;  // same guard as the flattener (S14)
    for (const std::string& id : onPath)
      if (id == c.symbolId) { nested = true; break; }
    if (nested) continue;
    onPath.push_back(c.symbolId);
    walkSym(lib, *def, prefix + std::to_string(c.id) + "/", onPath, w, depth + 1);
    onPath.pop_back();
  }
}

LibWalk walkLib(const SymbolLibrary& lib) {
  LibWalk w;
  const Symbol* root = lib.find(lib.rootId);
  if (!root) return w;
  std::vector<std::string> onPath = {lib.rootId};
  walkSym(lib, *root, "", onPath, w, 0);
  return w;
}

// Rule-1 equivalence: drivers identical AND Connection sources equally resolvable in old vs new
// graph (a dangling ref that becomes real — or vice versa — changes the upstream's version
// contribution without changing the strings, so it MUST count as a change).
bool inputsEquivalent(const ResidentNode& a, const ResidentEvalGraph& ga, const ResidentNode& b,
                      const ResidentEvalGraph& gb) {
  if (a.inputs.size() != b.inputs.size()) return false;
  for (size_t i = 0; i < a.inputs.size(); ++i) {
    const ResidentInput& x = a.inputs[i];
    const ResidentInput& y = b.inputs[i];
    if (x.slotId != y.slotId || x.driver != y.driver) return false;
    switch (x.driver) {
      case ResidentInput::Driver::Constant:
        if (x.constant != y.constant) return false;
        break;
      case ResidentInput::Driver::Connection:
        if (x.srcNodePath != y.srcNodePath || x.srcSlotId != y.srcSlotId) return false;
        if (ga.byPath.count(x.srcNodePath) != gb.byPath.count(y.srcNodePath)) return false;
        break;
      case ResidentInput::Driver::Automation:
        if (x.curveRef != y.curveRef) return false;
        break;
    }
  }
  return true;
}

// Rebuild from the (already-edited) lib and migrate caches per the header rules. Replaces g.
void rebuildWithCacheMigration(const SymbolLibrary& lib, ResidentEvalGraph& g) {
  ResidentEvalGraph fresh = buildEvalGraph(lib, lib.rootId);
  initResidentCache(fresh);
  for (ResidentNode& n : fresh.nodes) {
    const ResidentNode* old = g.node(n.path);
    if (!old) continue;  // rule 3: new path, stays initially dirty
    if (old->opType == n.opType && inputsEquivalent(*old, g, n, fresh)) {
      n.outCache = old->outCache;  // rule 1: untouched — cache survives wholesale
      continue;
    }
    for (auto& kv : n.outCache) {  // rule 2: changed — force, monotonically
      auto oc = old->outCache.find(kv.first);
      if (oc == old->outCache.end()) continue;
      // Monotonic floor = max(baseVersion, sourceVersion): the sourceVersion FIELD only refreshes
      // on a pull, so two structural edits back-to-back (no pull between — a batch-4 command group)
      // would read the stale field and REGRESS base -> downstream kept-cache sums collide ->
      // false-clean 卡舊 (refuter A-1, executable repro). The floor (and mirroring it into the
      // fresh sourceVersion field) keeps the invariant true under edit sequences, not just edits.
      const uint64_t flo = oc->second.baseVersion > oc->second.sourceVersion
                               ? oc->second.baseVersion
                               : oc->second.sourceVersion;
      kv.second.baseVersion = flo + 1;
      kv.second.sourceVersion = kv.second.baseVersion;
      kv.second.isLiveSource = kv.second.isLiveSource || oc->second.isLiveSource;
      // valueVersion stays 0 (initially dirty); sourceVersion >= old + 1 keeps downstream sums moving.
    }
  }
  g = std::move(fresh);
}

}  // namespace

bool patchLibSetDefault(SymbolLibrary& lib, ResidentEvalGraph& g, const std::string& symbolId,
                        const std::string& slotId, float newDef) {
  auto sit = lib.symbols.find(symbolId);
  if (sit == lib.symbols.end()) return false;
  SlotDef* def = nullptr;
  for (SlotDef& d : sit->second.inputDefs)
    if (d.id == slotId) def = &d;
  if (!def) return false;
  def->def = newDef;  // definition authority first
  if (!sit->second.atomic) {
    // Compound defaults project through childIn seeding into inner consumers' constants — route
    // through the canonical rebuild: migration rule 2 forces exactly the affected cones, and the
    // IsDefault filter EMERGES (overriding instances project unchanged -> rule 1 keeps cache).
    // (refuter A-4: the old early-return mutated the lib, reported success, left g desynced.)
    rebuildWithCacheMigration(lib, g);
    return true;
  }
  // Atomic broadcast (Symbol.cs:375-386): touch ONLY instances still on the default — an override
  // means the definition value is unused there (IsDefault filter, Symbol.Child.cs:677-698) ->
  // untouched, cache preserved.
  const LibWalk w = walkLib(lib);
  for (const WalkSite& s : w.sites) {
    if (s.child->symbolId != symbolId || s.child->overrides.count(slotId)) continue;
    auto it = g.byPath.find(s.path);
    if (it == g.byPath.end()) continue;
    ResidentNode& n = g.nodes[it->second];
    bool consumed = false;
    for (ResidentInput& in : n.inputs) {
      if (in.slotId != slotId) continue;
      // Constant-driven: the live value -> invalidate. Connection/Automation-driven: the constant
      // is the KEPT fallback a future disconnect restores (= TiXL reads the default live at eval,
      // InputSlot.cs:27-30) — refresh it WITHOUT a bump, the value isn't consumed while driven
      // (refuter A-2: a stale kept value made disconnect restore the OLD default, patch != rebuild).
      consumed = consumed || in.driver == ResidentInput::Driver::Constant;
      in.constant = newDef;
    }
    if (consumed)
      for (auto& kv : n.outCache) kv.second.baseVersion++;  // edit-time push (S1 rule)
  }
  return true;
}

bool patchLibAddChild(SymbolLibrary& lib, ResidentEvalGraph& g, const std::string& parentSymbolId,
                      int childId, const std::string& childSymbolId) {
  auto pit = lib.symbols.find(parentSymbolId);
  if (pit == lib.symbols.end() || pit->second.atomic || childId == kSymbolBoundary) return false;
  const Symbol* cdef = lib.find(childSymbolId);
  if (!cdef || !cdef->atomic) return false;  // compound add = recursive inline, named-deferred
  for (const SymbolChild& c : pit->second.children)
    if (c.id == childId) return false;  // id taken (TiXL throws, Instantiation.cs:20-23)
  SymbolChild nc;
  nc.id = childId;
  nc.symbolId = childSymbolId;
  pit->second.children.push_back(nc);
  // An atomic add can resolve a pre-existing dangling reference (version contribution flips from
  // the fixed dangling 1 to the fresh node's own 1 — aliasing). Migration's resolvability rule
  // catches exactly that, so structural adds go through the canonical rebuild + migrate.
  rebuildWithCacheMigration(lib, g);
  return true;
}

bool patchLibRemoveChild(SymbolLibrary& lib, ResidentEvalGraph& g, const std::string& parentSymbolId,
                         int childId) {
  auto pit = lib.symbols.find(parentSymbolId);
  if (pit == lib.symbols.end()) return false;
  Symbol& parent = pit->second;
  int idx = -1;
  for (size_t i = 0; i < parent.children.size(); ++i)
    if (parent.children[i].id == childId) idx = (int)i;
  if (idx < 0) return false;
  // 牽涉連線清除 first (Symbol.cs:311-330), then the child. Same-scope consumers thereby fall back
  // to Constant effectiveInput on rebuild; cross-boundary consumers (their wire lives in another
  // symbol and survives) resolve to a dangling path and evaluate the upstream as 0 — both are what
  // TiXL's instances see after its broadcast, and migration forces both (inputs changed).
  auto& conns = parent.connections;
  conns.erase(std::remove_if(conns.begin(), conns.end(),
                             [&](const SymbolConnection& c) {
                               return c.srcChild == childId || c.dstChild == childId;
                             }),
              conns.end());
  parent.children.erase(parent.children.begin() + idx);
  rebuildWithCacheMigration(lib, g);
  return true;
}

bool patchLibRemoveInputDef(SymbolLibrary& lib, ResidentEvalGraph& g, const std::string& symbolId,
                            const std::string& slotId) {
  auto sit = lib.symbols.find(symbolId);
  if (sit == lib.symbols.end()) return false;
  Symbol& sym = sit->second;
  int idx = -1;
  for (size_t i = 0; i < sym.inputDefs.size(); ++i)
    if (sym.inputDefs[i].id == slotId) idx = (int)i;
  if (idx < 0) return false;
  sym.inputDefs.erase(sym.inputDefs.begin() + idx);
  // 收屍 (S13, Symbol.TypeUpdating.cs:99-132): inside the symbol, wires FROM the removed boundary
  // input; in EVERY symbol, wires INTO (child-of-symbolId, slotId) and now-obsolete overrides.
  auto& inner = sym.connections;
  inner.erase(std::remove_if(inner.begin(), inner.end(),
                             [&](const SymbolConnection& c) {
                               return sourceIsSymbolInput(c) && c.srcSlot == slotId;
                             }),
              inner.end());
  for (auto& kv : lib.symbols) {
    Symbol& host = kv.second;
    std::set<int> affected;
    for (SymbolChild& c : host.children)
      if (c.symbolId == symbolId) {
        c.overrides.erase(slotId);
        affected.insert(c.id);
      }
    if (affected.empty()) continue;
    auto& hc = host.connections;
    hc.erase(std::remove_if(hc.begin(), hc.end(),
                            [&](const SymbolConnection& c) {
                              return affected.count(c.dstChild) && c.dstSlot == slotId;
                            }),
             hc.end());
  }
  rebuildWithCacheMigration(lib, g);
  return true;
}

}  // namespace sw
