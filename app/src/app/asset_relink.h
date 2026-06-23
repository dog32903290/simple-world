// app/asset_relink — the project-wide ASSET RELINK mutation (Lane L3 檔案/專案).
//
// WHAT THIS IS: given a loaded project and an asset reference that has moved/gone missing (a key the
// asset_index flags), rewrite EVERY instance of that reference to a new key — across all symbols,
// all children, and the soundtrack — and report how many references were updated. This is the WRITE
// side that sits on top of asset_index's READ side (the missing-asset work list). Pure business
// logic (app zone): it MUTATES a SymbolLibrary in memory and never touches imgui / g_lib / the cook
// core / disk. Headless-testable (--selftest-asset-relink).
//
// THE ASSET MODEL IT RIDES (same one asset_index documents — invents NO new fork):
//   • An image node references an asset by a "Lib:..." KEY stored as SymbolChild.strOverrides["Path"]
//     (compound_graph.h:101). The SAME key can appear on many children across many symbols (reuse) —
//     each carries its OWN copy of the string (there is NO shared resource entry).
//   • The composition's soundtrack is an absolute external path in composition.soundtrackPath
//     (compound_graph.h:144) — the one external (non-`Lib:`) asset.
//
// TiXL faithfulness (verified against external/tixl): TiXL relinking is a PER-INSTANCE string
// rewrite — FileReferenceOperations rewrites Symbol.Child.Input.Value for every child that carries
// the path and invalidates all instances; there is NO dedup to one place. The soundtrack is the
// special single-path case (TimelineAudioClip.AssetPath, one per composition). sw's model is the
// SAME shape — a per-child override string + one soundtrack path — so this mutation rewrites EVERY
// instance string equal to the old key and the soundtrack if it matches. RODE THE EXISTING MODEL,
// no new fork. (We match the WHOLE key exactly, not a basename substring: sw keys are full `Lib:`
// addresses, so exact equality is the faithful + unambiguous discriminant — a substring match could
// corrupt a longer key that merely contains the old one.)
//
// ZONE: app/ (a project-scope mutation over a library = business logic, same zone as asset_index /
// document / backup). Depends ONLY on runtime/compound_graph (the library). No UI, no cook-core, no
// platform, no registrars, no disk. The missing-check is the CALLER's job (it asks asset_index first,
// using the injected resolver) — this mutation is deliberately resolution-agnostic so it stays a
// pure, deterministic, headless rewrite.
#pragma once
#include <string>

#include "runtime/compound_graph.h"  // SymbolLibrary

namespace sw::assetrelink {

// The outcome of one relink: how many references were rewritten, split by kind so the caller (and
// the golden) can assert the breakdown. `total` = childRefs + (soundtrack ? 1 : 0).
struct RelinkResult {
  int childRefs = 0;     // # of SymbolChild.strOverrides values rewritten (across ALL symbols)
  bool soundtrack = false;  // was composition.soundtrackPath equal to oldKey and rewritten?
  int total() const { return childRefs + (soundtrack ? 1 : 0); }
};

// Rewrite EVERY reference to `oldKey` → `newKey` across the whole project, in place. Walks every
// symbol's every child's strOverrides; any value that EQUALS oldKey (exact match — the faithful
// discriminant, see header note) is set to newKey. The soundtrack is rewritten iff it EQUALS oldKey.
// Returns the per-kind count. A no-op (oldKey not referenced) returns {0,false} and leaves `lib`
// untouched. If oldKey == newKey, returns {0,false} without scanning (nothing to do) — a safe guard,
// never a crash. Pure: no disk, no resolution; the caller decides oldKey is the one to relink (e.g.
// from assetidx::AssetIndex::missing()).
RelinkResult relinkAsset(SymbolLibrary& lib, const std::string& oldKey, const std::string& newKey);

// Headless RED->GREEN proof (--selftest-asset-relink). Builds a project where one `Lib:` key is
// referenced by N>=2 children (+ the soundtrack set to it), relinks it to a NEW resolvable key, and
// asserts:
//   1. return count == the number of references that existed (every instance + soundtrack).
//   2. re-running buildAssetIndex shows the OLD key GONE and the NEW key PRESENT (right resolve
//      status under the injected resolver).
//   3. zero-missing after relinking the missing key to a resolvable one (the work list empties).
//   4. round-trip stable: the relinked project saves→loads identically (the rewritten keys survive
//      the production .swproj disk trip).
// injectBug makes the rewrite MISS one child instance (rewrites all-but-one) → assertion 1's count
// is short AND assertion 2 still sees the old key (a stale reference survives) → the golden goes RED.
// Teeth bite the REAL property: a relink that skips an instance leaves a broken reference behind.
int runAssetRelinkSelfTest(bool injectBug);

}  // namespace sw::assetrelink
