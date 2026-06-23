// Headless RED->GREEN proof of the ASSET-RELINK mutation (Lane L3 檔案/專案 — the WRITE side that
// sits on asset_index's missing-asset READ side). asset_index_selftest pins that the index correctly
// FINDS a missing key; THIS golden pins that relink correctly REWRITES it project-wide:
//   1. return count == the number of references that existed: the Lib: key on 3 children is rewritten
//      in ONE relink call (childRefs == 3, soundtrack == false), and the soundtrack — the OTHER asset
//      kind, an abs path — is rewritten by its OWN abs→abs relink call (childRefs == 0, soundtrack ==
//      true). Two calls because the two kinds resolve by DIFFERENT rules (Lib: key via the resolver;
//      soundtrack via fileExists on the path), exactly as asset_index models them.
//   2. re-index after relink shows BOTH old refs GONE and BOTH new refs PRESENT, with the right
//      resolve status (the new Lib: key + new soundtrack path are each backed by a real temp file).
//   3. zero-missing: after relinking, none of the relink endpoints appear in the work list
//      (assetidx::missing() retains only the deliberately-unbacked UNRELATED key).
//   4. round-trip stable: the relinked project saves→loads→re-indexes identically (the rewritten
//      keys + soundtrack survive the production .swproj disk trip).
//
// The resolver is INJECTED (the seam to platform::resolveAssetPath): the test backs the NEW key with
// a real temp file so it resolves, while the OLD key never had one → headless + deterministic, no
// SW_ASSETS_DIR.
//
// injectBug runs a DELIBERATELY-FAULTY rewrite that SKIPS one child instance (the production
// relinkAsset stays pure — the bug lives only here, never in business code). Result: the count is
// short by one AND a stale reference to the old key survives → assertions 1 + 2 + 3 FAIL → RED.
// Teeth bite the REAL property: a relink that misses an instance leaves a broken reference behind,
// exactly what the relink work list exists to prevent.
#include <unistd.h>  // getpid

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "app/asset_index.h"             // buildAssetIndex / AssetIndex (re-index after relink)
#include "app/asset_relink.h"            // relinkAsset (the mutation under test)
#include "runtime/compound_graph.h"      // SymbolLibrary / Symbol / SymbolChild
#include "runtime/compound_save.h"       // saveLibToFile / loadLibFromFile
#include "runtime/graph.h"               // findSpec
#include "runtime/graph_bridge.h"        // atomicSymbolFromSpec
#include "runtime/selftest_registry.h"   // REGISTER_SELFTESTS

namespace fs = std::filesystem;

namespace sw {
namespace {

// Test-scoped temp dir + the real file that makes the NEW Lib: key resolve after relink.
fs::path g_dir;
std::string g_newKey;       // the Lib: key that maps to a real file (post-relink target)
std::string g_newKeyPath;   // that real file's abs path

// The injected resolver (the platform::resolveAssetPath seam): the NEW Lib: key → the real temp file,
// everything else (the OLD/missing Lib: key) → a path that does NOT exist (resolves=false). The
// soundtrack is NOT a Lib: key — it's an abs path that asset_index checks with fileExists() directly,
// so the resolver never sees it; the soundtrack's new path is a real temp file created in main().
std::string testResolve(const std::string& key) {
  if (key == g_newKey) return g_newKeyPath;
  return (g_dir / ("missing_" + key + ".bin")).string();  // never created → resolves=false
}

const char* kOldKey = "Lib:images/old/missing-asset.png";   // referenced by 3 children (the relink target)
const char* kNewKey = "Lib:images/new/relinked-asset.png";  // the Lib: relink target (backed by a file)

// The soundtrack is the OTHER kind of asset (an abs external path). It gets its OWN relink (abs→abs),
// proving relink rewrites composition.soundtrackPath too. These two strings are filled in main() with
// real temp paths (old = a non-existent abs path; new = a created temp file).
std::string g_oldSoundtrack;
std::string g_newSoundtrack;

// Build a project: a Root holding THREE LoadImage children that ALL reference the OLD Lib: key (N=3
// reuse + the dedup target of relink), one carrying a non-asset string override (must be untouched by
// the rewrite), plus a FOURTH child referencing an UNRELATED key (must NOT be rewritten). Soundtrack
// set to the OLD abs path (its own relink target). Everything pointing at OLD is currently missing.
SymbolLibrary makeProject() {
  SymbolLibrary lib;
  lib.symbols["LoadImage"] = atomicSymbolFromSpec(*findSpec("LoadImage"));
  Symbol root;
  root.id = "Root";
  root.name = "Root";
  root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};

  SymbolChild a;  // old key
  a.id = 1;
  a.symbolId = "LoadImage";
  a.strOverrides["Path"] = kOldKey;

  SymbolChild b;  // SAME old key + a NON-ASSET string override (must survive untouched)
  b.id = 2;
  b.symbolId = "LoadImage";
  b.strOverrides["Path"] = kOldKey;
  b.strOverrides["SomeName"] = "not-an-asset-just-a-variable-name";  // no Lib: prefix → never touched

  SymbolChild c;  // SAME old key (third instance — relink must reach ALL of them)
  c.id = 3;
  c.symbolId = "LoadImage";
  c.strOverrides["Path"] = kOldKey;

  SymbolChild d;  // an UNRELATED key (must NOT be rewritten by a relink of kOldKey)
  d.id = 4;
  d.symbolId = "LoadImage";
  d.strOverrides["Path"] = "Lib:images/other/untouched.png";

  root.children = {a, b, c, d};
  root.connections = {{1, "out", kSymbolBoundary, "out"}};
  lib.symbols[root.id] = root;
  lib.rootId = "Root";
  lib.composition.soundtrackPath = g_oldSoundtrack;  // the soundtrack's own (abs) relink target
  return lib;
}

// The DELIBERATELY-FAULTY child rewrite for -bug: rewrites the old key on every child EXCEPT it skips
// the FIRST matching instance. Mirrors relinkAsset's child loop so the bug is "missed one instance",
// the realest relink failure. Lives ONLY in the test — production relinkAsset stays pure.
assetrelink::RelinkResult relinkSkippingOne(SymbolLibrary& lib, const std::string& oldKey,
                                            const std::string& newKey) {
  assetrelink::RelinkResult r;
  bool skippedOne = false;
  for (auto& [symId, sym] : lib.symbols) {
    (void)symId;
    for (auto& child : sym.children) {
      for (auto& [slotId, value] : child.strOverrides) {
        (void)slotId;
        if (value == oldKey) {
          if (!skippedOne) { skippedOne = true; continue; }  // BUG: leave the first match stale
          value = newKey;
          ++r.childRefs;
        }
      }
    }
  }
  return r;
}

}  // namespace

int runAssetRelinkSelfTest(bool injectBug) {
  std::error_code ec;
  g_dir = fs::temp_directory_path() / ("sw_assetrelink_" + std::to_string(::getpid()));
  fs::remove_all(g_dir, ec);
  fs::create_directories(g_dir, ec);

  // The real file the NEW Lib: key resolves to.
  g_newKey = kNewKey;
  g_newKeyPath = (g_dir / "relinked.png").string();
  { std::ofstream(g_newKeyPath, std::ios::binary) << "PNGDATA"; }
  // The soundtrack's old (missing) abs path + its new (real) abs path. asset_index checks the
  // soundtrack with fileExists() on the path itself, so the NEW path must be a created temp file.
  g_oldSoundtrack = (g_dir / "old_track.wav").string();  // never created → missing pre-relink
  g_newSoundtrack = (g_dir / "new_track.wav").string();
  { std::ofstream(g_newSoundtrack, std::ios::binary) << "WAVDATA"; }

  SymbolLibrary lib = makeProject();

  // Sanity precondition (framing, not an assertion target): pre-relink BOTH the old Lib: key AND the
  // old soundtrack are missing.
  assetidx::AssetIndex before = assetidx::buildAssetIndex(lib, testResolve);
  bool preOldMissing = before.references(kOldKey) && before.isMissing(kOldKey) &&
                       before.references(g_oldSoundtrack) && before.isMissing(g_oldSoundtrack);

  // --- THE MUTATION ------------------------------------------------------------------------------
  // (a) the Lib: image key, referenced by 3 children (bug path skips one; good path = pure production
  //     relink). (b) the soundtrack, its own abs→abs relink — proving relink rewrites soundtrackPath.
  assetrelink::RelinkResult res =
      injectBug ? relinkSkippingOne(lib, kOldKey, kNewKey) : assetrelink::relinkAsset(lib, kOldKey, kNewKey);
  assetrelink::RelinkResult stRes = assetrelink::relinkAsset(lib, g_oldSoundtrack, g_newSoundtrack);

  // --- 1: return count == number of references that existed -------------------------------------
  // children: 3 instances of the Lib: key (soundtrack=false for THAT call); soundtrack relink: the
  // one composition path (childRefs=0, soundtrack=true, total=1).
  bool countOk = res.childRefs == 3 && !res.soundtrack && res.total() == 3 &&
                 stRes.childRefs == 0 && stRes.soundtrack && stRes.total() == 1;

  // --- 2: re-index shows OLD refs GONE + NEW refs PRESENT (and resolving) -----------------------
  assetidx::AssetIndex after = assetidx::buildAssetIndex(lib, testResolve);
  bool oldGone = !after.references(kOldKey) && !after.references(g_oldSoundtrack);
  bool newPresent = after.references(kNewKey) && after.references(g_newSoundtrack);
  bool newResolves = newPresent && !after.isMissing(kNewKey) && !after.isMissing(g_newSoundtrack);
  bool unrelatedUntouched = after.references("Lib:images/other/untouched.png");
  bool reindexOk = oldGone && newPresent && newResolves && unrelatedUntouched;

  // --- 3: zero-missing — the relinked assets no longer appear in the work list ------------------
  // (the unrelated key never had a file → it is the expected residual miss, NOT a relink target).
  auto miss = after.missing();
  bool relinkedNotMissing = true;
  for (const auto& m : miss)
    if (m.key == kOldKey || m.key == kNewKey || m.key == g_oldSoundtrack || m.key == g_newSoundtrack)
      relinkedNotMissing = false;  // none of the four relink endpoints should be missing
  bool zeroMissingOk = relinkedNotMissing;

  // --- 4: round-trip stable — save→load→re-index gives the SAME (relinked) set ------------------
  const fs::path proj = g_dir / "relinked.swproj";
  bool wrote = saveLibToFile(proj.string(), lib);
  SymbolLibrary back;
  bool loadOk = loadLibFromFile(proj.string(), back, nullptr);
  assetidx::AssetIndex idx2 = assetidx::buildAssetIndex(back, testResolve);
  bool roundTripOk = wrote && loadOk && !idx2.references(kOldKey) && idx2.references(kNewKey) &&
                     !idx2.isMissing(kNewKey) && !idx2.references(g_oldSoundtrack) &&
                     idx2.references(g_newSoundtrack) && !idx2.isMissing(g_newSoundtrack) &&
                     idx2.references("Lib:images/other/untouched.png");

  fs::remove_all(g_dir, ec);

  bool pass = preOldMissing && countOk && reindexOk && zeroMissingOk && roundTripOk;
  printf("[selftest-asset-relink] preMissing=%d count(child=%d snd=%d stCall_snd=%d)=%d "
         "reindex(oldGone=%d newPresent=%d newResolves=%d unrelated=%d)=%d zeroMissing=%d "
         "roundTrip=%d -> %s\n",
         preOldMissing ? 1 : 0, res.childRefs, res.soundtrack ? 1 : 0, stRes.soundtrack ? 1 : 0,
         countOk ? 1 : 0, oldGone ? 1 : 0, newPresent ? 1 : 0, newResolves ? 1 : 0,
         unrelatedUntouched ? 1 : 0, reindexOk ? 1 : 0, zeroMissingOk ? 1 : 0, roundTripOk ? 1 : 0,
         pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/323, {"asset-relink", runAssetRelinkSelfTest});

}  // namespace sw
