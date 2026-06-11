// ui/toolbar — file ops / Add Node / audio device pick / composition breadcrumbs, split
// from editor_ui (one file one duty: editor_ui = canvas, this = the floating toolbar).
// Zone: ui. Depends on app(document/command/audio) + runtime + verify(thin hook).
#include "ui/editor_ui.h"

#include <memory>
#include <string>

#include "imgui.h"

#include "app/audio_settings.h"
#include "app/audio_monitor.h"
#include "app/command.h"
#include "app/document.h"
#include "app/graph_commands.h"
#include "runtime/compound_graph.h"
#include "runtime/graph.h"  // specTypes / findSpec
#include "verify/eye/eye.h"

namespace sw::ui {
namespace {

void addNode(const std::string& type) {
  sw::Symbol* cur = sw::doc::currentSymbol();
  if (!cur) return;
  sw::SymbolChild c;
  c.id = sw::nextFreeChildId(*cur);
  c.symbolId = type;
  c.x = 120.0f;
  c.y = 120.0f;
  // overrides stay EMPTY — the instance reads the definition's defaults until edited
  // (TiXL Symbol.Child semantics; the flat editor's params-prefill died with it).
  sw::g_commands.push(std::make_unique<sw::AddChildCommand>(sw::doc::g_lib, cur->id, c));
  sw::doc::g_relayout = true;
  sw::doc::g_status = "added " + type;
}

}  // namespace

void drawToolbar() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 12.0f, vp->WorkPos.y + 12.0f),
                          ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin("Toolbar");
  if (ImGui::Button("New")) sw::doc::doNew();
  sw::eye::recordItem("New");  // eye③: hand off this widget's screen rect
  ImGui::SameLine();
  if (ImGui::Button("Open")) sw::doc::doOpen();
  sw::eye::recordItem("Open");
  ImGui::SameLine();
  if (ImGui::Button("Save")) sw::doc::doSave();
  sw::eye::recordItem("Save");
  ImGui::SameLine();
  if (ImGui::Button("Save As")) sw::doc::doSaveAs();
  sw::eye::recordItem("Save As");
  ImGui::SameLine();
  if (ImGui::Button("Add Node")) ImGui::OpenPopup("add_node_popup");
  sw::eye::recordItem("Add Node");
  if (ImGui::BeginPopup("add_node_popup")) {
    for (const std::string& t : sw::specTypes()) {
      if (ImGui::MenuItem(t.c_str())) addNode(t);
      sw::eye::recordItem(("menu:" + t).c_str());  // eye: popup item rect (drawn outside canvas)
    }
    ImGui::EndPopup();
  }
  // Audio input device picker (Ableton-style): list the machine's inputs and route
  // capture to the chosen one. ui -> app(audio_settings) -> platform; the pick persists.
  ImGui::SetNextItemWidth(240.0f);
  if (ImGui::BeginCombo("Audio In", sw::audio::selectedName().c_str())) {
    if (ImGui::Selectable("System Default", sw::audio::selectedUid().empty()))
      sw::audio::selectByUid("");
    for (const sw::audio::DeviceInfo& d : sw::audio::inputDevices()) {
      const bool sel = (d.uid == sw::audio::selectedUid());
      std::string label = d.name + (d.isDefault ? "  (default)" : "");
      if (ImGui::Selectable(label.c_str(), sel)) sw::audio::selectByUid(d.uid);
    }
    ImGui::EndCombo();
  }
  sw::eye::recordItem("Audio In");  // eye③: hand off this widget's screen rect

  // Breadcrumbs (= TiXL GraphTitleAndBreadCrumbs): one button per composition level;
  // clicking jumps back to that level. Hidden at root (nothing to climb out of).
  if (!sw::doc::g_compositionPath.empty()) {
    size_t jumpTo = SIZE_MAX;  // apply after the loop — truncating mid-walk skews the walk
    if (ImGui::SmallButton("Root")) jumpTo = 0;
    sw::eye::recordItem("crumb:0");
    std::string symId = sw::doc::g_lib.rootId;
    for (size_t i = 0; i < sw::doc::g_compositionPath.size(); ++i) {
      const sw::Symbol* s = sw::doc::g_lib.find(symId);
      const sw::SymbolChild* c = s ? sw::childById(*s, sw::doc::g_compositionPath[i]) : nullptr;
      if (!c) break;  // dangling tail: frame_cook's validator trims it next frame
      const sw::Symbol* t = sw::doc::g_lib.find(c->symbolId);
      std::string label = t && !t->name.empty() ? t->name : c->symbolId;
      ImGui::SameLine();
      ImGui::TextDisabled(">");
      ImGui::SameLine();
      const bool isLast = (i + 1 == sw::doc::g_compositionPath.size());
      if (isLast) {
        ImGui::TextUnformatted(label.c_str());  // current level: a label, not a jump
      } else if (ImGui::SmallButton((label + "##crumb" + std::to_string(i + 1)).c_str())) {
        jumpTo = i + 1;
      }
      sw::eye::recordItem(("crumb:" + std::to_string(i + 1)).c_str());
      symId = c->symbolId;
    }
    if (jumpTo != SIZE_MAX) {
      sw::doc::truncateComposition(jumpTo);
      g_navPending = true;  // toolbar draws before the canvas: consumed there this frame
      g_pinnedNode = 0;     // bare child ids alias across symbols (refuter N3 B2)
      g_selectedNode = 0;
    }
  }

  // The live input meter now lives inside the AudioReaction node (level/hit on its face) —
  // see drawNodeCanvas; the toolbar just picks the device.
  ImGui::TextDisabled("%s", sw::doc::g_status.c_str());
  ImGui::End();
}

}  // namespace sw::ui
