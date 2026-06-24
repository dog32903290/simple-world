// app/asset_library — the AVAILABLE-asset enumeration + the click-to-create-op action that the
// asset browser UI sits on (Lane L3 檔案/專案, EXPERIENCE_SCOPE_GAPS #2).
//
// WHAT THIS IS — two things, both pure app-zone business logic:
//   1. ENUMERATE the shared-install asset library on disk: walk SW_ASSETS_DIR (the `Lib:` package
//      root) and produce the deterministically-ordered list of `Lib:`-prefixed asset KEYS that EXIST
//      as files. This is the browser's "what can I pick" list — the COMPLEMENT of asset_index, which
//      lists only the keys a PROJECT already references. (asset_index = referenced ∩ resolves;
//      this = everything ON DISK, available to add.)
//   2. The CREATE-OP ACTION: build a LoadImage op instance whose `Path` strOverride is a chosen asset
//      key, as a SymbolChild ready to drop on the graph through the undoable AddChildCommand. The UI
//      pushes that command, so selecting an asset adds a load-op wired to it — and undo removes it.
//
// TiXL faithfulness (verified against external/tixl Editor/Gui/Windows/AssetLib): the AssetLibrary
// window lists the resource folders' files (AssetRegistry enumeration) and, when a compatible
// file-consumer op is selected, clicking an asset rewrites that op's path input via an undoable
// ChangeInputValueCommand (AssetLibrary.Draw.cs:342 ApplyResourcePath). Dragging an asset onto the
// graph instead CREATES the asset-type's primary operator (LoadImage for images) and points it at
// the asset (DropHandling.cs:163 TryCreateSymbolInstanceOnGraph). We implement the CREATE-OP path as
// the headline click action (the most visible, demonstrably-undoable side effect) — for images that
// primary operator is LoadImage, matching TiXL's image asset-type. The full NSDragging affordance is
// a SEPARATE item (#3); here a click does what TiXL's drop does. NAMED FORK below.
//
// ZONE: app/ (a query over the asset library on disk + a graph-mutation command builder = business
// logic, same zone as asset_index / asset_relink / document). Depends ONLY on runtime/compound_graph
// (SymbolChild) + std::filesystem (the walk). The disk ROOT is INJECTED (the seam to
// platform::assetLibraryRoot), so this stays headless-testable: a test passes a temp dir it controls.
// No UI, no cook-core, no platform link, no registrars. The UI layer (ui/asset_browser) owns imgui +
// the g_commands push; this owns the data + the command-payload construction.
#pragma once
#include <string>
#include <vector>

#include "app/asset_index.h"          // AssetRef (project missing-reference list, UI banner)
#include "runtime/compound_graph.h"   // SymbolChild / SymbolLibrary

namespace sw::assetlib {

// One available asset on disk: its `Lib:` key (the project-stored reference form) and the extension
// (lowercased, no dot — e.g. "png") so the UI can icon/filter by kind. `exists` is always true for an
// enumerated entry (we only list real files); it is carried so a future stale/removed entry can be
// flagged uniformly without changing the struct.
struct AvailableAsset {
  std::string key;        // "Lib:images/basic/perlin-noise-rgb.png" — the project reference form
  std::string extension;  // "png" (lowercased, dotless); empty if the file has no extension
  bool exists = true;     // always true for an enumerated file (the relink predicate flips for refs)
};

// Walk `assetRoot` recursively and return EVERY regular file as an available `Lib:` asset, sorted
// lexicographically by key (stable list for the UI + the selftest). A file at
// <assetRoot>/images/basic/x.png becomes key "Lib:images/basic/x.png" (forward slashes, the project
// reference form). assetRoot == "" (SW_ASSETS_DIR unset) or a missing dir yields an EMPTY list (never
// a crash). Hidden files (leading '.') and directories are skipped. This is the inverse of
// platform::resolveAssetPath: resolveAssetPath("Lib:X") == assetRoot + "/X".
std::vector<AvailableAsset> enumerateAvailableAssets(const std::string& assetRoot);

// PRODUCTION convenience: enumerate the SHARED-INSTALL asset library (the root platform reports via
// assetLibraryRoot / SW_ASSETS_DIR). This is the function the UI calls — it keeps the ui zone off the
// platform include (ui → app → platform, not ui → platform). Equivalent to
// enumerateAvailableAssets(platform::assetLibraryRoot()).
std::vector<AvailableAsset> availableLibraryAssets();

// Build a LoadImage op instance pointing at `assetKey`, ready for AddChildCommand. The returned
// SymbolChild has: a fresh id (caller assigns via nextFreeChildId — we leave id 0; the UI sets it),
// symbolId "LoadImage", the given canvas position, and strOverrides["Path"] = assetKey (the seam the
// runtime reads per-instance, point_ops_loadimage.cpp:98). The UI wraps this in AddChildCommand so the
// add is undoable. Pure: builds a value, touches no library, no disk. assetKey is used as-is (the
// caller picked it from enumerateAvailableAssets, so it is already a valid `Lib:` key).
SymbolChild makeLoadImageChild(const std::string& assetKey, float x, float y);

// PRODUCTION convenience: the LOADED project's MISSING asset references (keys it references that no
// longer resolve on disk) — the asset_index missing() list, built with the platform resolver. The UI
// shows these as a "relink me" banner. Keeps the ui zone off the platform include. Equivalent to
// assetidx::buildAssetIndex(lib, platform::resolveAssetPath).missing().
std::vector<assetidx::AssetRef> projectMissingAssets(const SymbolLibrary& lib);

// Headless RED->GREEN proof (--selftest-asset-library). Builds a temp asset tree on disk (a couple of
// nested image files + a hidden file + an empty subdir), enumerates it, and asserts:
//   1. the available list is EXACTLY the real files as sorted `Lib:` keys (nested path joined with
//      '/', hidden file + directories excluded, deterministic order).
//   2. makeLoadImageChild produces a LoadImage child carrying strOverrides["Path"] == the chosen key
//      (the create-op payload is wired to the asset).
//   3. ROUND-TRIP through the real undo stack: pushing AddChildCommand(child) grows the target
//      symbol's child count by one AND the new child carries the Path override; undo REMOVES it
//      (child count back to baseline, no residue) — the undoable side-effect the task demands.
// injectBug drops the strOverride on the created child (a load-op with no Path) → assertion 2 and 3's
// "carries the override" leg FAIL (teeth bite the REAL property: a create-op that loses the asset key
// would silently load the default asset, not the one the user clicked).
int runAssetLibrarySelfTest(bool injectBug);

}  // namespace sw::assetlib
