#include "runtime/compound_graph.h"

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
