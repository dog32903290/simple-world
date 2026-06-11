#pragma once
#include <string>

#include <AppKit/AppKit.hpp>

#include "runtime/graph.h"

namespace sw::doc {

// The open document's graph — single source of truth for the canvas.
extern Graph g_graph;
extern NS::Window* g_window;   // set by main at launch; used by updateWindowTitle()
extern bool g_relayout;        // load/new/add asks the editor to re-layout positions
extern std::string g_status;   // status-line text shown by the toolbar

bool isDirty();                 // toJson(g_graph) != saved snapshot

// --- production-swap mirror contract ---
// g_graph stays the EDITING model; production cook walks a SymbolLibrary/resident mirror
// rebuilt (in main's frame loop) whenever this revision changes. ANY g_graph mutation must
// bump it: CommandStack push/undo/redo do (command.cpp), doOpen/doNew do, and the two
// Inspector live-drag writes do (editor_ui). Missing a write site = the picture freezes on
// stale values — if that's ever observed, look for an unbumped mutation first.
uint64_t graphRevision();
void bumpGraphRevision();
bool doSave();                  // overwrite current; falls back to Save As; false if canceled
bool doSaveAs();                // always prompt; true if written
void doOpen();                  // unsaved-guard -> Finder -> temp-load -> swap on success
void doNew();                   // unsaved-guard -> reset to default graph
bool confirmDiscardIfDirty();   // false == user canceled (caller aborts)
void updateWindowTitle();       // filename + dirty • ; no-op when unchanged (uses g_window)
void initSnapshot();            // call at startup: snapshot := toJson(default graph)

}  // namespace sw::doc
