// app/document — the open project's state + save/load operations.
// Zone: app (product behaviour). Depends on runtime + platform only (never ui).
#include "app/document.h"

#include <cstdio>
#include <vector>

#include <nfd.hpp>

#include "app/command.h"
#include "platform/dialogs.h"
#include "runtime/compound_save.h"  // .swproj v2 (SymbolLibrary) save/load + legacy migration
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"   // libFromGraph / graphFromLib (transitional flat editor leg)

namespace sw::doc {

Graph g_graph = sw::defaultParticleGraph();
NS::Window* g_window = nullptr;
bool g_relayout = true;
std::string g_status = "ready";

namespace {
std::string g_documentPath;    // empty == never saved
std::string g_savedSnapshot;   // toJson() at last save/open/new
std::string g_lastTitle;       // cache so we only setTitle when it changes
uint64_t g_graphRevision = 1;  // bumped on every g_graph mutation (mirror contract, document.h)
}  // namespace

bool isDirty() { return sw::toJson(g_graph) != g_savedSnapshot; }

uint64_t graphRevision() { return g_graphRevision; }
void bumpGraphRevision() { ++g_graphRevision; }

// Forward decl: doSave is used by confirmDiscardIfDirty before it is defined.
bool doSave();

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

// Always prompts for a location. Returns true if a file was written.
bool doSaveAs() {
  NFD::Guard nfdGuard;
  NFD::UniquePath outPath;
  nfdfilteritem_t filters[1] = {{"simple_world project", "swproj"}};
  nfdresult_t r = NFD::SaveDialog(outPath, filters, 1, nullptr, "untitled.swproj");
  if (r != NFD_OKAY) return false;  // cancel or error
  std::string path = outPath.get();
  if (path.size() < 7 || path.substr(path.size() - 7) != ".swproj") path += ".swproj";
  std::string json = sw::toJson(g_graph);
  // v2 on disk (契約 3: SymbolLibrary, compounds-only + atomic UUID refs); the dirty
  // snapshot stays the flat json — same identity, cheaper compare, dies with g_graph.
  if (!sw::saveLibToFile(path, sw::libFromGraph(g_graph))) {
    sw::showError("無法寫入：" + path);
    return false;
  }
  g_documentPath = path;
  g_savedSnapshot = json;
  g_status = "saved -> " + path;
  return true;
}

// Overwrites the current document; falls back to Save As when never saved.
bool doSave() {
  if (g_documentPath.empty()) return doSaveAs();
  std::string json = sw::toJson(g_graph);
  if (!sw::saveLibToFile(g_documentPath, sw::libFromGraph(g_graph))) {  // v2 on disk
    sw::showError("無法寫入：" + g_documentPath);
    return false;
  }
  g_savedSnapshot = json;
  g_status = "saved -> " + g_documentPath;
  return true;
}

void doOpen() {
  if (!confirmDiscardIfDirty()) return;
  NFD::Guard nfdGuard;
  NFD::UniquePath outPath;
  nfdfilteritem_t filters[1] = {{"simple_world project", "swproj"}};
  nfdresult_t r = NFD::OpenDialog(outPath, filters, 1, nullptr);
  if (r != NFD_OKAY) return;
  std::string path = outPath.get();
  // Tolerant two-phase loader (S15): reads v2 AND legacy v1 (auto-migrated through the
  // bridge); local problems are dropped with warnings, shown on the status line. Only swap
  // the live graph in on success.
  sw::SymbolLibrary lib;
  std::vector<std::string> warnings;
  if (!sw::loadLibFromFile(path, lib, &warnings)) {
    sw::showError("無法讀取此專案檔：" + path);
    return;
  }
  sw::Graph loaded;
  if (!sw::graphFromLib(lib, loaded, &warnings)) {
    // A root with compound children needs the lib-native editor (批次 3) — refuse honestly
    // rather than silently flattening the file.
    sw::showError("此專案含 compound 節點，目前版本的編輯器還打不開：" + path);
    return;
  }
  g_graph = loaded;
  bumpGraphRevision();
  g_documentPath = path;
  g_savedSnapshot = sw::toJson(g_graph);
  sw::g_commands.clear();
  g_relayout = true;
  g_status = "loaded <- " + path;
  if (!warnings.empty()) {
    for (const std::string& w : warnings) std::fprintf(stderr, "[open] %s\n", w.c_str());
    g_status += " (" + std::to_string(warnings.size()) + " repaired, see console)";
  }
}

void doNew() {
  if (!confirmDiscardIfDirty()) return;
  g_graph = sw::defaultParticleGraph();
  bumpGraphRevision();
  g_documentPath.clear();
  g_savedSnapshot = sw::toJson(g_graph);
  sw::g_commands.clear();
  g_relayout = true;
  g_status = "new project";
}

void updateWindowTitle() {
  if (!g_window) return;
  std::string name = g_documentPath.empty()
      ? std::string("Untitled")
      : g_documentPath.substr(g_documentPath.find_last_of('/') + 1);
  std::string title = (isDirty() ? "• " : "") + name + " — simple_world";
  if (title == g_lastTitle) return;
  g_lastTitle = title;
  g_window->setTitle(NS::String::string(title.c_str(), NS::StringEncoding::UTF8StringEncoding));
}

void initSnapshot() { g_savedSnapshot = sw::toJson(g_graph); }

}  // namespace sw::doc
