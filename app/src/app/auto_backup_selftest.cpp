// Headless RED->GREEN proof of app/auto_backup (blueprint §2.1 runAutoBackupSelfTest). Proves the
// COMPLETE restorable unit (the .swproj AND its referenced external soundtrack asset), the
// disk-derived restart-safe index, ms-timestamp naming, byte-compare dedup, and the interval gate.
//
//   1. round-trip + asset-sibling: a project referencing an EXTERNAL soundtrack → one backup named
//      "#00000-<yyyy_MM_dd-HH_mm_ss_fff>"; the backup dir holds BOTH the .swproj (reloads
//      structurally-identical via libToJsonV2) AND a byte-identical copy of the soundtrack file
//      (the complete restorable unit — TiXL's whole-tree intent, not just the project file).
//   2. index advance: a real content change → a SECOND backup (#00001).
//   3. dedup: an unchanged re-check writes NO new index (candidate byte-equal to the latest
//      ON-DISK backup → dropped, previous mtime touched).
//   4. interval gate: a dirty call before secondsBetweenSaves elapsed → Skipped.
//   5. restart-safety: a FRESH AutoBackup over the same on-disk dir continues the index from disk
//      (writes #00002, does NOT reset to #00000 / overwrite the prior backups).
//
// injectBug DELETES the bundled soundtrack from the backup before the complete-unit assertion → the
// assetBundled check FAILS (teeth bite the REAL fidelity property the refuter named: a backup that
// loses a referenced external asset is lossy — exactly the BLOCK-1 failure).
#include "app/auto_backup.h"

#include <unistd.h>  // getpid

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "runtime/compound_save.h"  // loadLibFromFile / libToJsonV2
#include "runtime/graph.h"          // findSpec
#include "runtime/graph_bridge.h"   // atomicSymbolFromSpec
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS (leaf-local registration)

namespace fs = std::filesystem;

namespace sw::backup {
namespace {

// A small but non-trivial library: Root holds a Const child (override value) feeding its output,
// PLUS an external soundtrack path on the composition (the asset a faithful backup must capture).
SymbolLibrary makeLib(float constValue, const std::string& soundtrackPath) {
  SymbolLibrary lib;
  lib.symbols["Const"] = atomicSymbolFromSpec(*findSpec("Const"));
  Symbol root;
  root.id = "Root";
  root.name = "Root";
  root.atomic = false;
  root.outputDefs = {{"out", "out", "Float", 0.0f}};
  SymbolChild c;
  c.id = 1;
  c.symbolId = "Const";
  c.overrides["value"] = constValue;
  root.children = {c};
  root.connections = {{1, "out", kSymbolBoundary, "out"}};
  lib.symbols[root.id] = root;
  lib.rootId = "Root";
  lib.composition.soundtrackPath = soundtrackPath;  // the external asset reference
  return lib;
}

// The single .swproj inside whatever the controller reports as the active backup directory.
fs::path activeSwproj(const AutoBackup& bk, const std::string& name) {
  return fs::path(bk.activeBackupPath()) / name;
}

bool fileBytesEqual(const fs::path& a, const fs::path& b) {
  std::error_code ec;
  if (!fs::exists(a, ec) || !fs::exists(b, ec)) return false;
  if (fs::file_size(a, ec) != fs::file_size(b, ec)) return false;
  std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
  std::istreambuf_iterator<char> ia(fa), ib(fb), end;
  return std::equal(ia, end, ib);
}

}  // namespace

int runAutoBackupSelfTest(bool injectBug) {
  std::error_code ec;
  const fs::path root = fs::temp_directory_path() / ("sw_autobackup_" + std::to_string(::getpid()));
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);
  const std::string projectRoot = root.string();
  const std::string swprojName = "project.swproj";

  // An EXTERNAL soundtrack asset living OUTSIDE the project tree (mirrors an NFD pick anywhere on
  // disk). A faithful backup must bundle it; a lossy single-file backup would lose it on restore.
  const fs::path assetSrc = root / "external_soundtrack.wav";
  {
    std::ofstream a(assetSrc, std::ios::binary);
    a << "RIFFsw_fake_wav_payload_distinct_bytes_0123456789";
  }
  const std::string soundtrack = assetSrc.string();

  // interval 0 so a dirty call always passes the elapsed gate (except the explicit gate test).
  AutoBackup bk(AutoBackupConfig{/*secondsBetweenSaves=*/0.0, /*enabled=*/true});

  // --- 1: first backup + round-trip + asset-sibling bundling ----------------------------------
  SymbolLibrary lib0 = makeLib(5.0f, soundtrack);
  BackupOutcome o1 = bk.checkForSave(projectRoot, swprojName, lib0, /*dirty=*/true, /*now=*/100.0);
  bool wrote1 = (o1 == BackupOutcome::Wrote);

  const fs::path written = activeSwproj(bk, swprojName);
  bool dirExists = !bk.activeBackupPath().empty() && fs::is_directory(bk.activeBackupPath(), ec);
  bool fileExists = fs::is_regular_file(written, ec);
  // name carries the monotonic #00000 tag AND the underscore+ms timestamp shape (TiXL :108).
  const std::string name0 = fs::path(bk.activeBackupPath()).filename().string();
  bool namedOk = name0.rfind("#00000-", 0) == 0;
  // shape check: "#00000-YYYY_MM_DD-HH_MM_SS_fff" → length 7 + 23 = 30; last 4 chars are "_NNN".
  bool tsShapeOk = name0.size() == 30 && name0[name0.size() - 4] == '_';

  // The bundled soundtrack sibling — the COMPLETE-UNIT property. Under injectBug we DELETE it
  // (simulating a backup that lost the referenced asset = the BLOCK-1 lossy fork).
  const fs::path bundled = fs::path(bk.activeBackupPath()) / assetSrc.filename();
  if (injectBug) fs::remove(bundled, ec);
  bool assetBundled = fs::is_regular_file(bundled, ec) && fileBytesEqual(bundled, assetSrc);

  SymbolLibrary back;
  std::vector<std::string> warn;
  bool loadOk = loadLibFromFile(written.string(), back, &warn);
  // structural identity: the canonical v2 form of the reloaded lib equals the original's.
  bool roundTripOk = loadOk && libToJsonV2(back) == libToJsonV2(lib0);

  // --- 2: a real content change advances the index -------------------------------------------
  SymbolLibrary lib1 = makeLib(7.0f, soundtrack);  // different override → different bytes
  BackupOutcome o2 = bk.checkForSave(projectRoot, swprojName, lib1, /*dirty=*/true, /*now=*/200.0);
  bool advanced = (o2 == BackupOutcome::Wrote) &&
                  fs::path(bk.activeBackupPath()).filename().string().rfind("#00001-", 0) == 0;

  // --- 3: dedup — identical content does NOT advance the index --------------------------------
  const std::string activeBefore = bk.activeBackupPath();
  BackupOutcome o3 = bk.checkForSave(projectRoot, swprojName, lib1, /*dirty=*/true, /*now=*/300.0);
  bool dedupOk = (o3 == BackupOutcome::DedupTouched) &&
                 bk.activeBackupPath() == activeBefore;  // still pointing at #00001

  // --- 4: interval gate — dirty but not elapsed → Skipped ------------------------------------
  // Isolated project root: this controller writes its own #00000, which must NOT pollute the
  // disk-derived index the restart test (step 5) asserts on `projectRoot`.
  const fs::path gatedRoot = root / "gated";
  fs::create_directories(gatedRoot, ec);
  AutoBackup gated(AutoBackupConfig{/*secondsBetweenSaves=*/180.0, /*enabled=*/true});
  // first call passes (lastSave = -inf); the SECOND, 10s later, is inside the 180s window.
  gated.checkForSave(gatedRoot.string(), swprojName, makeLib(1.0f, ""), /*dirty=*/true,
                     /*now=*/1000.0);
  const std::string gatedActiveAfterFirst = gated.activeBackupPath();
  BackupOutcome g2 = gated.checkForSave(gatedRoot.string(), swprojName, makeLib(2.0f, ""),
                                        /*dirty=*/true, /*now=*/1010.0);
  bool gateOk = (g2 == BackupOutcome::Skipped) && gated.activeBackupPath() == gatedActiveAfterFirst;

  // --- 5: restart-safety — a FRESH controller continues the index from DISK ------------------
  // (the production failure the refuter named: an in-memory counter resets to #00000 on app
  // restart and collides/overwrites. A disk-derived index must continue at #00002.)
  AutoBackup restarted(AutoBackupConfig{/*secondsBetweenSaves=*/0.0, /*enabled=*/true});
  SymbolLibrary lib2 = makeLib(9.0f, soundtrack);  // distinct content so it isn't deduped
  BackupOutcome o5 =
      restarted.checkForSave(projectRoot, swprojName, lib2, /*dirty=*/true, /*now=*/2000.0);
  const std::string restartName = fs::path(restarted.activeBackupPath()).filename().string();
  bool restartSafe = (o5 == BackupOutcome::Wrote) && restartName.rfind("#00002-", 0) == 0 &&
                     fs::is_directory(activeBefore, ec);  // the prior #00001 was NOT overwritten

  fs::remove_all(root, ec);

  // teeth: under injectBug the bundled soundtrack was removed, so assetBundled MUST be false here
  // → pass becomes false → the process exits nonzero (the -bug variant bites the REAL fidelity
  // property: a backup that drops a referenced external asset). Do NOT flip the result: --bite
  // requires the refuter to FAIL (run_all_selftests.sh:43).
  bool pass = wrote1 && dirExists && fileExists && namedOk && tsShapeOk && assetBundled &&
              roundTripOk && advanced && dedupOk && gateOk && restartSafe;
  printf("[selftest-auto-backup] wrote1=%d dir=%d file=%d named#00000=%d tsShape=%d "
         "assetBundled=%d(bug=%d) roundTrip=%d advance#00001=%d dedup=%d gate=%d "
         "restartSafe#00002=%d -> %s\n",
         wrote1, dirExists, fileExists, namedOk, tsShapeOk, assetBundled, injectBug, roundTripOk,
         advanced, dedupOk, gateOk, restartSafe, pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/320, {"auto-backup", runAutoBackupSelfTest});

}  // namespace sw::backup
