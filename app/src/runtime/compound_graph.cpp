#include "runtime/compound_graph.h"

#include <algorithm>
#include <cstdlib>
#include <set>

namespace sw {
namespace {

// The shared body of input/output def removal — isInput picks which boundary side a def lives on and
// which side of a parent wire references it. Mirrors TiXL's union (TypeUpdating.cs:99-132 inner wires
// + Child.cs:217-223 override scrub + Child.cs:424-451 parent wire scrub). `removed` captured with
// original indices so the command layer can restore order-faithfully (multi-input order contract).
bool removeDefFromLib(SymbolLibrary& lib, const std::string& symbolId, const std::string& slotId,
                      bool isInput, RemovedSlotDef* removed) {
  Symbol* sym = lib.find(symbolId);
  if (!sym) return false;
  std::vector<SlotDef>& defs = isInput ? sym->inputDefs : sym->outputDefs;
  int idx = -1;
  for (size_t i = 0; i < defs.size(); ++i)
    if (defs[i].id == slotId) idx = (int)i;
  if (idx < 0) return false;

  if (removed) {
    removed->def = defs[idx];
    removed->index = (size_t)idx;
    removed->isInput = isInput;
  }
  defs.erase(defs.begin() + idx);

  // 1) The edited symbol's OWN boundary wires: an inputDef is an inner SOURCE (sentinel src side),
  //    an outputDef is an inner SINK (sentinel dst side). Capture index+wire before erasing.
  auto& inner = sym->connections;
  for (size_t i = 0; i < inner.size(); ++i) {
    const SymbolConnection& c = inner[i];
    bool hit = isInput ? (sourceIsSymbolInput(c) && c.srcSlot == slotId)
                       : (targetIsSymbolOutput(c) && c.dstSlot == slotId);
    if (hit && removed) removed->innerWires.push_back({i, c});
  }
  inner.erase(std::remove_if(inner.begin(), inner.end(),
                             [&](const SymbolConnection& c) {
                               return isInput ? (sourceIsSymbolInput(c) && c.srcSlot == slotId)
                                              : (targetIsSymbolOutput(c) && c.dstSlot == slotId);
                             }),
              inner.end());

  // 2) Every parent symbol (every host of an instance of symbolId): scrub wires referencing the
  //    deleted slot of that instance, and drop the instance's now-obsolete overrides (inputs only).
  //    Parent wire side: an instance's inputDef is a wire SINK (dstChild,dstSlot); an outputDef is a
  //    wire SOURCE (srcChild,srcSlot). Order within each host preserved (multi-input contract).
  for (auto& kv : lib.symbols) {
    Symbol& host = kv.second;
    std::set<int> affected;
    for (SymbolChild& c : host.children)
      if (c.symbolId == symbolId) {
        if (isInput) {
          auto ov = c.overrides.find(slotId);
          if (ov != c.overrides.end()) {
            if (removed) removed->overrides.push_back({host.id, c.id, ov->second});
            c.overrides.erase(ov);
          }
        }
        affected.insert(c.id);
      }
    if (affected.empty()) continue;
    auto& hc = host.connections;
    auto hostHit = [&](const SymbolConnection& c) {
      return isInput ? (affected.count(c.dstChild) && c.dstSlot == slotId)
                     : (affected.count(c.srcChild) && c.srcSlot == slotId);
    };
    for (size_t i = 0; i < hc.size(); ++i)
      if (hostHit(hc[i]) && removed) removed->hostWires.push_back({host.id, i, hc[i]});
    hc.erase(std::remove_if(hc.begin(), hc.end(), hostHit), hc.end());
  }
  return true;
}

}  // namespace


const Symbol* SymbolLibrary::find(const std::string& id) const {
  auto it = symbols.find(id);
  return it != symbols.end() ? &it->second : nullptr;
}

Symbol* SymbolLibrary::find(const std::string& id) {
  auto it = symbols.find(id);
  return it != symbols.end() ? &it->second : nullptr;
}

SymbolChild* childById(Symbol& s, int id) {
  for (SymbolChild& c : s.children)
    if (c.id == id) return &c;
  return nullptr;
}

const SymbolChild* childById(const Symbol& s, int id) {
  for (const SymbolChild& c : s.children)
    if (c.id == id) return &c;
  return nullptr;
}

int nextFreeChildId(const Symbol& s) {
  int maxId = 0;
  for (const SymbolChild& c : s.children)
    if (c.id > maxId) maxId = c.id;
  // The monotonic floor wins over max+1: a freed id below the floor stays dead forever
  // (per-path runtime state must never be inherited by a new child).
  return maxId + 1 > s.nextChildId ? maxId + 1 : s.nextChildId;
}

const SymbolConnection* connectionToInput(const Symbol& s, int dstChild,
                                          const std::string& dstSlot) {
  for (const SymbolConnection& w : s.connections)
    if (w.dstChild == dstChild && w.dstSlot == dstSlot) return &w;
  return nullptr;
}

std::string viewProducerPath(const SymbolLibrary& lib, const std::string& prefixPath,
                             int childId) {
  std::string path = prefixPath + std::to_string(childId);
  // Re-derive the scope symbol by walking prefixPath from the root (each "/"-terminated
  // segment is a child id) — childId must be a child of the symbol the prefix reaches.
  // The walk also collects the chain's symbol ids: the follow loop below must MIRROR the
  // resident builder's S14 guard (a symbol id repeating on the path is skipped at inline
  // time) — otherwise a mutual-recursive chain that happens to END at an atomic returns a
  // NON-EMPTY path the builder never built, which bypasses the caller's fallback and cooks
  // black (refuter N4 #1, reproduced).
  std::vector<std::string> chain;
  chain.push_back(lib.rootId);
  const Symbol* scope = lib.find(lib.rootId);
  {
    size_t pos = 0;
    while (scope && pos < prefixPath.size()) {
      size_t slash = prefixPath.find('/', pos);
      if (slash == std::string::npos) break;
      int id = std::atoi(prefixPath.substr(pos, slash - pos).c_str());
      const SymbolChild* c = childById(*scope, id);
      scope = c ? lib.find(c->symbolId) : nullptr;
      if (scope) chain.push_back(scope->id);
      pos = slash + 1;
    }
  }
  auto onChain = [&](const std::string& id) {
    for (const std::string& cid : chain)
      if (cid == id) return true;
    return false;
  };
  const SymbolChild* child = scope ? childById(*scope, childId) : nullptr;
  const Symbol* s = child ? lib.find(child->symbolId) : nullptr;
  for (int depth = 0; s; ++depth) {
    if (s->atomic) return path;             // an atomic child IS the producer
    if (onChain(s->id)) return "";          // S14 mirror: the builder skipped this subtree
    chain.push_back(s->id);
    if ((int)chain.size() > 64 || depth > 64) return "";  // belt: ABSOLUTE depth, like the builder
    if (s->outputDefs.empty()) return "";   // nothing to view
    const SymbolConnection* w = connectionToInput(*s, kSymbolBoundary, s->outputDefs[0].id);
    if (!w || w->srcChild == kSymbolBoundary) return "";  // unwired / input passthrough
    path += "/" + std::to_string(w->srcChild);
    child = childById(*s, w->srcChild);
    s = child ? lib.find(child->symbolId) : nullptr;
  }
  return "";
}

float effectiveInput(const SymbolLibrary& lib, const SymbolChild& child,
                     const std::string& slotId, float fallback) {
  auto ov = child.overrides.find(slotId);
  if (ov != child.overrides.end()) return ov->second;  // this instance's override
  if (const Symbol* s = lib.find(child.symbolId))       // else the referenced def's default
    for (const SlotDef& d : s->inputDefs)
      if (d.id == slotId) return d.def;
  return fallback;
}

bool removeInputDefFromLib(SymbolLibrary& lib, const std::string& symbolId, const std::string& slotId,
                           RemovedSlotDef* removed) {
  return removeDefFromLib(lib, symbolId, slotId, /*isInput=*/true, removed);
}

bool removeOutputDefFromLib(SymbolLibrary& lib, const std::string& symbolId, const std::string& slotId,
                            RemovedSlotDef* removed) {
  return removeDefFromLib(lib, symbolId, slotId, /*isInput=*/false, removed);
}

void restoreSlotDefToLib(SymbolLibrary& lib, const std::string& symbolId, const RemovedSlotDef& r) {
  Symbol* sym = lib.find(symbolId);
  if (!sym) return;  // symbol vanished (e.g. removed in a later, now-undone edit) — nothing to do
  // Restore the def at its original index (clamp: the list may be shorter than at capture time).
  std::vector<SlotDef>& defs = r.isInput ? sym->inputDefs : sym->outputDefs;
  size_t at = r.index < defs.size() ? r.index : defs.size();
  defs.insert(defs.begin() + at, r.def);
  // Inner boundary wires back at their original indices (ascending = each insert position valid).
  for (const auto& [idx, w] : r.innerWires) {
    size_t iat = idx < sym->connections.size() ? idx : sym->connections.size();
    sym->connections.insert(sym->connections.begin() + iat, w);
  }
  // Parent wires back into their hosts at original indices, grouped per host (ascending within host).
  for (const RemovedHostWire& hw : r.hostWires) {
    Symbol* host = lib.find(hw.hostSymbolId);
    if (!host) continue;
    size_t hat = hw.index < host->connections.size() ? hw.index : host->connections.size();
    host->connections.insert(host->connections.begin() + hat, hw.wire);
  }
  // Instance overrides back onto their children (inputs only; outputs never had any). The slot is
  // the restored def's id (the scrub only ever removed overrides keyed by exactly that slot).
  for (const RemovedOverride& ro : r.overrides) {
    Symbol* host = lib.find(ro.hostSymbolId);
    if (SymbolChild* c = host ? childById(*host, ro.childId) : nullptr) c->overrides[r.def.id] = ro.value;
  }
}

}  // namespace sw
