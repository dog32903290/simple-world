// app/auto_backup — implementation. See auto_backup.h for the TiXL ground truth, the one named
// container fork (directory instead of .zip), and the named-deferred features.
#include "app/auto_backup.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "runtime/compound_save.h"  // saveLibToFile / libToJsonV2

namespace fs = std::filesystem;

namespace sw::backup {
namespace {

constexpr char kPendingSubName[] = ".pending";  // TiXL PendingZipName (:555), the atomic-finalize
                                                // staging name; never matches the backup regex.

// "YYYY_MM_DD-HH_MM_SS_fff" local time + milliseconds — the timestamp suffix in the backup name
// (TiXL AutoBackup.cs:108, "yyyy_MM_dd-HH_mm_ss_fff").
std::string timestampNow() {
  using namespace std::chrono;
  const auto now = system_clock::now();
  const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
  const std::time_t t = system_clock::to_time_t(now);
  std::tm tmv{};
  localtime_r(&t, &tmv);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y_%m_%d-%H_%M_%S", &tmv);
  char out[40];
  std::snprintf(out, sizeof(out), "%s_%03d", buf, static_cast<int>(ms.count()));
  return out;
}

// 5-digit zero-padded index → "#00007" (TiXL "#{index:D5}").
std::string indexTag(int index) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "#%05d", index);
  return buf;
}

// Parse the leading "#NNNNN" of a backup dir name → its index, or -1 if it isn't a backup name
// (mirror of TiXL's _backupNameRegex index group, :558). We accept any "#NNNNN-..." entry (the
// `.pending` staging dir starts with '.', so it never matches).
int indexOfBackupName(const std::string& name) {
  if (name.size() < 6 || name[0] != '#') return -1;
  int idx = 0;
  for (int i = 1; i <= 5; ++i) {
    if (name[i] < '0' || name[i] > '9') return -1;
    idx = idx * 10 + (name[i] - '0');
  }
  if (name.size() < 7 || name[6] != '-') return -1;  // require the "-timestamp" tail
  return idx;
}

// Byte-compare two files (TiXL AutoBackup.cs:97 dedup). Missing/oversized either side → not equal.
bool filesByteEqual(const fs::path& a, const fs::path& b) {
  std::error_code ec;
  if (!fs::exists(a, ec) || !fs::exists(b, ec)) return false;
  if (fs::file_size(a, ec) != fs::file_size(b, ec)) return false;
  std::ifstream fa(a, std::ios::binary), fb(b, std::ios::binary);
  if (!fa || !fb) return false;
  std::istreambuf_iterator<char> ia(fa), ib(fb), end;
  return std::equal(ia, end, ib);
}

// The .swproj written inside a given backup directory.
fs::path swprojIn(const fs::path& backupSubdir, const std::string& swprojName) {
  return backupSubdir / swprojName;
}

// Copy the project's referenced EXTERNAL asset(s) into the backup dir as siblings, so the backup
// is a complete restorable unit (TiXL whole-tree intent). Today the only external user asset is
// composition.soundtrackPath (an absolute path picked via the native dialog). Returns true unless a
// real copy failed (a missing/"" path is not an error — the project simply references no asset).
// The asset keeps its base filename inside the backup; the restored .swproj's stored path is left
// untouched (a restore tool maps it back — out of this cut, see DEFERRED).
bool bundleAssets(const fs::path& backupSubdir, const SymbolLibrary& lib) {
  const std::string& sp = lib.composition.soundtrackPath;
  if (sp.empty()) return true;
  std::error_code ec;
  const fs::path src(sp);
  if (!fs::exists(src, ec) || !fs::is_regular_file(src, ec))
    return true;  // a stale/unreachable path is not this cut's job to repair — skip, don't fail
  const fs::path dst = backupSubdir / src.filename();
  fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
  return !ec;
}

}  // namespace

std::string AutoBackup::backupDir(const std::string& projectRoot) {
  return (fs::path(projectRoot) / ".temp" / "Backup").string();
}

int AutoBackup::lastBackupIndexOnDisk(const std::string& backupDir) {
  std::error_code ec;
  int highest = -1;
  for (const auto& e : fs::directory_iterator(fs::path(backupDir), ec)) {
    if (!e.is_directory(ec)) continue;
    const int idx = indexOfBackupName(e.path().filename().string());
    if (idx > highest) highest = idx;
  }
  return highest;  // -1 when the dir is absent/empty (iterator over a missing path yields nothing)
}

std::string AutoBackup::latestBackupDirOnDisk(const std::string& backupDir) {
  std::error_code ec;
  int highest = -1;
  fs::path latest;
  for (const auto& e : fs::directory_iterator(fs::path(backupDir), ec)) {
    if (!e.is_directory(ec)) continue;
    const int idx = indexOfBackupName(e.path().filename().string());
    if (idx > highest) {
      highest = idx;
      latest = e.path();
    }
  }
  return highest >= 0 ? latest.string() : std::string();
}

BackupOutcome AutoBackup::checkForSave(const std::string& projectRoot,
                                       const std::string& swprojName, const SymbolLibrary& lib,
                                       bool dirty, double nowSeconds) {
  // TiXL CheckForSave gate: enabled, dirty, interval elapsed, not already saving.
  if (!_cfg.enabled || !dirty || _inProgress) return BackupOutcome::Skipped;
  if (nowSeconds - _lastSaveSeconds < _cfg.secondsBetweenSaves) return BackupOutcome::Skipped;

  _inProgress = true;
  // RAII-style guard so every return path clears the re-entrancy flag.
  struct Guard {
    bool& flag;
    ~Guard() { flag = false; }
  } guard{_inProgress};

  std::error_code ec;
  const fs::path dir = backupDir(projectRoot);
  fs::create_directories(dir, ec);

  // DISK is the source of truth (TiXL BackupProject): the next index and the dedup baseline are
  // derived from the on-disk backups every tick → restart-safe, no in-memory counter to reset.
  const std::string latestOnDisk = latestBackupDirOnDisk(dir.string());
  const int nextIndex = lastBackupIndexOnDisk(dir.string()) + 1;  // -1 + 1 = 0 for the first ever

  // Atomic finalize (TiXL :74,:104-118): write the candidate into a `.pending` staging dir, then
  // rename it to its final "#NNNNN-..." name only after it's fully written. A crashed half-write
  // leaves only `.pending` (cleaned below), never a real-indexed half backup.
  const fs::path pending = dir / kPendingSubName;
  fs::remove_all(pending, ec);  // clean any leftover from a crashed previous run (TiXL :74-76)
  fs::create_directories(pending, ec);
  const fs::path pendingSwproj = swprojIn(pending, swprojName);
  if (!saveLibToFile(pendingSwproj.string(), lib) || !bundleAssets(pending, lib)) {
    fs::remove_all(pending, ec);
    return BackupOutcome::Skipped;  // write failed → no backup, no index advance
  }

  // Dedup: byte-compare the candidate .swproj against the LATEST on-disk backup's .swproj (TiXL
  // :97). Identical → drop the candidate, touch the previous mtime; index does NOT advance. This
  // survives restart because the baseline is read from disk, not memory.
  if (!latestOnDisk.empty()) {
    const fs::path prevSwproj = swprojIn(latestOnDisk, swprojName);
    if (filesByteEqual(pendingSwproj, prevSwproj)) {
      fs::remove_all(pending, ec);
      fs::last_write_time(prevSwproj, fs::file_time_type::clock::now(), ec);
      _activeBackupPath = latestOnDisk;
      _lastSaveSeconds = nowSeconds;
      return BackupOutcome::DedupTouched;
    }
  }

  // A real new backup: rename `.pending` → "#NNNNN-timestamp" (atomic finalize), record the active
  // path, reset the interval clock. The index advances purely from disk state.
  const fs::path finalDir = dir / (indexTag(nextIndex) + "-" + timestampNow());
  fs::rename(pending, finalDir, ec);
  if (ec) {
    // Rare: finalize failed (e.g. name race). Drop the candidate; do not advance.
    fs::remove_all(pending, ec);
    return BackupOutcome::Skipped;
  }
  _activeBackupPath = finalDir.string();
  _lastSaveSeconds = nowSeconds;
  return BackupOutcome::Wrote;
}

}  // namespace sw::backup
