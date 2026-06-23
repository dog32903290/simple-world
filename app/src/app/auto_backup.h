// app/auto_backup — periodic project backup with round-trip + complete-unit integrity (Lane L6
// 維運 half, zero-dependency). Faithful to TiXL AutoBackup.cs; ONE named container fork (below).
//
// TiXL ground truth (external/tixl/Editor/Gui/AutoBackup/AutoBackup.cs, 577 lines):
//   • CheckForSave() (:27-66) — called periodically; if dirty AND >= SecondsBetweenSaves since
//     the last backup, kick a background task. A re-entrancy flag (_isSaving) stops overlap.
//   • BackupProject() (:68-121) — derives index/dedup FROM DISK every tick:
//       - GetIndexOfLastBackup(backupDir)+1 (:104,:415) scans existing "#NNNNN-*" files → the
//         next index. This is RESTART-SAFE: an app restart re-reads disk, never resets to #00000.
//       - GetLatestArchiveFilePath(backupDir) (:78,:433) finds the highest-index on-disk backup,
//         and the new candidate is byte-compared against IT (:97) — dedup survives restart too.
//       - Atomic finalize (:74,:104-118): the candidate is written to a `.pending` path first,
//         then File.Move'd to the final "#NNNNN-..." name. A half-written backup never gets a
//         real index; a leftover .pending from a crash is deleted on the next tick (:74-76).
//   • naming (:108) — "#{index:D5}-{yyyy_MM_dd-HH_mm_ss_fff}" (5-digit index, underscores +
//     milliseconds). The on-disk index/dedup scanners only match this exact regex (:558).
//   • WHAT a backup contains (:121-146,:184-200) — TiXL zips EVERY file UNDER projectFolder
//     (Directory.EnumerateFiles(projectFolder,"*",AllDirectories)) minus excluded dirs (:564:
//     bin/obj/.git/.temp/Render/Export/ImageSequence/Screenshots). TiXL projects are MULTI-FILE:
//     .t3/.t3ui/.hlsl/.csproj + a project-local `Assets/` subfolder (FileLocations.cs:109) where
//     user-imported images/audio land. So the whole-tree zip = "every project-scope file a
//     restore needs". It deliberately does NOT chase the shared `Lib:` package (a separate
//     install-restorable folder) and does NOT follow absolute paths outside projectFolder.
//
// NAMED FORK (the one load-bearing divergence) + WHY IT'S FAITHFUL FOR sw:
//   sw's .swproj is single-file (compound_save.h S20 fork: all symbols in one JSON). But it is
//   NOT self-contained — composition.soundtrackPath (compound_graph.h:144) holds an ABSOLUTE path
//   to an external audio file the user picked via the native dialog (soundtrack.cpp:192 stores the
//   raw NFD path, which can be ANYWHERE on disk, outside the project tree). That is sw's analogue
//   of TiXL's project-local `Assets/` audio: a user asset a restore needs but the install can't
//   regenerate. (Image `Lib:` keys → SW_ASSETS_DIR are the shared-install library = TiXL's `Lib:`
//   package = correctly EXCLUDED, restorable from the app install.)
//   So a faithful sw backup = the .swproj + the referenced external soundtrack, captured TOGETHER.
//   Container fork: TiXL zips; sw has no zip dependency, so a backup is a DIRECTORY
//   `#NNNNN-timestamp/` holding `project.swproj` + (when set) the soundtrack file copied in as a
//   sibling. This preserves TiXL's INTENT (one self-contained restorable unit on disk per backup)
//   without the zip dependency. Mechanism stays faithful: disk-derived monotonic index, ms
//   timestamp, byte-compare dedup against the latest on-disk backup, interval gate, .pending
//   atomic finalize, in-progress flag.
//
// DEFERRED (named, NOT pretended-done — see DEBT_LEDGER follow-ups):
//   • ReduceNumberOfBackups binary-thinning retention (TiXL :104,:481-519) — older backups are
//     thinned by the binary representation of their index. NOT implemented: sw keeps every backup.
//   • RestoreLatestBackups / crash-recovery prompt (TiXL :251-389) — on launch, offer to restore
//     the latest backup of a project that crashed. NOT implemented: backups are write-only here.
//   • "-minimal" backup toggle (TiXL :84-90,:108) — extension-filtered reduced backups. N/A while
//     sw is single-file (nothing to filter), noted so a future multi-file sw revisits it.
//
// ZONE: app/ (backup STRATEGY = business logic, same zone as document.h). Pure: depends only on
// runtime/compound_save (round-trip) + std::filesystem. It does NOT touch g_lib / AppKit — the
// caller passes the library + project path, so the whole thing is headless-testable.
#pragma once
#include <cstdint>
#include <string>

#include "runtime/compound_graph.h"  // SymbolLibrary

namespace sw::backup {

// Tunables (TiXL: SecondsBetweenSaves default 180). Kept on the controller so a test can shrink
// the interval to 0 and trigger deterministically.
struct AutoBackupConfig {
  double secondsBetweenSaves = 180.0;  // TiXL AutoBackup default (3 minutes)
  bool enabled = true;
};

// The result of one CheckForSave() decision — what actually happened, so callers (and the
// selftest) can assert on it without reaching into the filesystem.
enum class BackupOutcome {
  Skipped,        // not enabled / not dirty / interval not elapsed
  Wrote,          // a NEW backup directory was created (index advanced)
  DedupTouched,   // content identical to the latest backup → new one dropped, previous touched
};

// Stateless-ish controller (holds only the timing/re-entrancy bookkeeping TiXL keeps as fields).
// The backup INDEX and dedup baseline are NOT held in memory — they are derived from disk every
// tick (TiXL GetIndexOfLastBackup / GetLatestArchiveFilePath), so an app restart never resets the
// index to #00000 and never overwrites a prior backup. One instance per open document; the caller
// owns it.
class AutoBackup {
 public:
  explicit AutoBackup(AutoBackupConfig cfg = {}) : _cfg(cfg) {}

  // The TiXL CheckForSave() decision point. `projectRoot` is the .swproj's directory (backups go
  // to <projectRoot>/.temp/Backup/). `lib` is the live library to snapshot. `swprojName` is the
  // project file name written inside each backup (e.g. "project.swproj"). `dirty` = caller's
  // isDirty(); `nowSeconds` = a monotonic clock reading (injected so tests are deterministic).
  // Returns what happened. Synchronous here (the blueprint's "background Task" is a UI-thread
  // concern; the harness proves the WORK, and the caller may std::async the call site).
  BackupOutcome checkForSave(const std::string& projectRoot, const std::string& swprojName,
                             const SymbolLibrary& lib, bool dirty, double nowSeconds);

  // <projectRoot>/.temp/Backup  — where backups live (TiXL ".temp/Backup").
  static std::string backupDir(const std::string& projectRoot);

  // Scan a backup dir for the highest existing "#NNNNN-*" entry → its index, or -1 if none
  // (TiXL GetIndexOfLastBackup, :415). Disk is the source of truth → restart-safe.
  static int lastBackupIndexOnDisk(const std::string& backupDir);
  // The highest-index existing backup directory, or "" if none (TiXL GetLatestArchiveFilePath,
  // :433). The dedup byte-compare baseline.
  static std::string latestBackupDirOnDisk(const std::string& backupDir);

  // The most recent backup directory written/seen, or "" before the first write. (TiXL exposes
  // the active backup path for the UI status line.)
  const std::string& activeBackupPath() const { return _activeBackupPath; }

 private:
  AutoBackupConfig _cfg;
  std::string _activeBackupPath;
  double _lastSaveSeconds = -1e18;  // "never" — first eligible call always passes the interval
  bool _inProgress = false;         // re-entrancy guard (TiXL _isSaving)
};

// Headless RED->GREEN proof (blueprint §2.1 runAutoBackupSelfTest). Proves the COMPLETE restorable
// unit, not just the .swproj:
//   1. round-trip + asset-sibling: a project that references an external soundtrack → one backup;
//      the backup dir holds BOTH the .swproj (reloads structurally-identical) AND a copy of the
//      soundtrack file (the complete restorable unit, TiXL whole-tree intent).
//   2. index advance: a real content change → a SECOND backup (#00001).
//   3. dedup: an unchanged re-check writes NO new index (byte-equal to the latest on-disk backup).
//   4. interval gate: a dirty call before secondsBetweenSaves elapsed → Skipped.
//   5. restart-safety: a FRESH controller over the same on-disk dir continues the index from disk
//      (does NOT reset to #00000 / overwrite the prior backup).
// injectBug DROPS the bundled soundtrack from the backup before the assertion → the complete-unit
// check FAILS (teeth bite the REAL fidelity property: missing referenced asset = lossy backup).
int runAutoBackupSelfTest(bool injectBug);

}  // namespace sw::backup
