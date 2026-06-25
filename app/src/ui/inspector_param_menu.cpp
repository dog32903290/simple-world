// ui/inspector_param_menu — the per-parameter right-click context menu (split from inspector.cpp,
// ARCHITECTURE rule 4). Reset / Animate / Remove Animation, all undoable. See header for contract.
#include "ui/inspector_param_menu.h"

#include <memory>

#include "imgui.h"

#include "app/animation_commands.h"  // Add/Remove Animation commands
#include "app/command.h"             // MacroCommand / g_commands
#include "app/document.h"            // g_lib / currentSymbol / bumpLibRevision
#include "app/frame_cook.h"          // transportPosition (the playhead Animate writes at)
#include "app/graph_commands.h"      // ResetOverrideCommand
#include "runtime/compound_graph.h"  // effectiveInput
#include "runtime/graph.h"           // findSpec / animGroupForSlot
#include "verify/eye/eye.h"          // one-line hooks

namespace sw::ui {

void animateContextMenu(const std::string& symbolId, int childId, const std::string& slotId,
                        bool animated, const std::vector<ResetSlot>& resets) {
  if (!ImGui::BeginPopupContextItem(("##anim_" + slotId).c_str())) return;
  if (!animated) {
    bool anyOverride = false;
    for (const ResetSlot& r : resets) anyOverride = anyOverride || r.had;
    if (anyOverride && ImGui::MenuItem("Reset to default")) {  // = TiXL ResetInputToDefault
      auto macro = std::make_unique<sw::MacroCommand>("Reset Value");
      for (const ResetSlot& r : resets) {
        auto cmd = std::make_unique<sw::ResetOverrideCommand>(sw::doc::g_lib(), symbolId, childId,
                                                              r.id, r.had, r.old);
        if (!cmd->refused()) macro->add(std::move(cmd));  // refused == slot already default
      }
      if (!macro->empty()) {
        sw::g_commands.push(std::move(macro));  // doIt erases inside push -> default shines through
        sw::doc::bumpLibRevision();             // projection contract: resident re-projects
      }
    }
    if (anyOverride) {
      sw::eye::recordItem("insp:reset");  // space-free marker (scenario @-resolver splits on space)
      ImGui::Separator();
    }
    if (ImGui::MenuItem("Animate")) {
      const double t = sw::framecook::transportPosition();
      sw::SymbolChild* c = sw::childById(*sw::doc::currentSymbol(), childId);
      std::vector<float> curVals;  // one entry per channel, port order from the head
      if (c) {
        if (const sw::NodeSpec* spec = sw::findSpec(c->symbolId)) {
          const sw::AnimGroup ag = sw::animGroupForSlot(*spec, slotId);
          for (size_t i = 0; i < spec->ports.size(); ++i)
            if (spec->ports[i].isInput && spec->ports[i].id == ag.headId) {
              for (int k = 0; k < ag.arity && i + (size_t)k < spec->ports.size(); ++k) {
                const sw::PortSpec& pp = spec->ports[i + (size_t)k];
                curVals.push_back(sw::effectiveInput(sw::doc::g_lib(), *c, pp.id, pp.def));
              }
              break;
            }
        }
      }
      if (curVals.empty()) curVals.push_back(0.0f);  // unknown spec: scalar fallback
      auto cmd = std::make_unique<sw::AddAnimationCommand>(sw::doc::g_lib(), symbolId, childId,
                                                           slotId, t, std::move(curVals));
      if (!cmd->refused()) {  // doIt runs in push; refused (already animated) -> skip stack
        sw::g_commands.push(std::move(cmd));
      }
    }
    sw::eye::recordItem("insp:Animate");
  } else {
    if (ImGui::MenuItem("Remove Animation")) {
      auto cmd = std::make_unique<sw::RemoveAnimationCommand>(sw::doc::g_lib(), symbolId, childId,
                                                              slotId);
      if (!cmd->refused()) sw::g_commands.push(std::move(cmd));
    }
    sw::eye::recordItem("insp:Remove Animation");
  }
  ImGui::EndPopup();
}

}  // namespace sw::ui
