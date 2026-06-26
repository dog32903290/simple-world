#pragma once
#include <string>
#include <vector>

#include <AppKit/AppKit.hpp>

#include "runtime/compound_graph.h"

namespace sw::doc {

// The open document IS a SymbolLibrary (批次 3 N2, lib-native: TiXL SymbolPackage shape).
// The flat Graph died as the editing model — it survives only as the v1 importer
// (graph_bridge::libFromGraph) and inside goldens.
//
// CONSTRUCT-ON-FIRST-USE (init-order discipline): the default library is built by
// libFromGraph(defaultParticleGraph()), which calls findSpec for every seed node. findSpec
// reads the self-registration sinks (math/image-filter/value/field/…) LIVE, and those sinks
// are filled by file-scope registrars during pre-main dynamic init. A pre-main static global
// would race them: any seed op whose spec lives in a sink (e.g. AudioReaction in mathSpecSink)
// gets silently dropped because its registrar hasn't run yet (the static-init-order fiasco —
// it caught a real regression: the default root lost its AudioReaction child, 5→4).
// g_lib() is an accessor over a function-local static — construction is deferred to the first
// call, which is always inside main() (after ALL pre-main registrars have run), so every seed
// op resolves. Call it as g_lib() everywhere; never cache the reference in a pre-main static.
SymbolLibrary& g_lib();

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

// Combine the CURRENT symbol's selected children into a new compound (runtime/combine.h).
// 照 TiXL (Combine.cs:257): NOT undoable — clears the command stack on success (undoing
// the children-delete would orphan the new definition).
bool doCombine(const std::vector<int>& childIds, const std::string& name);

// Resident path of a child in the CURRENT symbol (join of compositionPath + childId) —
// what frame_cook cooks / the per-path state keys off.
std::string residentPathFor(int childId);
// Just the composition prefix: "" at root, else "1/4/" (trailing slash; valid prefix walk).
// Callers composing their own paths (viewProducerPath) take this + a child id.
std::string residentPathPrefix();

extern NS::Window* g_window;   // set by main at launch; used by updateWindowTitle()
extern bool g_relayout;        // load/new/add asks the editor to re-layout positions
extern std::string g_status;   // status-line text shown by the toolbar

bool isDirty();                 // cached: libToJsonV2(g_lib) != saved snapshot (recomputed on libRevision change)
void invalidateDirtyCache();    // call when g_lib is written without bumpLibRevision (soundtrack path, bpm drag)

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
// ADDITIVE folder-package (.swpkg) entry points (compound_folder.h). A SECOND capability beside
// the single-file .swproj path above — NOT a replacement or migration (.swproj stays the default).
// doSaveAsPackage: prompt for a package dir name -> write one .t3 per symbol + metadata.json.
// doOpenPackage: unsaved-guard -> Finder folder picker -> load the package. Both leave the live
// document's saved-snapshot tracking as a .swproj snapshot (the in-memory model is format-agnostic).
bool doSaveAsPackage();         // always prompt; true if the package was written
void doOpenPackage();           // unsaved-guard -> Finder folder picker -> load .swpkg
// Load+swap without a file picker (doOpen body; also the `--open <file>` CLI seam).
// quiet=true reports failure on stderr ONLY — the CLI seam runs before NSApplication
// exists, where showError's NSAlert runModal hangs forever (refuter N3 B1).
bool doOpenPath(const std::string& path, bool quiet = false);
void doNew();                   // unsaved-guard -> reset to default graph
bool confirmDiscardIfDirty();   // false == user canceled (caller aborts)
void updateWindowTitle();       // filename + dirty • ; no-op when unchanged (uses g_window)
void initSnapshot();            // call at startup: snapshot := serialized default lib

// The open document's absolute file path, or "" when never saved. Read-only accessor over
// the file-scope g_documentPath; the snapshot helper derives <project>/Screenshots from it.
const std::string& documentPath();

// Headless RED->GREEN proof of composition-path semantics: push/pop/truncate rules (atomic
// children refuse push), validate-trims-dangling-tail (delete mid-path child -> valid prefix
// kept), pure-getter consistency. injectBug skips the validation so the stale path survives
// and the assertion FAILS (teeth).
int runNavigationSelfTest(bool injectBug);

}  // namespace sw::doc
