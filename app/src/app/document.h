#pragma once
#include <string>
#include <vector>

#include <AppKit/AppKit.hpp>

#include "runtime/compound_graph.h"

namespace sw::doc {

// The open document IS a SymbolLibrary (批次 3 N2, lib-native: TiXL SymbolPackage shape).
// The flat Graph died as the editing model — it survives only as the v1 importer
// (graph_bridge::libFromGraph) and inside goldens.
extern SymbolLibrary g_lib;

// Where the canvas is looking: child ids walked from the root symbol (= TiXL composition
// path, ProjectView._compositionPath). Empty = the root symbol itself. The CURRENT symbol =
// the one the last child references. Session/view state — never serialized.
extern std::vector<int> g_compositionPath;

// PURE walk of the path (no mutation): the deepest symbol the path's valid prefix reaches.
// All panels in one frame therefore agree on the current symbol even if a mid-frame edit
// dangled the tail. The actual TRUNCATION happens once per frame in validateCompositionPath
// (called by frame_cook at frame start) — never as a getter side effect (refuter N2 S-6).
const std::string& currentSymbolId();
Symbol* currentSymbol();              // nullptr only if the lib is broken (missing symbol)
const Symbol* currentSymbolConst();
// Trim dangling tail entries (deleted/retyped children). Call once per frame, frame start.
void validateCompositionPath();

// Navigation (= TiXL TrySetCompositionOpToChild / ToParent / breadcrumb jump). push refuses
// non-compound children (TiXL: only items with children open). All three ask the canvas to
// re-seed positions (g_relayout) — the caller clears ed selection + frames content (ui state).
bool pushComposition(int childId);    // enter a compound child of the CURRENT symbol
bool popComposition();                // up one level; false at root
void truncateComposition(size_t depth);  // breadcrumb jump: keep first `depth` entries

// Resident path of a child in the CURRENT symbol (join of compositionPath + childId) —
// what frame_cook cooks / the per-path state keys off. Root-only today: "<childId>".
std::string residentPathFor(int childId);

extern NS::Window* g_window;   // set by main at launch; used by updateWindowTitle()
extern bool g_relayout;        // load/new/add asks the editor to re-layout positions
extern std::string g_status;   // status-line text shown by the toolbar

bool isDirty();                 // libToJsonV2(g_lib) != saved snapshot

// --- resident-projection contract (was the g_graph mirror contract) ---
// g_lib is the EDITING model; production cook walks a RESIDENT eval graph rebuilt (in
// frame_cook) whenever this revision changes. ANY g_lib mutation must bump it: CommandStack
// push/undo/redo do (command.cpp), doOpen/doNew do, and the Inspector live-drag writes do
// (editor_ui). Missing a write site = the picture freezes on stale values — if that's ever
// observed, look for an unbumped mutation first.
uint64_t libRevision();
void bumpLibRevision();
bool doSave();                  // overwrite current; falls back to Save As; false if canceled
bool doSaveAs();                // always prompt; true if written
void doOpen();                  // unsaved-guard -> Finder -> doOpenPath
// Load+swap without a file picker (doOpen body; also the `--open <file>` CLI seam).
// quiet=true reports failure on stderr ONLY — the CLI seam runs before NSApplication
// exists, where showError's NSAlert runModal hangs forever (refuter N3 B1).
bool doOpenPath(const std::string& path, bool quiet = false);
void doNew();                   // unsaved-guard -> reset to default graph
bool confirmDiscardIfDirty();   // false == user canceled (caller aborts)
void updateWindowTitle();       // filename + dirty • ; no-op when unchanged (uses g_window)
void initSnapshot();            // call at startup: snapshot := serialized default lib

// Headless RED->GREEN proof of composition-path semantics: push/pop/truncate rules (atomic
// children refuse push), validate-trims-dangling-tail (delete mid-path child -> valid prefix
// kept), pure-getter consistency. injectBug skips the validation so the stale path survives
// and the assertion FAILS (teeth).
int runNavigationSelfTest(bool injectBug);

}  // namespace sw::doc
