// app/asset_index — the non-UI asset-dependency data model for a loaded project (Lane L3 檔案/專案).
//
// WHAT THIS IS: given a loaded SymbolLibrary, walk it and produce the DEDUPED set of asset keys the
// project references, each tagged with whether it currently resolves to a real file on disk. This is
// the data layer a future "missing asset / relink" check and the asset browser sit on. It is pure
// business logic (app zone): it READS a library + asks platform to resolve a key, and never touches
// imgui / g_lib / the cook core. Headless-testable (--selftest-asset-index).
//
// THE ASSET MODEL IT RIDES (already shipped — this invents NO new fork):
//   • An image node references an asset by a "Lib:..." KEY string stored as
//     SymbolChild.strOverrides["Path"] (compound_graph.h:101; the round-trip is pinned by
//     asset_ref_roundtrip_selftest.cpp). The asset FILE is shared-install (TiXL's `Lib:` package),
//     resolved under SW_ASSETS_DIR via platform::resolveAssetPath (image_decode.h:80).
//   • The composition's soundtrack is an ABSOLUTE external path in composition.soundtrackPath
//     (compound_graph.h:144; auto_backup.h:25-39 explains the two-kind asset model — shared `Lib:`
//     image keys vs. the one external user-picked soundtrack file).
//
// TiXL faithfulness: TiXL enumerates a project's resource dependencies through its resource-package
// system, where a dependency is a `Lib:`-prefixed key resolved against the resource folders. sw's
// model is the SAME shape (a `Lib:` key + a resolver) — already forked to shared-install resolution
// (auto_backup.h NAMED FORK). This index rides that fork: the discriminant for "is this string an
// asset reference" is the `Lib:` prefix (which TiXL also uses), so generic non-asset string
// overrides (e.g. SetFloatVar's VariableName, also living in strOverrides) are correctly EXCLUDED.
// The soundtrack is the one external (non-`Lib:`) asset, carried as a distinct kind below.
//
// ZONE: app/ (a project-scope query over a library = business logic, same zone as document/backup).
// Depends only on runtime/compound_graph (the library) + platform/image_decode (key→path resolve,
// app→platform is legal) + std::filesystem (existence). No UI, no cook-core, no registrars.
#pragma once
#include <string>
#include <vector>

#include "runtime/compound_graph.h"  // SymbolLibrary

namespace sw::assetidx {

// What KIND of asset a referenced key is — they resolve to a real file by DIFFERENT rules.
enum class AssetKind {
  LibImage,    // a "Lib:..." key → resolved under SW_ASSETS_DIR (shared-install library)
  Soundtrack,  // composition.soundtrackPath → an absolute external path (used as-is)
};

// One referenced asset: the key as stored in the project, its kind, and whether it currently
// resolves to a real file on disk (false = MISSING → the future relink check flags it).
struct AssetRef {
  std::string key;     // the project-stored reference (a "Lib:..." key, or the soundtrack abs path)
  AssetKind kind = AssetKind::LibImage;
  bool resolves = false;  // does it map to an existing file right now?
};

// The deduped, deterministically-ordered set of assets a project references. Order: all LibImage
// keys first (lexicographically), then the soundtrack (if any) — stable so the selftest can assert
// an exact set and so a UI list is reproducible.
struct AssetIndex {
  std::vector<AssetRef> refs;

  // Convenience views over `refs` (no extra state — derived).
  bool empty() const { return refs.empty(); }
  size_t size() const { return refs.size(); }
  // Is `key` referenced AND currently missing (does not resolve)? false if not referenced at all.
  bool isMissing(const std::string& key) const;
  // Is `key` referenced at all (regardless of resolution)?
  bool references(const std::string& key) const;
  // The subset of refs that do NOT resolve (the relink work list).
  std::vector<AssetRef> missing() const;
};

// Walk a loaded library and build its asset index. Iterates EVERY symbol's children (every instance
// across the whole project, atomic defs included) collecting strOverrides whose value carries the
// `Lib:` asset-key prefix, plus composition.soundtrackPath. Dedupes by key. The resolution check is
// done here (so the result is a self-contained snapshot): a `Lib:` key resolves iff
// resolveKeyToPath(key) names an existing file; the soundtrack resolves iff its absolute path exists.
//
// `resolveLibKey` is injected (the seam to platform::resolveAssetPath) so the index stays headless-
// testable: a test passes a fake resolver to control which keys "exist" without touching SW_ASSETS_DIR
// or the real disk. Production wires platform::resolveAssetPath. If null, `Lib:` keys are treated as
// unresolved (resolves=false) — a safe default, never a crash.
AssetIndex buildAssetIndex(const SymbolLibrary& lib,
                           std::string (*resolveLibKey)(const std::string&));

// Headless RED->GREEN proof (--selftest-asset-index). Builds a project with several asset-referencing
// children (some keys resolvable, some deliberately bogus) + a non-asset string override + a
// soundtrack, indexes it, and asserts:
//   1. referenced-key set is EXACTLY right — deduped (the same key on two children appears once),
//      includes the soundtrack, EXCLUDES the non-asset string (VariableName).
//   2. missing predicate is correct — the bogus key flags missing, the real key passes.
//   3. round-trip stable — save→load→re-index yields the SAME set (keys survive the disk trip).
// injectBug inverts the missing predicate (a real key reported missing / a bogus key reported
// present) → assertion 2 FAILS (teeth bite the REAL property: a wrong relink check would either nag
// about a present asset or silently pass a broken reference).
int runAssetIndexSelfTest(bool injectBug);

}  // namespace sw::assetidx
