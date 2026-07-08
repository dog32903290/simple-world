// runtime/t3_import_recurse — the COMPOUND-RECURSION SEAM of the .t3 importer, split out of
// t3_import.cpp for the ARCHITECTURE rule-4 line ratchet (the walk+fill already sits at ~390 lines).
// Owns three things the single-layer importer never needed:
//   1. t3RecurseDisable()        — the test injection flag (recursion RED case).
//   2. t3SlotNameForChildType()  — §1.3 cross-layer slot resolution (compound ports have no map row).
//   3. t3ResolveNestedCompound() — fetch a nested compound child's .t3 via the resolver + recurse it.
//
// runtime leaf: NO filesystem here either — the guid→.t3 lookup is the injected T3Resolver (the APP zone
// builds the index). This file only decides "is this child a nested compound, and if so recurse it".
//
// ZONE: runtime (純計算). Pure CPU: dedup + cycle-guard + depth-cap + a callback into the importer core.
#include "runtime/t3_import.h"

#include <functional>
#include <string>
#include <vector>

#include "runtime/compound_graph.h"       // Symbol / SymbolLibrary / addChildWouldCycle
#include "runtime/t3_import_internal.h"   // t3i::lc + importT3SymbolImpl decl + the helper decls
#include "runtime/t3_import_maps.h"       // swSlotNameForGuid (atom-side slot resolution)

namespace sw {

using t3i::lc;

// Test-only injection seam (compound-recursion RED case): when true, t3ResolveNestedCompound refuses to
// recurse even with a resolver present → the nested compound child reverts to "unmapped, skipped" (the
// pre-seam single-layer flatten). Off in production; the nested-compound golden flips it to measure RED.
bool& t3RecurseDisable() {
  static bool flag = false;
  return flag;
}

// §1.3 CROSS-LAYER SLOT FALLBACK. See the decl in t3_import_internal.h for the full rationale. Atoms
// resolve via the maps; a NESTED COMPOUND (atomic==false in lib) has no map row, so its "slot name" IS
// the matching inputDef/outputDef id — which the importer keyed by the very .t3 slot guid (Inputs[].Id /
// the boundary-output slot guid). Checks inputs first (override + connection-DST), then outputs
// (connection-SRC off a nested compound's boundary output).
std::string t3SlotNameForChildType(const SymbolLibrary& lib, const std::string& swType,
                                   const std::string& slotGuid) {
  const std::string atomName = swSlotNameForGuid(swType, slotGuid);
  if (!atomName.empty()) return atomName;
  const Symbol* def = lib.find(swType);
  if (!def || def->atomic) return std::string();  // atom with no row, or unknown type → honest miss
  const std::string want = lc(slotGuid);
  for (const SlotDef& d : def->inputDefs) if (d.id == want) return d.id;
  for (const SlotDef& d : def->outputDefs) if (d.id == want) return d.id;
  return std::string();
}

// NESTED-COMPOUND RESOLUTION. See the decl in t3_import_internal.h for the contract.
std::string t3ResolveNestedCompound(const std::string& parentGuid, const std::string& childGuid,
                                    const std::string& childSymbolId, SymbolLibrary& lib,
                                    std::vector<std::string>* warnings, const T3Resolver& resolve,
                                    int depth, const std::function<void(const std::string&)>& warn) {
  const std::string childSymGuid = lc(childSymbolId);
  if (!resolve || t3RecurseDisable() || childSymGuid.empty()) return std::string();

  // DEDUP: already imported (shared sub-graph / diamond) → reference it, no re-recurse. addChildWouldCycle
  // then rejects the case where referencing it back would close a containment cycle (A⊃B⊃A across a
  // committed ancestor); depth cap 64 is the hard backstop for a not-yet-committed direct cycle.
  if (lib.symbols.count(childSymGuid)) {
    if (addChildWouldCycle(lib, parentGuid, childSymGuid)) {
      warn("t3: child " + childGuid + " nested compound " + childSymGuid + " would cycle, skipped");
      return std::string();
    }
    const Symbol* d = lib.find(childSymGuid);
    return (d && !d->atomic) ? childSymGuid : std::string();  // only reference a real compound
  }

  if (depth >= 64) {
    warn("t3: child " + childGuid + " nested compound " + childSymGuid + " depth cap (64), skipped");
    return std::string();
  }
  if (addChildWouldCycle(lib, parentGuid, childSymGuid)) {
    warn("t3: child " + childGuid + " nested compound " + childSymGuid + " would cycle, skipped");
    return std::string();
  }

  std::string childJson;
  if (!resolve(childSymGuid, childJson)) return std::string();  // resolver miss → honest "unmapped" above

  std::string nestedId;
  const bool ok = importT3SymbolImpl(childJson, lib, &nestedId, warnings, resolve, depth + 1);
  const Symbol* nested = lib.find(childSymGuid);
  if (!ok || !nested || nested->atomic) {
    warn("t3: child " + childGuid + " nested compound import of " + childSymGuid + " failed, skipped");
    return std::string();
  }
  return childSymGuid;  // the parent SymbolChild references the nested compound by guid
}

}  // namespace sw
