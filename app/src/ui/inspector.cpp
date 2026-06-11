// ui/inspector — the selected child's parameter panel (imgui draw), split from editor_ui
// (one file one duty: editor_ui = toolbar/canvas, this = the Inspector window).
// Zone: ui. Depends on app(document/command) + runtime. Never the reverse.
//
// Values resolve through the lib (instance override -> definition default) and edits write
// instance overrides — the definition is never polluted. Undo restores "had an override?
// old value : erase" so a never-overridden slot returns to following its definition default.
#include "ui/editor_ui.h"

#include <memory>
#include <string>
#include <vector>

#include "imgui.h"

#include "app/command.h"
#include "app/document.h"
#include "app/graph_commands.h"
#include "runtime/compound_graph.h"
#include "runtime/graph.h"  // findSpec (a compound child resolves like an atomic, N1)

namespace sw::ui {
namespace {

// Slider 拖動開始時的值 + 「拖之前這個 slot 有沒有 override」（undo 要還原成 erase 還是
// 舊值——定義層 default 永不被 0 殘渣污染）。一次只有一個 slider 在拖。
float g_paramEditBefore = 0.0f;
bool g_paramEditHadOverride = false;
// Same, for a Vec param's components (DragScalarN edits up to 4 at once). One undo step per
// drag: snapshot all components on activation, push a command per changed component on release.
float g_vecEditBefore[4] = {0.0f, 0.0f, 0.0f, 0.0f};
bool g_vecEditHadOverride[4] = {false, false, false, false};

}  // namespace

void drawInspector() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - 320.0f, vp->WorkPos.y + 24.0f),
                          ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(300.0f, 180.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin("Inspector");
  sw::Symbol* cur = sw::doc::currentSymbol();
  sw::SymbolChild* sel = cur ? sw::childById(*cur, g_selectedNode) : nullptr;
  if (sel) {
    const sw::NodeSpec* spec = sw::findSpec(sel->symbolId);
    ImGui::TextUnformatted(spec ? spec->title.c_str() : sel->symbolId.c_str());
    ImGui::Separator();
    if (spec) {
      // Effective value of a Float input slot (instance override else definition default).
      auto eff = [&](const sw::PortSpec& p) {
        return sw::effectiveInput(sw::doc::g_lib, *sel, p.id, p.def);
      };
      auto pushSet = [&](const std::string& slotId, bool had, float oldV, float newV) {
        sw::g_commands.push(std::make_unique<sw::SetOverrideCommand>(
            sw::doc::g_lib, cur->id, sel->id, slotId, had, oldV, newV));
      };
      bool any = false;
      for (size_t i = 0; i < spec->ports.size(); ++i) {
        const sw::PortSpec& p = spec->ports[i];
        if (!(p.isInput && p.dataType == "Float")) continue;
        any = true;
        if (p.widget == sw::Widget::Vec && p.vecArity >= 2) {
          // Vector param head: draw its `vecArity` components ("<base>.x/.y/.z/.w") as ONE
          // DragScalarN row. Components are pinless (unwired) so no connection branch.
          int N = p.vecArity > 4 ? 4 : p.vecArity;
          float vals[4] = {0, 0, 0, 0};
          bool had[4] = {false, false, false, false};
          for (int k = 0; k < N; ++k) {
            vals[k] = eff(spec->ports[i + k]);
            had[k] = sel->overrides.count(spec->ports[i + k].id) > 0;
          }
          float pre[4];
          for (int k = 0; k < N; ++k) pre[k] = vals[k];
          ImGui::DragScalarN(p.name.c_str(), ImGuiDataType_Float, vals, N, 0.01f, &p.minV,
                             &p.maxV, "%.2f");
          if (ImGui::IsItemActivated())
            for (int k = 0; k < N; ++k) {
              g_vecEditBefore[k] = pre[k];
              g_vecEditHadOverride[k] = had[k];
            }
          if (ImGui::IsItemEdited()) {  // live write so the runtime sees changes mid-drag —
            // ONLY components that moved this frame: an unconditional write materializes
            // overrides for untouched components with no undo entry (refuter N2 #4).
            for (int k = 0; k < N; ++k)
              if (vals[k] != pre[k]) sel->overrides[spec->ports[i + k].id] = vals[k];
            sw::doc::bumpLibRevision();  // projection contract (document.h)
          }
          if (ImGui::IsItemDeactivatedAfterEdit())
            for (int k = 0; k < N; ++k) {
              if (vals[k] != g_vecEditBefore[k]) {
                pushSet(spec->ports[i + k].id, g_vecEditHadOverride[k], g_vecEditBefore[k],
                        vals[k]);
              } else if (!g_vecEditHadOverride[k]) {
                // drag wandered then released at the exact start value: erase the mid-drag
                // residue so the slot is override-free again (no command — nothing changed).
                if (sel->overrides.erase(spec->ports[i + k].id)) sw::doc::bumpLibRevision();
              }
            }
          i += N - 1;  // skip the consumed component ports (for-loop ++i moves past the group)
          continue;
        }
        if (const sw::SymbolConnection* w = sw::connectionToInput(*cur, sel->id, p.id)) {
          // Driven by a connection — grey out, show source type.
          const sw::SymbolChild* src = sw::childById(*cur, w->srcChild);
          ImGui::TextDisabled("%s <- %s", p.name.c_str(), src ? src->symbolId.c_str() : "?");
        } else if (p.widget == sw::Widget::Enum) {
          // Enum param (e.g. InputBand / Output): a dropdown; the value stored is the index.
          const float preV = eff(p);
          const bool had = sel->overrides.count(p.id) > 0;
          int curIdx = (int)(preV + 0.5f);
          std::vector<const char*> items;
          for (const std::string& s : p.labels) items.push_back(s.c_str());
          if (!items.empty() &&
              ImGui::Combo(p.name.c_str(), &curIdx, items.data(), (int)items.size())) {
            sel->overrides[p.id] = (float)curIdx;
            pushSet(p.id, had, preV, (float)curIdx);
          }
        } else if (p.widget == sw::Widget::Bool) {
          const float preV = eff(p);
          const bool had = sel->overrides.count(p.id) > 0;
          bool b = preV > 0.5f;
          if (ImGui::Checkbox(p.name.c_str(), &b)) {
            sel->overrides[p.id] = b ? 1.0f : 0.0f;
            pushSet(p.id, had, preV, b ? 1.0f : 0.0f);
          }
        } else {
          // Free constant — slider writes LIVE into the override so the runtime sees
          // changes mid-drag (柏為 expects immediate feedback). One undo step per drag:
          // capture the pre-drag value + had-override on activation, record on release.
          const bool had = sel->overrides.count(p.id) > 0;
          float v = eff(p);
          const float preV = v;
          if (ImGui::SliderFloat(p.name.c_str(), &v, p.minV, p.maxV)) {
            sel->overrides[p.id] = v;
            sw::doc::bumpLibRevision();  // projection contract (document.h)
          }
          if (ImGui::IsItemActivated()) {
            g_paramEditBefore = preV;
            g_paramEditHadOverride = had;
          }
          if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (v != g_paramEditBefore) {
              pushSet(p.id, g_paramEditHadOverride, g_paramEditBefore, v);
            } else if (!g_paramEditHadOverride) {
              // drag returned to the exact start value: erase the mid-drag residue so the
              // slot stays override-free (no command — nothing changed).
              if (sel->overrides.erase(p.id)) sw::doc::bumpLibRevision();
            }
          }
        }
      }
      if (!any) ImGui::TextDisabled("(no editable parameters)");
    } else {
      ImGui::TextDisabled("(no editable parameters)");
    }
  } else {
    ImGui::TextDisabled("No node selected");
    ImGui::TextWrapped("Click a node in the canvas to edit its parameters.");
  }
  ImGui::Spacing();
  ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
  ImGui::End();
}

}  // namespace sw::ui
