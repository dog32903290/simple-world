#include "runtime/compound_graph.h"

#include <cstdlib>

namespace sw {

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

}  // namespace sw
