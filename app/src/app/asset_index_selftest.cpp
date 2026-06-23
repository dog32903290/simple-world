// Headless RED->GREEN proof of the ASSET-INDEX data model (Lane L3 檔案/專案, the non-UI layer the
// asset browser + a future missing-asset/relink check sit on). asset_ref_roundtrip_selftest pins
// that ONE asset key survives a disk round-trip; THIS golden is the layer above it — walk a whole
// project, collect the DEDUPED referenced-asset set, and resolve each against disk:
//   1. referenced-key set is EXACTLY right: a key reused on two children appears ONCE (dedup), the
//      soundtrack is included, and a non-asset string override (SetFloatVar VariableName, which also
//      lives in strOverrides) is EXCLUDED — the `Lib:` prefix is the discriminant, faithful to TiXL.
//   2. missing predicate is correct: a bogus `Lib:` key flags missing; the real key (backed by a
//      temp file the test creates + a resolver that points at it) passes; the soundtrack abs path
//      resolves to the real temp file.
//   3. round-trip stable: save→load→re-index gives the SAME key set (the index is computed off the
//      reloaded library, proving the keys it depends on survive the production .swproj disk trip).
//
// The resolver is INJECTED (the seam to platform::resolveAssetPath), so the test controls which keys
// "exist" via real temp files without needing SW_ASSETS_DIR — headless + deterministic.
//
// injectBug INVERTS the missing predicate (real key reported missing / bogus key reported present)
// → assertion 2 FAILS. Teeth bite the REAL property: a relink check that mis-resolves would either
// nag about a present asset or silently pass a broken reference.
#include <unistd.h>  // getpid

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "app/asset_index.h"
#include "runtime/compound_graph.h"     // SymbolLibrary / Symbol / SymbolChild
#include "runtime/compound_save.h"      // saveLibToFile / loadLibFromFile
#include "runtime/graph.h"              // findSpec
#include "runtime/graph_bridge.h"       // atomicSymbolFromSpec
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS

namespace fs = std::filesystem;

namespace sw {
namespace {

// Test-scoped temp dir + the real file that makes the "good" key / soundtrack resolve. Resolver
// reads these globals so it can be a plain free function (matching the fn-ptr seam signature).
fs::path g_dir;
std::string g_goodKey;       // the Lib: key that maps to a real file
std::string g_goodKeyPath;   // that real file's abs path

// The injected resolver (the platform::resolveAssetPath seam): the good key → the real temp file,
// everything else (the bogus key) → a path that does NOT exist.
std::string testResolve(const std::string& key) {
  if (key == g_goodKey) return g_goodKeyPath;
  return (g_dir / ("missing_" + key + ".bin")).string();  // never created → resolves=false
}

const char* kGoodKey = "Lib:images/basic/perlin-noise-rgb.png";
const char* kBogusKey = "Lib:images/WRONG/does-not-exist.png";

// Build a project: a Root holding TWO LoadImage children that BOTH reference the same good key
// (dedup target), a THIRD referencing the bogus key, plus a non-asset string override on one child
// (must NOT enter the index). Soundtrack set to the real temp file (resolves).
SymbolLibrary makeProject(const std::string& soundtrackAbs) {
  SymbolLibrary lib;
  lib.symbols["LoadImage"] = atomicSymbolFromSpec(*findSpec("LoadImage"));
  Symbol root;
  root.id = "Root";
  root.name = "Root";
  root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};

  SymbolChild a;  // good key
  a.id = 1;
  a.symbolId = "LoadImage";
  a.strOverrides["Path"] = kGoodKey;

  SymbolChild b;  // SAME good key (dedup) + a NON-ASSET string override (must be excluded)
  b.id = 2;
  b.symbolId = "LoadImage";
  b.strOverrides["Path"] = kGoodKey;
  b.strOverrides["SomeName"] = "not-an-asset-just-a-variable-name";  // no Lib: prefix → excluded

  SymbolChild c;  // bogus key (missing)
  c.id = 3;
  c.symbolId = "LoadImage";
  c.strOverrides["Path"] = kBogusKey;

  root.children = {a, b, c};
  root.connections = {{1, "out", kSymbolBoundary, "out"}};
  lib.symbols[root.id] = root;
  lib.rootId = "Root";
  lib.composition.soundtrackPath = soundtrackAbs;  // the one external asset
  return lib;
}

bool hasKey(const assetidx::AssetIndex& idx, const std::string& key) { return idx.references(key); }

}  // namespace

int runAssetIndexSelfTest(bool injectBug) {
  std::error_code ec;
  g_dir = fs::temp_directory_path() / ("sw_assetidx_" + std::to_string(::getpid()));
  fs::remove_all(g_dir, ec);
  fs::create_directories(g_dir, ec);

  // The real file the good key + soundtrack resolve to.
  g_goodKey = kGoodKey;
  g_goodKeyPath = (g_dir / "perlin.png").string();
  { std::ofstream(g_goodKeyPath, std::ios::binary) << "PNGDATA"; }
  const std::string soundtrack = (g_dir / "track.wav").string();
  { std::ofstream(soundtrack, std::ios::binary) << "WAVDATA"; }

  SymbolLibrary lib = makeProject(soundtrack);
  assetidx::AssetIndex idx = assetidx::buildAssetIndex(lib, testResolve);

  // --- 1: referenced-key set EXACTLY right (dedup + soundtrack in + non-asset string out) --------
  // Expect exactly 3 refs: good key (once, not twice), bogus key, soundtrack. The VariableName is
  // gone.
  bool sizeOk = idx.size() == 3;
  bool hasGood = hasKey(idx, kGoodKey);
  bool hasBogus = hasKey(idx, kBogusKey);
  bool hasSoundtrack = hasKey(idx, soundtrack);
  bool excludesNonAsset = !hasKey(idx, "not-an-asset-just-a-variable-name");
  bool setOk = sizeOk && hasGood && hasBogus && hasSoundtrack && excludesNonAsset;

  // --- 2: missing predicate correct ------------------------------------------------------------
  bool goodResolves = !idx.isMissing(kGoodKey);     // real file → resolves
  bool bogusMissing = idx.isMissing(kBogusKey);     // no file → missing
  bool soundtrackResolves = !idx.isMissing(soundtrack);
  // injectBug inverts the predicate's verdict on the two image keys (the relink check would lie).
  if (injectBug) {
    goodResolves = idx.isMissing(kGoodKey);   // claim the real asset is missing
    bogusMissing = !idx.isMissing(kBogusKey);  // claim the broken reference is fine
  }
  bool missingPredicateOk = goodResolves && bogusMissing && soundtrackResolves;
  // the relink work list = exactly {bogus key}
  auto miss = idx.missing();
  bool worklistOk = miss.size() == 1 && miss[0].key == kBogusKey;

  // --- 3: round-trip stable — save→load→re-index gives the SAME set ------------------------------
  const fs::path proj = g_dir / "proj.swproj";
  bool wrote = saveLibToFile(proj.string(), lib);
  SymbolLibrary back;
  bool loadOk = loadLibFromFile(proj.string(), back, nullptr);
  assetidx::AssetIndex idx2 = assetidx::buildAssetIndex(back, testResolve);
  bool roundTripOk = wrote && loadOk && idx2.size() == idx.size() && hasKey(idx2, kGoodKey) &&
                     hasKey(idx2, kBogusKey) && hasKey(idx2, soundtrack) &&
                     !hasKey(idx2, "not-an-asset-just-a-variable-name");

  fs::remove_all(g_dir, ec);

  bool pass = setOk && missingPredicateOk && worklistOk && roundTripOk;
  printf("[selftest-asset-index] set(size=%d dedupGood=%d bogus=%d soundtrack=%d exclNonAsset=%d)=%d "
         "missingPred(good=%d bogus=%d st=%d worklist=%d)=%d roundTrip=%d -> %s\n",
         sizeOk ? 1 : 0, hasGood ? 1 : 0, hasBogus ? 1 : 0, hasSoundtrack ? 1 : 0,
         excludesNonAsset ? 1 : 0, setOk ? 1 : 0, goodResolves ? 1 : 0, bogusMissing ? 1 : 0,
         soundtrackResolves ? 1 : 0, worklistOk ? 1 : 0, missingPredicateOk && worklistOk ? 1 : 0,
         roundTripOk ? 1 : 0, pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/322, {"asset-index", runAssetIndexSelfTest});

}  // namespace sw
