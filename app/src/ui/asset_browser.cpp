// ui/asset_browser — the AssetLibrary window (imgui draw). See asset_browser.h for the contract +
// the TiXL behavior it matches. Data + command payload live in app/asset_library + app/asset_index;
// this is the surface + the undoable g_commands push.
#include "ui/asset_browser.h"

#include <memory>
#include <string>
#include <vector>

#include "imgui.h"

#include "app/asset_library.h"    // availableLibraryAssets + projectMissingAssets + makeLoadImageChild
#include "app/command.h"          // g_commands
#include "app/document.h"         // g_lib / currentSymbol / g_relayout / g_status
#include "app/graph_commands.h"   // AddChildCommand
#include "runtime/compound_graph.h"
#include "verify/eye/eye.h"       // one-line hooks: asset:<key> row rects for the hand

namespace sw::ui {
namespace {

// Cached available-asset list (a disk walk is too costly per-frame). Rebuilt lazily: on the first
// draw and whenever the user hits Refresh. The asset library is a shared-install folder that does not
// change under us mid-session in the normal case; Refresh covers the rare case it does.
std::vector<sw::assetlib::AvailableAsset> g_available;
bool g_loaded = false;

void rebuildAvailable() {
  g_available = sw::assetlib::availableLibraryAssets();
  g_loaded = true;
}

// Create a LoadImage op wired to `key`, at a fixed canvas offset, as an UNDOABLE command. Mirrors
// toolbar::spawnNodeAt (cycle gate is irrelevant: LoadImage is atomic, never self-nests) but seeds
// the Path strOverride so the new op decodes THIS asset. Returns false if there is no current symbol.
bool createLoadOpForAsset(const std::string& key) {
  sw::Symbol* cur = sw::doc::currentSymbol();
  if (!cur) return false;
  sw::SymbolChild child = sw::assetlib::makeLoadImageChild(key, 140.0f, 140.0f);
  child.id = sw::nextFreeChildId(*cur);
  sw::g_commands.push(std::make_unique<sw::AddChildCommand>(sw::doc::g_lib(), cur->id, child));
  sw::doc::g_relayout = true;
  sw::doc::g_status = "added LoadImage <- " + key;
  return true;
}

}  // namespace

bool& assetBrowserVisible() {
  static bool g_visible = false;  // default OFF: opened on demand from the toolbar, never over canvas
  return g_visible;
}

void drawAssetBrowser() {
  if (!assetBrowserVisible()) return;  // closed: the canvas underneath keeps every click
  if (!g_loaded) rebuildAvailable();

  const ImGuiViewport* vp = ImGui::GetMainViewport();
  // RIGHT-edge column (below the Inspector), NOT over the canvas. An always-open floating panel
  // spawned over the left of the graph (the old +12,+360 default) sat ON TOP of nodes that load
  // there (compound_smoke's node:1/node:2 at canvas x≈120) and ate every canvas click/right-click
  // aimed at them — coordinate hit-test silently dead. TiXL keeps these tool windows docked at the
  // screen edge, clear of the graph; we mirror that by stacking on the right where the Inspector
  // (WorkSize.x-320, y+24, h≈180) already lives. FirstUseEver: the user can still drag it anywhere.
  ImGui::SetNextWindowPos(
      ImVec2(vp->WorkPos.x + vp->WorkSize.x - 320.0f, vp->WorkPos.y + 216.0f),
      ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(300.0f, 280.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin("Asset Library");

  if (ImGui::Button("Refresh")) rebuildAvailable();
  sw::eye::recordItem("asset_refresh");
  ImGui::SameLine();
  ImGui::TextDisabled("%d assets", (int)g_available.size());

  if (g_available.empty()) {
    ImGui::TextDisabled("(no assets under the Lib: root)");
    ImGui::End();
    return;
  }

  ImGui::Separator();

  // The project-reference missing predicate: an asset the LOADED project references but that no
  // longer resolves on disk is flagged. Built off the live lib each frame (cheap: it walks the in-
  // memory library, not disk, except the resolve check). An ENUMERATED asset (listed here) by
  // definition exists on disk, so it is never "missing" — the flag is meaningful only for keys the
  // project references that are NOT in our available list (those still appear nowhere here; the
  // browser lists what's available). We surface the project's missing set as a banner so the user
  // sees there is a broken reference to relink, even though the missing file isn't browsable.
  const auto missing = sw::assetlib::projectMissingAssets(sw::doc::g_lib());
  if (!missing.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(230, 120, 90, 255));
    ImGui::Text("%d missing reference%s in project", (int)missing.size(),
                missing.size() == 1 ? "" : "s");
    ImGui::PopStyleColor();
    sw::eye::recordItem("asset_missing_banner");
    if (ImGui::IsItemHovered()) {
      ImGui::BeginTooltip();
      for (const auto& m : missing) ImGui::TextUnformatted(m.key.c_str());
      ImGui::EndTooltip();
    }
    ImGui::Separator();
  }

  // Deferred action: never mutate the graph mid-imgui-list (resolve after the list is drawn).
  std::string pendingCreateKey;

  ImGui::BeginChild("##asset_list", ImVec2(-1.0f, 0.0f), ImGuiChildFlags_None);
  for (const auto& a : g_available) {
    // The whole row is a Selectable: clicking it creates a LoadImage op wired to this asset (= TiXL
    // drop-to-create, the image asset-type's primary operator). The label shows the key body (after
    // "Lib:") so the list reads as a folder tree without the prefix noise.
    const std::string label =
        a.key.rfind("Lib:", 0) == 0 ? a.key.substr(4) : a.key;
    if (ImGui::Selectable(label.c_str(), false)) pendingCreateKey = a.key;
    // Per-row eye hook so the scenario can `do click @asset:Lib:...`. Key = the FULL asset key (the
    // unique, stable token), not the trimmed label — so the hand targets exactly the right row.
    sw::eye::recordItem(("asset:" + a.key).c_str());
    if (!a.extension.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("  .%s", a.extension.c_str());
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("click: add LoadImage <- %s", a.key.c_str());
  }
  ImGui::EndChild();

  ImGui::End();

  if (!pendingCreateKey.empty()) createLoadOpForAsset(pendingCreateKey);
}

}  // namespace sw::ui
