// app/auto_backup_restore — the READ/recovery half of L6 維運 backup: retention thinning
// (ReduceNumberOfBackups) + crash-recovery restore (RestoreLatestBackup). Split from auto_backup.cpp
// (the WRITE half) per ARCHITECTURE rule 4 (one file, one responsibility; <400 lines): writing a
// backup and pruning/restoring backups are distinct jobs over the same on-disk layout.
//
// TiXL ground truth: external/tixl/Editor/Gui/AutoBackup/AutoBackup.cs
//   • ReduceNumberOfBackups (:481-519) + GetSignificantBit (:521-541) — binary-thinning retention.
//   • RestoreLatestForProject (:262-300) — restore the highest-index backup of a project.
// See auto_backup.h for the named container fork (directory-per-backup instead of .zip) and the
// named restore-path fork (rewrite the external soundtrack's absolute path on restore).
//
// ZONE: app/ (same as auto_backup.cpp). Pure: std::filesystem + runtime/compound_save only.
#include "app/auto_backup.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>

#include "runtime/compound_graph.h"  // SymbolLibrary / CompositionSettings
#include "runtime/compound_save.h"   // loadLibFromFile / saveLibToFile

namespace fs = std::filesystem;

namespace sw::backup {
namespace {

// Parse the leading "#NNNNN" of a backup dir name → its index, or -1 if it isn't a backup name
// (same regex group as auto_backup.cpp; duplicated here so the read half stays independent of the
// write half — it's ~10 lines, cheaper than a shared-surface header). Requires the "-timestamp"
// tail so the `.pending` staging dir (starts with '.') never matches.
int indexOfBackupName(const std::string& name) {
  if (name.size() < 7 || name[0] != '#') return -1;
  int idx = 0;
  for (int i = 1; i <= 5; ++i) {
    if (name[i] < '0' || name[i] > '9') return -1;
    idx = idx * 10 + (name[i] - '0');
  }
  if (name[6] != '-') return -1;
  return idx;
}

// TiXL GetSignificantBit (:521-541), ported verbatim (faithfulness over a clever rewrite). Returns
// the index of the lowest bit that is set in (n+1) but NOT in n — i.e. the position where adding 1
// to n carries. Used with n = 0xffffff - backupIndex so RECENT backups (small i → large argument →
// high carry position) sit in higher "generations" and survive thinning longest.
int significantBit(int n) {
  bool a[32] = {false};
  int rest = n;
  while (rest > 0) {
    int h = 0;
    for (int b = 31; b >= 0; --b) {
      if ((rest >> b) & 1) { h = b; break; }
    }
    rest -= (1 << h);
    a[h] = true;
  }
  rest = n + 1;
  while (rest > 0) {
    int h = 0;
    for (int b = 31; b >= 0; --b) {
      if ((rest >> b) & 1) { h = b; break; }
    }
    rest -= (1 << h);
    if (!a[h]) return h;
  }
  return 0;
}

// The .swproj written inside a given backup directory.
fs::path swprojIn(const fs::path& backupSubdir, const std::string& swprojName) {
  return backupSubdir / swprojName;
}

}  // namespace

int AutoBackup::reduceNumberOfBackups(const std::string& backupDir, int density) {
  if (density <= 0) return 0;  // thinning disabled → keep everything (TiXL has no <=0 branch; our
                               // explicit fork so a test/caller can keep full history)
  std::error_code ec;
  // index → directory path, for every "#NNNNN-*" backup on disk.
  std::vector<std::pair<int, fs::path>> byIndex;
  int highestIndex = -1;
  for (const auto& e : fs::directory_iterator(fs::path(backupDir), ec)) {
    if (!e.is_directory(ec)) continue;
    const int idx = indexOfBackupName(e.path().filename().string());
    if (idx < 0) continue;
    if (idx > highestIndex) highestIndex = idx;
    byIndex.emplace_back(idx, e.path());
  }

  // TiXL thinning loop (:498-518). Walk from highestIndex-1 down to 0; the highest index is never
  // visited (always kept). A backup is KEPT when its generation `b` exceeds the running `limit`
  // (advancing limit every `density` such keeps); otherwise it is removed.
  int deleted = 0;
  int limit = 0;
  int limitCount = 0;
  for (int i = highestIndex - 1; i >= 0; --i) {
    const int b = significantBit(0xffffff - i) + 1;
    if (b > limit) {
      ++limitCount;
      if (limitCount >= density) {
        limitCount = 0;
        ++limit;
      }
    } else {
      // Remove backup #i (dir + bundled assets) if it exists on disk.
      for (const auto& [idx, path] : byIndex) {
        if (idx == i) {
          fs::remove_all(path, ec);
          if (!ec) ++deleted;
          break;
        }
      }
    }
  }
  return deleted;
}

RestoreOutcome restoreLatestBackup(const std::string& backupDir,
                                   const std::string& targetProjectRoot,
                                   const std::string& swprojName) {
  std::error_code ec;
  const std::string latest = AutoBackup::latestBackupDirOnDisk(backupDir);
  if (latest.empty()) return RestoreOutcome::NoBackup;  // TiXL :266-267 (no archive)

  const fs::path latestDir(latest);
  const fs::path backupSwproj = swprojIn(latestDir, swprojName);
  if (!fs::is_regular_file(backupSwproj, ec)) return RestoreOutcome::Failed;

  // Load the backup's project so we can rewrite the asset path through the canonical serializer
  // (the restored .swproj must reload structurally-identical to the backup — same path as the
  // round-trip golden — plus the one path rewrite). A corrupt backup → Failed (TiXL :294-298).
  SymbolLibrary lib;
  if (!loadLibFromFile(backupSwproj.string(), lib, nullptr)) return RestoreOutcome::Failed;

  const fs::path targetRoot(targetProjectRoot);
  fs::create_directories(targetRoot, ec);

  // Copy every bundled sibling asset back beside the project (TiXL extracts every zip entry into the
  // project folder, :276-292). Here the only non-.swproj sibling is the bundled soundtrack; copy it
  // and REWRITE the project's absolute soundtrackPath to the restored sibling (named restore-path
  // fork, see auto_backup.h). We match by base filename so a renamed/moved original still relinks.
  if (!lib.composition.soundtrackPath.empty()) {
    const fs::path origName = fs::path(lib.composition.soundtrackPath).filename();
    const fs::path bundled = latestDir / origName;
    if (fs::is_regular_file(bundled, ec)) {
      const fs::path restoredAsset = targetRoot / origName;
      fs::copy_file(bundled, restoredAsset, fs::copy_options::overwrite_existing, ec);
      if (ec) return RestoreOutcome::Failed;
      lib.composition.soundtrackPath = restoredAsset.string();  // path rewrite
    }
    // (bundled missing = an old/lossy backup; leave the stored path as-is, still restore the project)
  }

  // Write the (path-rewritten) project to the working location. This is the restored .swproj.
  if (!saveLibToFile((targetRoot / swprojName).string(), lib)) return RestoreOutcome::Failed;
  return RestoreOutcome::Restored;
}

}  // namespace sw::backup
