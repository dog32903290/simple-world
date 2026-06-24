// Headless RED->GREEN proof of the AVAILABLE-asset enumeration + the click-to-create-op action
// (EXPERIENCE_SCOPE_GAPS #2, the layer the asset browser UI sits on). asset_index walks a PROJECT for
// the keys it references; THIS is the complement — walk the asset library ON DISK for the keys a user
// can ADD — plus the undoable create-op the browser fires when an asset is clicked.
//
// The asset ROOT is a real temp dir the test builds (the seam to platform::assetLibraryRoot is just a
// path), so this is headless + deterministic without needing SW_ASSETS_DIR.
//
// Asserts:
//   1. enumerateAvailableAssets returns EXACTLY the real files as sorted `Lib:` keys — nested path
//      joined with '/', a hidden file excluded, directories not listed, extension lowercased.
//   2. makeLoadImageChild yields a LoadImage child carrying strOverrides["Path"] == the chosen key.
//   3. UNDOABLE ROUND-TRIP through the REAL command stack: AddChildCommand(child) on a Root grows its
//      child count by one AND the new child carries the Path override; undo REMOVES it (count back to
//      baseline, no residue). This is the side-effect + undo the task demands, proven on the same
//      AddChildCommand the UI pushes.
//
// injectBug strips the Path override from the created child (a load-op that forgot which asset) →
// assertions 2 and 3's "carries the override" leg FAIL. Teeth bite the REAL property: a create-op that
// loses the key would silently load the DEFAULT asset instead of the one the user clicked.
#include <unistd.h>  // getpid

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "app/asset_library.h"
#include "app/graph_commands.h"          // AddChildCommand
#include "runtime/compound_graph.h"      // SymbolLibrary / Symbol / SymbolChild / nextFreeChildId
#include "runtime/graph.h"               // findSpec
#include "runtime/graph_bridge.h"        // atomicSymbolFromSpec
#include "runtime/selftest_registry.h"   // REGISTER_SELFTESTS

namespace fs = std::filesystem;

namespace sw {
namespace {

// Build a one-symbol project: a Root with N existing children (the baseline the create-op grows).
SymbolLibrary makeProject(int baselineChildren) {
  SymbolLibrary lib;
  lib.symbols["LoadImage"] = atomicSymbolFromSpec(*findSpec("LoadImage"));
  Symbol root;
  root.id = "Root";
  root.name = "Root";
  root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  for (int i = 0; i < baselineChildren; ++i) {
    SymbolChild c;
    c.id = i + 1;
    c.symbolId = "LoadImage";
    root.children.push_back(c);
  }
  root.nextChildId = baselineChildren + 1;
  lib.symbols[root.id] = root;
  lib.rootId = "Root";
  return lib;
}

const Symbol& root(const SymbolLibrary& lib) { return lib.symbols.at("Root"); }

}  // namespace

int runAssetLibrarySelfTest(bool injectBug) {
  std::error_code ec;
  fs::path dir = fs::temp_directory_path() / ("sw_assetlib_" + std::to_string(::getpid()));
  fs::remove_all(dir, ec);

  // A small asset tree: two nested image files, a top-level file, a hidden file (excluded), and an
  // EMPTY subdir (no file → contributes nothing). Mixed-case extension proves the lowercasing.
  fs::create_directories(dir / "images" / "basic", ec);
  fs::create_directories(dir / "empty", ec);
  auto touch = [](const fs::path& p) { std::ofstream(p, std::ios::binary) << "DATA"; };
  touch(dir / "images" / "basic" / "perlin.png");
  touch(dir / "images" / "basic" / "ramp.PNG");  // mixed case → ext "png"
  touch(dir / "logo.jpg");
  touch(dir / ".hidden.png");  // hidden → excluded

  // --- 1: enumeration is EXACTLY the real files as sorted Lib: keys ------------------------------
  auto avail = assetlib::enumerateAvailableAssets(dir.string());
  // Expect 3 (the two nested + the top-level; hidden out, dirs out), sorted lexicographically:
  //   Lib:images/basic/perlin.png < Lib:images/basic/ramp.PNG < Lib:logo.jpg
  bool countOk = avail.size() == 3;
  bool orderOk = countOk &&
                 avail[0].key == "Lib:images/basic/perlin.png" &&
                 avail[1].key == "Lib:images/basic/ramp.PNG" &&
                 avail[2].key == "Lib:logo.jpg";
  bool extOk = countOk && avail[0].extension == "png" && avail[1].extension == "png" &&
               avail[2].extension == "jpg";
  // The hidden file must be absent from the keys.
  bool hiddenExcluded = true;
  for (const auto& a : avail)
    if (a.key.find(".hidden") != std::string::npos) hiddenExcluded = false;
  bool emptyDirOk = true;  // already covered by countOk==3 (the empty dir adds nothing)
  bool enumOk = countOk && orderOk && extOk && hiddenExcluded && emptyDirOk;

  // --- 2: makeLoadImageChild wires the chosen key into strOverrides["Path"] ----------------------
  const std::string pickKey = "Lib:images/basic/perlin.png";
  SymbolChild child = assetlib::makeLoadImageChild(pickKey, 120.0f, 80.0f);
  if (injectBug) child.strOverrides.erase("Path");  // forget which asset → the real bug
  bool isLoadImage = child.symbolId == "LoadImage";
  auto pit = child.strOverrides.find("Path");
  bool pathWired = pit != child.strOverrides.end() && pit->second == pickKey;
  bool createPayloadOk = isLoadImage && pathWired;

  // --- 3: undoable round-trip through the REAL AddChildCommand -----------------------------------
  SymbolLibrary lib = makeProject(/*baselineChildren=*/2);
  const size_t baseN = root(lib).children.size();  // 2

  // Assign the real id (what the UI does via nextFreeChildId) and push the SAME command the UI pushes.
  child.id = nextFreeChildId(*lib.find("Root"));
  AddChildCommand add(lib, "Root", child);
  add.doIt();
  const size_t afterN = root(lib).children.size();
  bool grew = afterN == baseN + 1;
  // The newly-added child must carry the Path override (the asset key survived into the graph).
  bool addedCarriesPath = false;
  for (const auto& c : root(lib).children) {
    if (c.id == child.id) {
      auto it = c.strOverrides.find("Path");
      addedCarriesPath = it != c.strOverrides.end() && it->second == pickKey;
    }
  }
  add.undo();
  const size_t undoN = root(lib).children.size();
  bool undone = undoN == baseN;  // back to baseline, op removed
  bool roundTripOk = grew && addedCarriesPath && undone;

  fs::remove_all(dir, ec);

  bool pass = enumOk && createPayloadOk && roundTripOk;
  printf("[selftest-asset-library] enum(count=%d order=%d ext=%d hiddenOut=%d)=%d "
         "createPayload(loadimage=%d pathWired=%d)=%d "
         "roundTrip(grew=%d carriesPath=%d undone=%d)=%d -> %s\n",
         countOk ? 1 : 0, orderOk ? 1 : 0, extOk ? 1 : 0, hiddenExcluded ? 1 : 0, enumOk ? 1 : 0,
         isLoadImage ? 1 : 0, pathWired ? 1 : 0, createPayloadOk ? 1 : 0, grew ? 1 : 0,
         addedCarriesPath ? 1 : 0, undone ? 1 : 0, roundTripOk ? 1 : 0, pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/341, {"asset-library", runAssetLibrarySelfTest});

}  // namespace sw
