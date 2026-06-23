// Headless RED->GREEN proof of L6 維運 backup RETENTION + RESTORE (auto_backup_restore.cpp).
//
//   RETENTION (TiXL ReduceNumberOfBackups :481-519): create K=7 backups (#00000..#00006) with
//   thinning DISABLED, then run reduceNumberOfBackups(density=3) once and assert EXACTLY the
//   TiXL-thinned survivors remain: {0,2,3,4,5,6} (index #00001 thinned). The thinned dir AND its
//   bundled soundtrack are gone from disk; the latest (#00006) and the first-ever (#00000) survive.
//
//   RESTORE (TiXL RestoreLatestForProject :262-300): restoreLatestBackup into a FRESH target dir →
//     • the restored project.swproj reloads structurally-identical to the LATEST surviving backup
//       (canonical libToJsonV2 equality — same property as the round-trip golden);
//     • the bundled soundtrack is copied back beside it;
//     • the restored project's composition.soundtrackPath is REWRITTEN to that restored sibling
//       (the named restore-path fork) — i.e. it points at a file that actually exists in the target.
//
// injectBug RESTORES AN OLDER backup (#00000) instead of the latest and skips the path rewrite →
//   • "restored == latest" FAILS (stale recovery), AND
//   • "soundtrackPath points at an existing restored file" FAILS (dangling asset).
// Teeth bite the REAL recovery properties: a restore that returns stale state or a dead asset path
// is a broken restore. Do NOT flip the result — --bite requires the refuter to FAIL.
#include "app/auto_backup.h"

#include <unistd.h>  // getpid

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "runtime/compound_save.h"      // loadLibFromFile / libToJsonV2
#include "runtime/graph.h"              // findSpec
#include "runtime/graph_bridge.h"       // atomicSymbolFromSpec
#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS

namespace fs = std::filesystem;

namespace sw::backup {
namespace {

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
  lib.composition.soundtrackPath = soundtrackPath;
  return lib;
}

int indexOf(const std::string& name) {
  if (name.size() < 7 || name[0] != '#') return -1;
  int idx = 0;
  for (int i = 1; i <= 5; ++i) {
    if (name[i] < '0' || name[i] > '9') return -1;
    idx = idx * 10 + (name[i] - '0');
  }
  return name[6] == '-' ? idx : -1;
}

// All backup indices currently on disk, sorted ascending.
std::vector<int> indicesOnDisk(const std::string& backupDir) {
  std::error_code ec;
  std::vector<int> out;
  for (const auto& e : fs::directory_iterator(fs::path(backupDir), ec)) {
    if (!e.is_directory(ec)) continue;
    const int idx = indexOf(e.path().filename().string());
    if (idx >= 0) out.push_back(idx);
  }
  std::sort(out.begin(), out.end());
  return out;
}

// The dir for a given backup index, or "" if absent.
std::string dirForIndex(const std::string& backupDir, int want) {
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(fs::path(backupDir), ec)) {
    if (!e.is_directory(ec)) continue;
    if (indexOf(e.path().filename().string()) == want) return e.path().string();
  }
  return "";
}

}  // namespace

int runBackupRestoreSelfTest(bool injectBug) {
  std::error_code ec;
  const fs::path root =
      fs::temp_directory_path() / ("sw_backup_restore_" + std::to_string(::getpid()));
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);
  const std::string projectRoot = root.string();
  const std::string swprojName = "project.swproj";

  // External soundtrack asset living outside the project tree.
  const fs::path assetSrc = root / "external_soundtrack.wav";
  {
    std::ofstream a(assetSrc, std::ios::binary);
    a << "RIFFsw_fake_wav_payload_distinct_bytes_0123456789";
  }
  const std::string soundtrack = assetSrc.string();

  // --- build K=7 distinct backups (#00000..#00006), thinning DISABLED so all survive ----------
  AutoBackup writer(AutoBackupConfig{/*secondsBetweenSaves=*/0.0, /*enabled=*/true,
                                     /*backupDensity=*/0});
  const std::string dir = AutoBackup::backupDir(projectRoot);
  for (int k = 0; k < 7; ++k) {
    // distinct content per backup → no dedup; each advances the index.
    writer.checkForSave(projectRoot, swprojName, makeLib(1.0f + k, soundtrack),
                        /*dirty=*/true, /*now=*/100.0 * (k + 1));
  }
  std::vector<int> beforeReduce = indicesOnDisk(dir);
  bool builtAll = (beforeReduce == std::vector<int>{0, 1, 2, 3, 4, 5, 6});

  // remember #00001's bundled asset path (it is the one TiXL thins at H=6) to prove the asset is
  // deleted alongside the dir.
  const std::string thinnedDir = dirForIndex(dir, 1);
  const fs::path thinnedAsset = fs::path(thinnedDir) / assetSrc.filename();
  bool thinnedAssetExistedBefore = fs::is_regular_file(thinnedAsset, ec);

  // --- retention: thin once at density=3; TiXL keeps {0,2,3,4,5,6} for highestIndex=6 ----------
  int deletedCount = AutoBackup::reduceNumberOfBackups(dir, /*density=*/3);
  std::vector<int> afterReduce = indicesOnDisk(dir);
  bool retentionOk = (afterReduce == std::vector<int>{0, 2, 3, 4, 5, 6}) && deletedCount == 1;
  // the thinned backup's dir AND its bundled asset are gone (complete-unit deletion).
  bool thinnedGone = thinnedAssetExistedBefore && !fs::exists(fs::path(thinnedDir), ec) &&
                     !fs::is_regular_file(thinnedAsset, ec);
  // latest (#00006) and first-ever (#00000) always survive.
  bool keptEnds = !dirForIndex(dir, 6).empty() && !dirForIndex(dir, 0).empty();

  // The LATEST surviving backup's STRUCTURAL canonical form — restore must reproduce this. We
  // null the soundtrackPath before canonicalizing so the deliberately-rewritten path (the named
  // restore fork) does NOT count as a structural difference; the rewrite is asserted SEPARATELY by
  // restorePathRelinked. The load-bearing staleness signal that remains is the Const override value
  // (#00006 = 7.0 vs an older #00000 = 1.0) — so the bug variant (restoring #00000) still mismatches.
  const std::string latestDir = AutoBackup::latestBackupDirOnDisk(dir);
  SymbolLibrary latestLib;
  bool latestLoad = loadLibFromFile((fs::path(latestDir) / swprojName).string(), latestLib, nullptr);
  if (latestLoad) latestLib.composition.soundtrackPath = "";
  const std::string latestCanonical = latestLoad ? libToJsonV2(latestLib) : "";

  // --- restore into a fresh target -----------------------------------------------------------
  const fs::path target = root / "restored";
  fs::create_directories(target, ec);

  RestoreOutcome ro;
  std::string restoredCanonical;
  std::string restoredSoundtrackPath;
  bool restoredAssetExists = false;

  if (!injectBug) {
    ro = restoreLatestBackup(dir, target.string(), swprojName);
  } else {
    // BUG: restore an OLDER backup (#00000) and skip the path rewrite — simulates a recovery that
    // hands back stale state with a dangling external asset path.
    const std::string olderDir = dirForIndex(dir, 0);
    SymbolLibrary older;
    loadLibFromFile((fs::path(olderDir) / swprojName).string(), older, nullptr);
    // intentionally NOT rewriting soundtrackPath and NOT copying the asset into target.
    saveLibToFile((target / swprojName).string(), older);
    ro = RestoreOutcome::Restored;  // pretend success — the assertions below catch the lie.
  }

  // Read back the restored project. structural compare nulls the path (see latestCanonical above);
  // the path-relink check uses the ACTUAL stored path before nulling.
  SymbolLibrary back;
  bool bl = loadLibFromFile((target / swprojName).string(), back, nullptr);
  restoredSoundtrackPath = bl ? back.composition.soundtrackPath : "";
  restoredAssetExists = !restoredSoundtrackPath.empty() &&
                        fs::is_regular_file(fs::path(restoredSoundtrackPath), ec);
  if (bl) back.composition.soundtrackPath = "";
  restoredCanonical = bl ? libToJsonV2(back) : "";

  bool restoredOutcomeOk = (ro == RestoreOutcome::Restored);
  // restored project reloads structurally-identical to the LATEST backup (NOT an older one). The
  // bug restores #00000 (Const=1.0) → structural mismatch vs the latest #00006 (Const=7.0).
  bool restoreMatchesLatest = !restoredCanonical.empty() && restoredCanonical == latestCanonical;
  // The path-rewrite invariant: the restored soundtrackPath points at a file that EXISTS AND lives
  // INSIDE the target dir (a self-contained restored unit). The bug skips the copy+rewrite, leaving
  // the path pointing at the original external asset OUTSIDE target → this tooth bites too.
  bool pathInsideTarget = false;
  if (restoredAssetExists) {
    const fs::path p = fs::weakly_canonical(fs::path(restoredSoundtrackPath), ec);
    const fs::path t = fs::weakly_canonical(target, ec);
    pathInsideTarget = p.string().rfind(t.string(), 0) == 0;
  }
  bool restorePathRelinked = restoredAssetExists && pathInsideTarget;

  fs::remove_all(root, ec);

  bool pass = builtAll && retentionOk && thinnedGone && keptEnds && latestLoad &&
              restoredOutcomeOk && restoreMatchesLatest && restorePathRelinked;
  printf("[selftest-backup-restore] builtAll=%d retention{0,2..6}=%d(del=%d) thinnedGone=%d "
         "keptEnds=%d latestLoad=%d restored=%d matchesLatest=%d pathRelinked=%d (bug=%d) -> %s\n",
         builtAll, retentionOk, deletedCount, thinnedGone, keptEnds, latestLoad, restoredOutcomeOk,
         restoreMatchesLatest, restorePathRelinked, injectBug, pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/324, {"backup-restore", runBackupRestoreSelfTest});

}  // namespace sw::backup
