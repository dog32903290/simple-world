// Headless RED->GREEN proof of the IMAGE-ASSET-REFERENCE round-trip through .swproj save→load
// (Lane L3 檔案/專案, AssetLibrary data-model first step). The two existing round-trip goldens cover
// the .swproj structure (compound_save_selftest: float/var-string overrides) and the COMPLETE
// restorable unit (auto_backup_selftest: soundtrack-asset bundling). NEITHER constructs an
// image-asset-referencing node, writes it to a real .swproj file, reloads it, and asserts the asset
// KEY survived. This harness fills that gap — the data layer beneath any future asset browser.
//
// WHY THIS IS THE RIGHT FOUNDATION (not a reinvention):
//   sw's asset model (auto_backup.h:25-39): an image node references an asset by a "Lib:..." KEY
//   string (resolved under SW_ASSETS_DIR), stored as SymbolChild.strOverrides["Path"]
//   (compound_graph.h:101). The asset FILE is shared-install (TiXL's `Lib:` package) and is NOT
//   bundled per-backup — so the ONE thing a save MUST preserve for an image reference to survive a
//   restore is the KEY. This golden proves that key round-trips, byte-stable, through a real disk
//   file (saveLibToFile / loadLibFromFile — the production path), and that an image source carries
//   BOTH a string asset key AND its float params (Resolution enum / CustomW) across the trip.
//
// LoadImage (point_ops_loadimage.cpp:185-197) is sw's image SOURCE op: a String "Path" inputDef
// (strDef = the committed perlin asset) + Float "Resolution"/"CustomW"/"CustomH" inputDefs. It is
// the canonical asset-reference consumer, so the round-trip subject is faithful to production.
//
// ASSERTIONS:
//   1. asset-key round-trip: a Root holding a LoadImage child with strOverrides["Path"] = a custom
//      "Lib:..." key (PLUS a non-default Resolution + CustomW on the float rail) → saved to a real
//      temp .swproj, reloaded → the canonical libToJsonV2 form is identical, AND the reloaded
//      child's Path override + float overrides match exactly. (The key survives the disk trip.)
//   2. byte-stable on disk: the bytes WRITTEN equal libToJsonV2(reloaded) — the file the app wrote
//      reloads to the same canonical form (no drift in the asset-key serialization).
//   3. default-key zero-churn: a LoadImage child with NO Path override (spawn state — the strDef
//      default applies) does NOT emit a Path override into the file (the default lives in the spec,
//      compound_graph.h SlotDef.strDef, never duplicated onto the child → minimal file/diff).
//   4. CJK asset key: a "Lib:images/中文/雜訊.png" key round-trips byte-stable (柏為 WILL name
//      assets in Chinese; the patched crude_json parser must carry non-ASCII keys — same property
//      compound_save_selftest pins for symbol NAMES, here pinned for asset KEYS).
//
// injectBug TAMPERS the reloaded asset key (rewrites the Path override to a different asset) before
// the round-trip identity check → assertion 1 FAILS (teeth bite the REAL fidelity property: a save
// that does not preserve the exact asset key would silently load the WRONG image on restore).
#include <unistd.h>  // getpid

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "runtime/compound_graph.h"     // SymbolLibrary / Symbol / SymbolChild
#include "runtime/compound_save.h"      // saveLibToFile / loadLibFromFile / libToJsonV2
#include "runtime/graph.h"              // findSpec
#include "runtime/graph_bridge.h"       // atomicSymbolFromSpec
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS (leaf-local registration)

namespace fs = std::filesystem;

namespace sw {
namespace {

// A Root holding a single LoadImage child. `pathKey` non-empty → it rides strOverrides["Path"]
// (the asset reference). `resOverride`/`customW` (when >=0) ride the float `overrides` map — an
// image source carries both rails, so the round-trip subject is realistic, not float- or string-
// only. The atomic LoadImage def is regenerated from the registry on load (atomicSymbolFromSpec),
// so the loaded lib needs it in the source lib too for the strOverride "known def" scrub to keep
// the Path key (compound_load.cpp keeps only overrides whose slot exists on the referenced symbol).
SymbolLibrary makeImageLib(const std::string& pathKey, float resOverride, float customW) {
  SymbolLibrary lib;
  lib.symbols["LoadImage"] = atomicSymbolFromSpec(*findSpec("LoadImage"));
  Symbol root;
  root.id = "Root";
  root.name = "Root";
  root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild img;
  img.id = 1;
  img.symbolId = "LoadImage";
  if (!pathKey.empty()) img.strOverrides["Path"] = pathKey;   // the asset KEY (string rail)
  if (resOverride >= 0.0f) img.overrides["Resolution"] = resOverride;  // enum on the float rail
  if (customW >= 0.0f) img.overrides["CustomW"] = customW;             // float param
  root.children = {img};
  root.connections = {{1, "out", kSymbolBoundary, "out"}};
  lib.symbols[root.id] = root;
  lib.rootId = "Root";
  return lib;
}

std::string readFile(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

const SymbolChild* firstChild(const SymbolLibrary& lib) {
  const Symbol* r = lib.find("Root");
  return (r && !r->children.empty()) ? &r->children[0] : nullptr;
}

}  // namespace

int runAssetRefRoundTripSelfTest(bool injectBug) {
  std::error_code ec;
  const fs::path dir = fs::temp_directory_path() / ("sw_assetref_" + std::to_string(::getpid()));
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);

  // --- 1: asset-key + float-param round-trip through a REAL .swproj file ----------------------
  const std::string customKey = "Lib:images/basic/perlin-noise-rgb.png";  // a real "Lib:" key shape
  SymbolLibrary lib0 = makeImageLib(customKey, /*resOverride=*/2.0f, /*customW=*/1024.0f);
  const fs::path proj = dir / "asset.swproj";
  bool wrote = saveLibToFile(proj.string(), lib0);
  const std::string onDiskBytes = readFile(proj);

  SymbolLibrary back;
  std::vector<std::string> warn;
  bool loadOk = loadLibFromFile(proj.string(), back, &warn);

  // teeth: tamper the reloaded asset KEY (a save that loaded the WRONG image). The round-trip
  // identity + key-match assertions below then FAIL → process exits nonzero.
  if (injectBug) {
    SymbolChild* c = nullptr;
    Symbol* r = back.find("Root");
    if (r && !r->children.empty()) c = &r->children[0];
    if (c) c->strOverrides["Path"] = "Lib:images/WRONG/other.png";
  }

  const SymbolChild* rc = firstChild(back);
  bool keyOk = rc && rc->strOverrides.count("Path") && rc->strOverrides.at("Path") == customKey;
  bool floatOk = rc && rc->overrides.count("Resolution") && rc->overrides.at("Resolution") == 2.0f &&
                 rc->overrides.count("CustomW") && rc->overrides.at("CustomW") == 1024.0f;
  // canonical structural identity: the reloaded lib's v2 form equals the original's (the asset
  // reference + its params survived the disk trip with no drift). injectBug breaks this too.
  bool structOk = loadOk && libToJsonV2(back) == libToJsonV2(lib0);
  bool roundTripOk = wrote && loadOk && keyOk && floatOk && structOk;

  // --- 2: byte-stable on disk — the file the app WROTE reloads to the same canonical bytes ------
  // (compare the ON-DISK bytes to the reloaded canonical form; under injectBug `back` is tampered
  // so skip the equality there — assertion 1 already carries the teeth.)
  bool byteStableOnDisk = injectBug ? true : (onDiskBytes == libToJsonV2(back));

  // --- 3: default-key zero-churn — no Path override when the spec default applies ---------------
  SymbolLibrary defLib = makeImageLib(/*pathKey=*/"", /*resOverride=*/-1.0f, /*customW=*/-1.0f);
  const std::string defJson = libToJsonV2(defLib);
  // The default asset key lives ONLY in the spec's strDef; a spawn-state child must NOT duplicate it
  // into the file. (We assert the child carries no "Path" override key AND the default-asset string
  // is not written as a per-child value — it would only appear from a child override.)
  SymbolLibrary defBack;
  bool defLoadOk = libFromJsonAny(defJson, defBack, nullptr);
  const SymbolChild* dc = firstChild(defBack);
  bool defZeroChurn = defLoadOk && dc && dc->strOverrides.empty() &&
                      defJson.find("\"Path\"") == std::string::npos;  // no per-child Path emitted

  // --- 4: CJK asset key survives byte-stable ---------------------------------------------------
  const std::string cjkKey = "Lib:images/中文/雜訊.png";
  SymbolLibrary cjkLib = makeImageLib(cjkKey, /*resOverride=*/-1.0f, /*customW=*/-1.0f);
  const std::string cjkJson = libToJsonV2(cjkLib);
  SymbolLibrary cjkBack;
  bool cjkLoadOk = libFromJsonAny(cjkJson, cjkBack, nullptr);
  const SymbolChild* cc = firstChild(cjkBack);
  bool cjkOk = cjkLoadOk && cc && cc->strOverrides.count("Path") &&
               cc->strOverrides.at("Path") == cjkKey &&
               libToJsonV2(cjkBack) == cjkJson;  // byte-stable through the patched parser

  fs::remove_all(dir, ec);

  bool pass = roundTripOk && byteStableOnDisk && defZeroChurn && cjkOk;
  printf("[selftest-asset-ref] roundTrip(key=%d float=%d struct=%d)=%d byteStableDisk=%d "
         "defaultZeroChurn=%d cjkKey=%d -> %s\n",
         keyOk ? 1 : 0, floatOk ? 1 : 0, structOk ? 1 : 0, roundTripOk ? 1 : 0,
         byteStableOnDisk ? 1 : 0, defZeroChurn ? 1 : 0, cjkOk ? 1 : 0, pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/321, {"asset-ref", runAssetRefRoundTripSelfTest});

}  // namespace sw
