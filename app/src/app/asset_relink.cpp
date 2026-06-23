// app/asset_relink — implementation. See asset_relink.h for the design + the asset model it rides.
#include "app/asset_relink.h"

namespace sw::assetrelink {

RelinkResult relinkAsset(SymbolLibrary& lib, const std::string& oldKey, const std::string& newKey) {
  RelinkResult r;
  if (oldKey == newKey) return r;  // nothing to do (and never a self-rewrite) — safe guard.

  // Rewrite every per-instance override string equal to oldKey, across EVERY symbol's children.
  // Per-instance (no shared entry) is faithful to TiXL's FileReferenceOperations — see header note.
  for (auto& [symId, sym] : lib.symbols) {
    (void)symId;
    for (auto& child : sym.children) {
      for (auto& [slotId, value] : child.strOverrides) {
        (void)slotId;
        if (value == oldKey) {  // exact whole-key match (not a substring) — the faithful discriminant
          value = newKey;
          ++r.childRefs;
        }
      }
    }
  }

  // The one external asset: the soundtrack (absolute path, not a Lib: key). Rewritten iff it matches.
  if (lib.composition.soundtrackPath == oldKey) {
    lib.composition.soundtrackPath = newKey;
    r.soundtrack = true;
  }

  return r;
}

}  // namespace sw::assetrelink
