// app/document — the open project's state + save/load operations.
// Zone: app (product behaviour). Depends on runtime + platform only (never ui).
#include "app/document.h"

#include <cstdio>
#include <vector>

#include <nfd.hpp>

#include "app/command.h"
#include "platform/dialogs.h"
#include "runtime/compound_save.h"  // .swproj v2 (SymbolLibrary) save/load + v1 migration
#include "runtime/graph.h"          // defaultParticleGraph (seed only)
#include "runtime/graph_bridge.h"   // libFromGraph (default-lib seed) + refreshCompoundSpecs

namespace sw::doc {

// Default document = the default particle graph imported once through the forever-importer.
SymbolLibrary g_lib = sw::libFromGraph(sw::defaultParticleGraph());
std::vector<int> g_compositionPath;
NS::Window* g_window = nullptr;
bool g_relayout = true;
std::string g_status = "ready";

namespace {
std::string g_documentPath;    // empty == never saved
std::string g_savedSnapshot;   // libToJsonV2() at last save/open/new
std::string g_lastTitle;       // cache so we only setTitle when it changes
uint64_t g_libRevision = 1;    // bumped on every g_lib mutation (projection contract, document.h)
}  // namespace

const std::string& currentSymbolId() {
  // Walk the path: each element is a child id; the current symbol = what the last child
  // references. A dangling element (deleted child) truncates honestly to the valid prefix.
  static std::string s_cur;
  s_cur = g_lib.rootId;
  for (size_t i = 0; i < g_compositionPath.size(); ++i) {
    const Symbol* s = g_lib.find(s_cur);
    const SymbolChild* c = s ? childById(*s, g_compositionPath[i]) : nullptr;
    if (!c) {
      g_compositionPath.resize(i);
      break;
    }
    s_cur = c->symbolId;
  }
  return s_cur;
}

Symbol* currentSymbol() { return g_lib.find(currentSymbolId()); }
const Symbol* currentSymbolConst() { return g_lib.find(currentSymbolId()); }

std::string residentPathFor(int childId) {
  std::string p;
  for (int id : g_compositionPath) p += std::to_string(id) + "/";
  return p + std::to_string(childId);
}

bool isDirty() { return sw::libToJsonV2(g_lib) != g_savedSnapshot; }

uint64_t libRevision() { return g_libRevision; }
void bumpLibRevision() { ++g_libRevision; }

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
  std::string json = sw::libToJsonV2(g_lib);
  if (!sw::saveLibToFile(path, g_lib)) {
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
  std::string json = sw::libToJsonV2(g_lib);
  if (!sw::saveLibToFile(g_documentPath, g_lib)) {
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
  // bridge); local problems are dropped with warnings, shown on the status line. The doc IS
  // a lib now — files with compound children open directly (the graphFromLib refusal died
  // with the flat editor). Only swap the live lib in on success.
  sw::SymbolLibrary lib;
  std::vector<std::string> warnings;
  if (!sw::loadLibFromFile(path, lib, &warnings)) {
    sw::showError("無法讀取此專案檔：" + path);
    return;
  }
  g_lib = std::move(lib);
  g_compositionPath.clear();
  sw::refreshCompoundSpecs(g_lib);
  bumpLibRevision();
  g_documentPath = path;
  g_savedSnapshot = sw::libToJsonV2(g_lib);
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
  g_lib = sw::libFromGraph(sw::defaultParticleGraph());
  g_compositionPath.clear();
  sw::refreshCompoundSpecs(g_lib);
  bumpLibRevision();
  g_documentPath.clear();
  g_savedSnapshot = sw::libToJsonV2(g_lib);
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

void initSnapshot() {
  sw::refreshCompoundSpecs(g_lib);  // dynamic spec table live from frame one
  g_savedSnapshot = sw::libToJsonV2(g_lib);
}

}  // namespace sw::doc
