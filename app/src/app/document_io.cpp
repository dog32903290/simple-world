// app/document_io — new/open/save/saveAs lifecycle + dirty tracking + window title + the
// navigation selftest (split from document.cpp, rule 4). Zone: app (product behaviour). Depends
// on runtime + platform only (never ui). Owns the on-disk document state (path / saved snapshot /
// title cache) and the revision-keyed dirty cache that compares the live lib against that snapshot.
#include "app/document.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <nfd.hpp>

#include "app/command.h"
#include "platform/dialogs.h"
#include "runtime/compound_save.h"  // .swproj v2 (SymbolLibrary) save/load + v1 migration
#include "runtime/graph.h"          // defaultParticleGraph (seed only)
#include "runtime/graph_bridge.h"   // libFromGraph (default-lib seed) + refreshCompoundSpecs
#include "app/variation_live.h"     // P1 crossfader slice (reset on document swap)
#include "app/variation_panel.h"    // P2 Variation panel pool (reset on document swap)
#include "app/midi_bind.h"          // P3 live bindings (reset on document swap)
#include "app/user_settings.h"      // #12: noteRecentFile — push opened/saved project to recent-files MRU
#include "app/output_window_state.h" // out-window-persistence: save/restore Output view state per-project (sidecar)

namespace sw::doc {

namespace {
std::string g_documentPath;    // empty == never saved
std::string g_savedSnapshot;   // libToJsonV2() at last save/open/new
std::string g_lastTitle;       // cache so we only setTitle when it changes
// B4 fix: isDirty() was calling libToJsonV2(g_lib) EVERY FRAME (via updateWindowTitle).
// For a project with animation curves this JSON serialization can consume 5-30ms/frame,
// dropping FPS from 30→3. Fix: cache the dirty result keyed off g_libRevision — recompute
// at most once per revision bump (only when the lib actually changed), O(1) otherwise.
uint64_t g_dirtyCheckedRev = 0;  // libRevision() value when we last ran the full comparison
bool g_dirtyCache = false;        // cached result of libToJsonV2 != g_savedSnapshot
}  // namespace

bool isDirty() {
  // B4 fix: cache the serialisation result keyed off g_libRevision — recompute only when
  // the lib actually changed (O(1) on every frame the lib is idle). The full JSON comparison
  // still happens on each revision bump so mutations that bypass bumpLibRevision (soundtrack
  // path, bpm drag) are still caught — they all ultimately bump at the next command commit.
  if (g_dirtyCheckedRev != libRevision()) {
    g_dirtyCache = (sw::libToJsonV2(g_lib()) != g_savedSnapshot);
    g_dirtyCheckedRev = libRevision();
  }
  return g_dirtyCache;
}

// Mark the dirty cache stale — call after any g_lib write that bypasses bumpLibRevision,
// so the next isDirty() call reruns the JSON comparison even if the revision didn't change.
// (Currently: soundtrack path update + bpm drag both write g_lib directly.)
void invalidateDirtyCache() { g_dirtyCheckedRev = 0; }

// Returns false only when the user explicitly cancels (so callers abort).
bool confirmDiscardIfDirty() {
  if (!isDirty()) return true;
  switch (sw::askUnsaved()) {
    case sw::UnsavedChoice::Save:     return doSave();   // false if Save As canceled
    case sw::UnsavedChoice::DontSave: return true;
    case sw::UnsavedChoice::Cancel:   return false;
  }
  return false;
}

const std::string& documentPath() { return g_documentPath; }  // "" == never saved (anon global)
// Always prompts for a location. Returns true if a file was written.
bool doSaveAs() {
  NFD::Guard nfdGuard;
  NFD::UniquePath outPath;
  nfdfilteritem_t filters[1] = {{"simple_world project", "swproj"}};
  nfdresult_t r = NFD::SaveDialog(outPath, filters, 1, nullptr, "untitled.swproj");
  if (r != NFD_OKAY) return false;  // cancel or error
  std::string path = outPath.get();
  if (path.size() < 7 || path.substr(path.size() - 7) != ".swproj") path += ".swproj";
  std::string json = sw::libToJsonV2(g_lib());
  if (!sw::saveLibToFile(path, g_lib())) {
    sw::showError("無法寫入：" + path);
    return false;
  }
  g_documentPath = path;
  g_savedSnapshot = json;
  invalidateDirtyCache();  // snapshot changed but revision didn't — a stale cached `true` keeps the • lit
  g_status = "saved -> " + path;
  sw::settings::noteRecentFile(path);  // #12: Save As pushes the new path to recent-files MRU
  sw::settings::saveOutputWindowStateFor(path);  // out-window-persistence: write the view-state sidecar
  return true;
}

// Overwrites the current document; falls back to Save As when never saved.
bool doSave() {
  if (g_documentPath.empty()) return doSaveAs();
  std::string json = sw::libToJsonV2(g_lib());
  if (!sw::saveLibToFile(g_documentPath, g_lib())) {
    sw::showError("無法寫入：" + g_documentPath);
    return false;
  }
  g_savedSnapshot = json;
  invalidateDirtyCache();  // same stale-• hazard as doSaveAs
  g_status = "saved -> " + g_documentPath;
  sw::settings::noteRecentFile(g_documentPath);  // #12: Save re-pushes path to front of recent MRU
  sw::settings::saveOutputWindowStateFor(g_documentPath);  // out-window-persistence: write the sidecar
  return true;
}

// Dialog-free load+swap: the shared body of doOpen and the `--open <file>` CLI seam. Tolerant
// two-phase loader (S15): reads v2 AND legacy v1 (auto-migrated); local problems drop to warnings on
// the status line. Files with compound children open directly. Only swaps the live lib in on success.
bool doOpenPath(const std::string& path, bool quiet) {
  sw::SymbolLibrary lib;
  std::vector<std::string> warnings;
  if (!sw::loadLibFromFile(path, lib, &warnings)) {
    if (quiet)  // pre-GUI callers: an NSAlert here would hang before the app runs
      std::fprintf(stderr, "[open] cannot read project file: %s\n", path.c_str());
    else
      sw::showError("無法讀取此專案檔：" + path);
    return false;
  }
  g_lib() = std::move(lib);
  g_compositionPath.clear();
  sw::refreshCompoundSpecs(g_lib());
  bumpLibRevision();
  g_documentPath = path;
  g_savedSnapshot = sw::libToJsonV2(g_lib());
  sw::g_commands.clear();
  sw::varlive::reset();  // a loaded doc has new child ids — the P1 slice target dangles otherwise
  sw::varpanel::reset();  // P2 pool snapshots capture child ids — a loaded doc dangles them too
  sw::midibind::reset();  // P3 bindings route by child id — a loaded doc dangles them too
  g_relayout = true;
  g_status = "loaded <- " + path;
  sw::settings::noteRecentFile(path);  // #12: opening a project pushes it to recent-files MRU
  sw::settings::loadOutputWindowStateFor(path);  // out-window-persistence: load the sidecar + arm restore
  if (!warnings.empty()) {
    for (const std::string& w : warnings) std::fprintf(stderr, "[open] %s\n", w.c_str());
    g_status += " (" + std::to_string(warnings.size()) + " repaired, see console)";
  }
  return true;
}

void doOpen() {
  if (!confirmDiscardIfDirty()) return;
  NFD::Guard nfdGuard;
  NFD::UniquePath outPath;
  nfdfilteritem_t filters[1] = {{"simple_world project", "swproj"}};
  nfdresult_t r = NFD::OpenDialog(outPath, filters, 1, nullptr);
  if (r != NFD_OKAY) return;
  doOpenPath(outPath.get());
}

void doNew() {
  if (!confirmDiscardIfDirty()) return;
  g_lib() = sw::libFromGraph(sw::defaultParticleGraph());
  g_compositionPath.clear();
  sw::refreshCompoundSpecs(g_lib());
  bumpLibRevision();
  g_documentPath.clear();
  g_savedSnapshot = sw::libToJsonV2(g_lib());
  sw::g_commands.clear();
  sw::varlive::reset(); sw::varpanel::reset(); sw::midibind::reset();  // drop prior P1 slice/P2 pool/P3 bindings
  g_relayout = true;
  g_status = "new project";
  sw::settings::resetOutputWindowStateToDefaults();  // out-window-persistence: drop prior project's view state
}

void updateWindowTitle() {
  if (!g_window) return;
  std::string name = g_documentPath.empty()
      ? std::string("Untitled") : g_documentPath.substr(g_documentPath.find_last_of('/') + 1);
  std::string title = (isDirty() ? "• " : "") + name + " — simple_world";
  if (title == g_lastTitle) return;
  g_lastTitle = title;
  g_window->setTitle(NS::String::string(title.c_str(), NS::StringEncoding::UTF8StringEncoding));
}

void initSnapshot() {
  sw::refreshCompoundSpecs(g_lib());  // dynamic spec table live from frame one
  g_savedSnapshot = sw::libToJsonV2(g_lib());
}

// --- navigation selftest (document.h) ---
int runNavigationSelfTest(bool injectBug) {
  // The doc API is global-bound; snapshot + restore so the test leaves no residue.
  SymbolLibrary savedLib = g_lib();
  std::vector<int> savedPath = g_compositionPath;

  // root{1:CompA, 2:RadialPoints} / CompA{1:CompB, 2:RadialPoints} / CompB{1:RadialPoints}
  SymbolLibrary lib;
  lib.rootId = "Root";
  Symbol radial;
  radial.id = "RadialPoints";
  radial.atomic = true;
  Symbol compB;
  compB.id = "CompB";
  compB.children.push_back({1, "RadialPoints"});
  Symbol compA;
  compA.id = "CompA";
  compA.children.push_back({1, "CompB"});
  compA.children.push_back({2, "RadialPoints"});
  Symbol root;
  root.id = "Root";
  root.children.push_back({1, "CompA"});
  root.children.push_back({2, "RadialPoints"});
  lib.symbols = {{"RadialPoints", radial}, {"CompB", compB}, {"CompA", compA}, {"Root", root}};
  g_lib() = lib;
  g_compositionPath.clear();

  bool ok = currentSymbolId() == "Root";
  ok = ok && !pushComposition(2);                       // atomic child refuses (TiXL rule)
  ok = ok && currentSymbolId() == "Root";
  ok = ok && pushComposition(1) && currentSymbolId() == "CompA";
  ok = ok && pushComposition(1) && currentSymbolId() == "CompB";
  ok = ok && g_compositionPath.size() == 2;

  // Delete the CompB instance out from under the path (simulating a mid-frame edit): the
  // PURE getter falls back to the valid prefix WITHOUT mutating the path...
  g_lib().find("CompA")->children.erase(g_lib().find("CompA")->children.begin());
  ok = ok && currentSymbolId() == "CompA" && g_compositionPath.size() == 2;
  // ...and the per-frame validator is the one place that trims (teeth: skipping it leaves
  // the stale tail alive and the size assertion FAILS).
  if (!injectBug) validateCompositionPath();
  ok = ok && g_compositionPath.size() == 1 && currentSymbolId() == "CompA";

  ok = ok && popComposition() && currentSymbolId() == "Root";
  ok = ok && !popComposition();  // already at root

  // breadcrumb jump: rebuild depth 2 (restore the deleted child first), truncate to root.
  g_lib().find("CompA")->children.insert(g_lib().find("CompA")->children.begin(), {1, "CompB"});
  ok = ok && pushComposition(1) && pushComposition(1) && g_compositionPath.size() == 2;
  truncateComposition(0);
  ok = ok && g_compositionPath.empty() && currentSymbolId() == "Root";

  // Self-nesting refusal (mirror of the resident build's S14 guard): give CompB a child
  // referencing CompA — entering it from Root>CompA>CompB would put CompA on the chain
  // twice, a subtree the resident graph deliberately skips (= permanent black inside).
  g_lib().find("CompB")->children.push_back({2, "CompA"});
  ok = ok && pushComposition(1) && pushComposition(1);  // Root > CompA > CompB
  ok = ok && !pushComposition(2);                       // the CompA instance: refused
  ok = ok && g_compositionPath.size() == 2;
  truncateComposition(0);

  // Dirty-cache save leg (refuter-R2 of 批次10-B4): doSave updates g_savedSnapshot but NOT
  // g_libRevision, so the revision-keyed isDirty cache must be explicitly invalidated there —
  // a save that leaves the cached `true` alive keeps the title's • lit until the next command.
  // This bites doSave ITSELF (real /tmp write), not a hand-rolled simulation.
  {
    std::string savedDoc = g_documentPath;
    std::string savedSnap = g_savedSnapshot;
    g_documentPath = "/tmp/sw_dirtycache_selftest.swproj";
    g_savedSnapshot = "<stale>";   // pretend the file differs -> dirty
    invalidateDirtyCache();
    ok = ok && isDirty();          // primed: cache now holds `true` for this revision
    ok = ok && doSave();           // must wash the dirty bit同 frame (no revision bump happens)
    if (injectBug) { g_dirtyCache = true; g_dirtyCheckedRev = libRevision(); }  // the stale-• bug
    ok = ok && !isDirty();
    std::remove(g_documentPath.c_str());
    g_documentPath = std::move(savedDoc);
    g_savedSnapshot = std::move(savedSnap);
    invalidateDirtyCache();
  }

  g_lib() = std::move(savedLib);
  g_compositionPath = std::move(savedPath);
  printf("[selftest-navigation] push/pop/truncate + dangling-trim + dirty-cache-save%s -> %s\n",
         injectBug ? "(bugged)" : "", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw::doc
