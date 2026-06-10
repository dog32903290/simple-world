#include "runtime/compound_graph.h"

namespace sw {

const Symbol* SymbolLibrary::find(const std::string& id) const {
  auto it = symbols.find(id);
  return it != symbols.end() ? &it->second : nullptr;
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
