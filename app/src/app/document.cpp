// app/document — the open project's state + save/load operations.
// Zone: app (product behaviour). Depends on runtime + platform only (never ui).
#include "app/document.h"

#include <nfd.hpp>

#include "platform/dialogs.h"
#include "runtime/graph.h"

namespace sw::doc {

Graph g_graph = sw::defaultParticleGraph();
NS::Window* g_window = nullptr;
bool g_relayout = true;
std::string g_status = "ready";

namespace {
std::string g_documentPath;    // empty == never saved
std::string g_savedSnapshot;   // toJson() at last save/open/new
std::string g_lastTitle;       // cache so we only setTitle when it changes
}  // namespace

bool isDirty() { return sw::toJson(g_graph) != g_savedSnapshot; }

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
  if (!sw::saveGraphToFile(path, g_graph)) { sw::showError("無法寫入：" + path); return false; }
  g_documentPath = path;
  g_savedSnapshot = json;
  g_status = "saved -> " + path;
  return true;
}

// Overwrites the current document; falls back to Save As when never saved.
bool doSave() {
  if (g_documentPath.empty()) return doSaveAs();
  std::string json = sw::toJson(g_graph);
  if (!sw::saveGraphToFile(g_documentPath, g_graph)) {
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
  sw::Graph loaded;  // load into a temp graph; only swap in on success
  if (!sw::loadGraphFromFile(path, loaded)) {
    sw::showError("無法讀取此專案檔：" + path);
    return;
  }
  g_graph = loaded;
  g_documentPath = path;
  g_savedSnapshot = sw::toJson(g_graph);
  g_relayout = true;
  g_status = "loaded <- " + path;
}

void doNew() {
  if (!confirmDiscardIfDirty()) return;
  g_graph = sw::defaultParticleGraph();
  g_documentPath.clear();
  g_savedSnapshot = sw::toJson(g_graph);
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
