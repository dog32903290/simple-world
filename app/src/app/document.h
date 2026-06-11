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
// path). Empty = the root symbol itself. The CURRENT symbol = the one the last child
// references. Session/view state — never serialized. N2 keeps it root-only; N3 (navigation)
// pushes/pops it.
extern std::vector<int> g_compositionPath;
const std::string& currentSymbolId();
Symbol* currentSymbol();              // nullptr only if the lib is broken (missing symbol)
const Symbol* currentSymbolConst();

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
void doOpen();                  // unsaved-guard -> Finder -> temp-load -> swap on success
void doNew();                   // unsaved-guard -> reset to default graph
bool confirmDiscardIfDirty();   // false == user canceled (caller aborts)
void updateWindowTitle();       // filename + dirty • ; no-op when unchanged (uses g_window)
void initSnapshot();            // call at startup: snapshot := serialized default lib

}  // namespace sw::doc
