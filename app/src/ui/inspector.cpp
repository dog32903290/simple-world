// ui/inspector — the selected child's parameter panel (imgui draw), split from editor_ui
// (one file one duty: editor_ui = toolbar/canvas, this = the Inspector window).
// Zone: ui. Depends on app(document/command) + runtime. Never the reverse.
//
// Values resolve through the lib (instance override -> definition default) and edits write
// instance overrides — the definition is never polluted. Undo restores "had an override?
// old value : erase" so a never-overridden slot returns to following its definition default.
#include "ui/editor_ui.h"
#include "ui/gradient_widget.h"       // gradient color-band editor widget (port of TiXL GradientEditor.cs)
#include "ui/inspector_param_menu.h"  // ResetSlot + animateContextMenu (split for line-count rule)
#include "ui/slider_ladder.h"         // SliderLadder precision-edit overlay (TiXL SliderLadder.cs)

#include <memory>
#include <string>
#include <vector>

#include "imgui.h"

#include "app/command.h"
#include "app/document.h"
#include "app/graph_commands.h"
#include "app/animation_commands.h"  // Animate gesture: Add/Remove Animation + playhead-write (P1)
#include "app/frame_cook.h"          // transportPosition (the playhead the Animate gesture writes at)
#include "app/midi_bind.h"           // P3 MIDI-learn affordance (beginLearn / isParamBound / unbind)
#include "runtime/sw_gradient.h"     // SwGradient (gradient widget; residentCookedGradient returns this)
#include "runtime/compound_graph.h"
#include "runtime/graph.h"  // findSpec (a compound child resolves like an atomic, N1)
#include "verify/eye/eye.h"  // one-line hooks: param widget rects for the hand

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
// SliderLadder overlay: when a Float/Vec drag begins we record GetTime() so the overlay knows
// its time-since-visible (TiXL SingleValueEdit._timeOpened; the <0.2s initial-delay branch). One
// drag at a time → one shared start time. center is read from MouseClickedPos[0] each frame.
double g_ladderTimeStart = 0.0;
// P1 動已動畫 slider 的 live-write 收尾：拖曳開始快照整條 CurveArray，放手時把 before/after 兩份交給
// SetCurveSnapshotCommand。一次只有一個 slider 在拖。
sw::Animator::CurveArray g_curveSnapBefore;

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
        return sw::effectiveInput(sw::doc::g_lib(), *sel, p.id, p.def);
      };
      auto pushSet = [&](const std::string& slotId, bool had, float oldV, float newV) {
        sw::g_commands.push(std::make_unique<sw::SetOverrideCommand>(
            sw::doc::g_lib(), cur->id, sel->id, slotId, had, oldV, newV));
      };
      bool any = false;
      for (size_t i = 0; i < spec->ports.size(); ++i) {
        const sw::PortSpec& p = spec->ports[i];
        if (!(p.isInput && p.dataType == "Float")) continue;
        // `_`-prefixed Float params are INTERNAL/hidden (no Inspector knob): they carry machine
        // state through the value spine, not a user dial. First user = the particle-force lane's
        // `_ForceKind` discriminator (particle_params.h ForceKind) — exposing it would let a drag
        // switch a force node's kernel out from under its params. pinless already drops the canvas
        // pin (node_draw.cpp); this drops the Inspector row too. (particle-force lane addition.)
        if (!p.id.empty() && p.id[0] == '_') continue;
        any = true;
        if (p.widget == sw::Widget::Vec && p.vecArity >= 2) {
          // Vector param head: draw its `vecArity` components ("<base>.x/.y/.z/.w") as ONE
          // DragScalarN row. Components are pinless (unwired) so no connection branch.
          int N = p.vecArity > 4 ? 4 : p.vecArity;
          // Animated group? Curves live under the HEAD's id (= animGroupForSlot 同源 grouping —
          // the resident projection asks the exact same question on the exact same key).
          const bool vecAnimated = cur->animator.isAnimated(sel->id, p.id);
          if (vecAnimated) {
            // ANIMATED Vec (批次8): each component shows ITS channel curve @ the playhead; dragging
            // a component live-writes a key on ONLY that channel (per-channel curve, 批次3 vec
            // live-write 樣式 + the scalar animated-slider precedent above/below). One undo step
            // per drag: whole-CurveArray snapshot on activation, SetCurveSnapshot on release.
            const double playhead = sw::framecook::transportPosition();
            sw::Animator::CurveArray* arr = cur->animator.curvesFor(sel->id, p.id);
            float vals[4] = {0, 0, 0, 0};
            float pre[4];
            for (int k = 0; k < N; ++k) {
              vals[k] = (arr && k < (int)arr->size() && !(*arr)[k].empty())
                            ? (float)(*arr)[k].sample(playhead)
                            : eff(spec->ports[i + k]);  // short array: constant fallback (projection同)
              pre[k] = vals[k];
            }
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 200, 90, 255));
            ImGui::DragScalarN(p.name.c_str(), ImGuiDataType_Float, vals, N, 0.01f, &p.minV,
                               &p.maxV, "%.2f");
            ImGui::PopStyleColor();
            sw::eye::recordItem(("param:" + p.id).c_str());
            if (ImGui::IsItemActivated() && arr) g_curveSnapBefore = *arr;
            if (ImGui::IsItemActive() && ImGui::IsItemEdited() && arr) {
              for (int k = 0; k < N && k < (int)arr->size(); ++k) {
                if (vals[k] == pre[k]) continue;  // only the channel that moved this frame
                sw::Curve& c = (*arr)[k];
                sw::VDefinition kd;
                if (c.hasKeyAt(sw::Curve::roundTime(playhead))) {
                  kd = c.table().at(sw::Curve::roundTime(playhead));  // keep interp/tangents
                  kd.value = vals[k];
                } else {
                  kd.value = vals[k];
                  kd.u = sw::Curve::roundTime(playhead);
                  kd.inInterpolation = sw::KeyInterpolation::Linear;
                  kd.outInterpolation = sw::KeyInterpolation::Linear;
                  kd.brokenTangents = true;
                }
                c.addOrUpdate(playhead, kd);
              }
              sw::doc::bumpLibRevision();  // projection contract: resident driver follows live
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && arr) {
              auto cmd = std::make_unique<sw::SetCurveSnapshotCommand>(
                  sw::doc::g_lib(), cur->id, sel->id, p.id, g_curveSnapBefore, *arr);
              if (!cmd->refused()) sw::g_commands.push(std::move(cmd));
            }
            animateContextMenu(cur->id, sel->id, p.id, /*animated=*/true);
            i += N - 1;  // skip the consumed component ports
            continue;
          }
          float vals[4] = {0, 0, 0, 0};
          bool had[4] = {false, false, false, false};
          std::vector<ResetSlot> resets;  // captured PRE-drag: committed override per comp
          for (int k = 0; k < N; ++k) {
            vals[k] = eff(spec->ports[i + k]);
            had[k] = sel->overrides.count(spec->ports[i + k].id) > 0;
            resets.push_back({spec->ports[i + k].id, had[k], vals[k]});
          }
          float pre[4];
          for (int k = 0; k < N; ++k) pre[k] = vals[k];
          // default/override text color (= TiXL InputValueUi line 516): any component overridden ->
          // ForegroundFull white; all-default -> TextMuted grey. Colors the label + value text.
          bool vecAnyOverride = false;
          for (int k = 0; k < N; ++k) vecAnyOverride = vecAnyOverride || had[k];
          ImGui::PushStyleColor(ImGuiCol_Text, vecAnyOverride ? IM_COL32(255, 255, 255, 255)
                                                              : IM_COL32(128, 128, 128, 255));
          ImGui::DragScalarN(p.name.c_str(), ImGuiDataType_Float, vals, N, 0.01f, &p.minV,
                             &p.maxV, "%.2f");
          ImGui::PopStyleColor();
          sw::eye::recordItem(("param:" + p.id).c_str());
          if (ImGui::IsItemActivated()) {
            for (int k = 0; k < N; ++k) {
              g_vecEditBefore[k] = pre[k];
              g_vecEditHadOverride[k] = had[k];
            }
            g_ladderTimeStart = ImGui::GetTime();
            sw::ui::resetSliderLadder();
          }
          // SliderLadder overlay for Vec rows (DragScalarN). FORK: Vec never applies the ladder
          // delta — DragScalarN owns all N components (overlay-only, visual + modifier feedback).
          sw::ui::drawLadderIfActive((double)p.minV, (double)p.maxV, g_ladderTimeStart);
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
          animateContextMenu(cur->id, sel->id, p.id, /*animated=*/false, resets);
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
          // Record the combo's rect from PRE-widget geometry: while its popup is open,
          // GetItemRect refers to the popup's last Selectable, and a hand reading the map
          // mid-interaction would click that instead (refuter N4 #2).
          const ImVec2 comboPos = ImGui::GetCursorScreenPos();
          const float comboW = ImGui::CalcItemWidth();
          if (!items.empty()) {
            if (ImGui::Combo(p.name.c_str(), &curIdx, items.data(), (int)items.size())) {
              sel->overrides[p.id] = (float)curIdx;
              pushSet(p.id, had, preV, (float)curIdx);
            }
            sw::eye::recordRect(("param:" + p.id).c_str(), comboPos.x, comboPos.y,
                                comboPos.x + comboW, comboPos.y + ImGui::GetFrameHeight());
          }
        } else if (p.widget == sw::Widget::Bool) {
          const float preV = eff(p);
          const bool had = sel->overrides.count(p.id) > 0;
          bool b = preV > 0.5f;
          if (ImGui::Checkbox(p.name.c_str(), &b)) {
            sel->overrides[p.id] = b ? 1.0f : 0.0f;
            pushSet(p.id, had, preV, b ? 1.0f : 0.0f);
          }
          sw::eye::recordItem(("param:" + p.id).c_str());
        } else if (cur->animator.isAnimated(sel->id, p.id)) {
          // ANIMATED Float (S3): the slider shows the curve's value AT THE PLAYHEAD, and editing it
          // writes/updates a key there (P1 拍板: 動已動畫參數 = 在播放頭寫 key). = TiXL
          // FloatInputUi.DrawAnimatedValue -> ApplyValueToAnimation. Drawn in the StatusAnimated tint.
          //
          // LIVE-WRITE (BUG-A 修, 批次3 vec live-write 同款): the slider seeds v from sample(playhead)
          // EVERY frame. If we only pushed a command on release, mid-drag the slider would bounce back
          // to the (unchanged) sampled value each frame and the edit could never accumulate. So during
          // the drag we write v straight into the definition curve @ the playhead — next frame's
          // sample(playhead) then returns the dragged value and the drag accumulates naturally; bumping
          // libRevision re-projects the resident graph so the preview follows live. One undo step per
          // drag: snapshot the whole CurveArray on activation, push SetCurveSnapshot on release.
          const double playhead = sw::framecook::transportPosition();
          sw::Animator::CurveArray* arr = cur->animator.curvesFor(sel->id, p.id);
          float v = (arr && !arr->empty()) ? (float)(*arr)[0].sample(playhead) : eff(p);
          ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 200, 90, 255));
          ImGui::SliderFloat(p.name.c_str(), &v, p.minV, p.maxV);
          ImGui::PopStyleColor();
          sw::eye::recordItem(("param:" + p.id).c_str());
          if (ImGui::IsItemActivated() && arr) g_curveSnapBefore = *arr;  // whole-curve pre-drag snapshot
          // Mid-drag: write the slider value as a key @ the playhead (addOrUpdate = TiXL
          // ApplyValueToAnimation -> Curve.UpdateCurveValues). bumpLibRevision -> resident re-project.
          if (ImGui::IsItemActive() && ImGui::IsItemEdited() && arr && !arr->empty()) {
            sw::Curve& c = (*arr)[0];
            sw::VDefinition k;
            if (c.hasKeyAt(sw::Curve::roundTime(playhead))) {
              k = c.table().at(sw::Curve::roundTime(playhead));  // keep interpolation/tangents
              k.value = v;
            } else {
              k.value = v;
              k.u = sw::Curve::roundTime(playhead);
              k.inInterpolation = sw::KeyInterpolation::Linear;
              k.outInterpolation = sw::KeyInterpolation::Linear;
              k.brokenTangents = true;
            }
            c.addOrUpdate(playhead, k);
            sw::doc::bumpLibRevision();  // projection contract: resident driver picks up the key live
          }
          // Release: hand the pre-drag + post-drag snapshots to one undoable command (refused if equal).
          if (ImGui::IsItemDeactivatedAfterEdit() && arr) {
            auto cmd = std::make_unique<sw::SetCurveSnapshotCommand>(
                sw::doc::g_lib(), cur->id, sel->id, p.id, g_curveSnapBefore, *arr);
            if (!cmd->refused()) sw::g_commands.push(std::move(cmd));
          }
          animateContextMenu(cur->id, sel->id, p.id, /*animated=*/true);
        } else {
          // Free constant — JOG-DIAL drag editor (= TiXL SingleValueEdit drag-to-scrub; here the
          // imgui DragFloat, matching the Vec row's DragScalarN). Writes LIVE into the override so
          // the runtime sees changes mid-drag (柏為 expects immediate feedback). One undo step per
          // drag: capture the pre-drag value + had-override on activation, record on release.
          const bool had = sel->overrides.count(p.id) > 0;
          float v = eff(p);
          const float preV = v;
          // default/override text color (= TiXL InputValueUi line 516): overridden -> ForegroundFull
          // white; default -> TextMuted grey. Colors the label + value text.
          ImGui::PushStyleColor(ImGuiCol_Text,
                                had ? IM_COL32(255, 255, 255, 255) : IM_COL32(128, 128, 128, 255));
          if (ImGui::DragFloat(p.name.c_str(), &v, 0.01f, p.minV, p.maxV, "%.2f",
                               ImGuiSliderFlags_AlwaysClamp)) {
            sel->overrides[p.id] = v;
            sw::doc::bumpLibRevision();  // projection contract (document.h)
          }
          ImGui::PopStyleColor();
          sw::eye::recordItem(("param:" + p.id).c_str());
          if (ImGui::IsItemActivated()) {
            g_paramEditBefore = preV;
            g_paramEditHadOverride = had;
            g_ladderTimeStart = ImGui::GetTime();  // SliderLadder time-since-visible origin
            sw::ui::resetSliderLadder();           // fresh drag → clear locked-range latch
          }
          // SliderLadder precision overlay (= TiXL SingleValueEdit ValueLadder edit method): while
          // this DragFloat is held, draw the 7-row scale ladder over the mouse-down point. FORK:
          // TiXL replaces DragFloat with the ladder (it owns the delta); simple_world v1 overlays it
          // for visual + modifier feedback and leaves DragFloat owning the delta (overlay-only).
          sw::ui::drawLadderIfActive((double)p.minV, (double)p.maxV, g_ladderTimeStart);
          if (ImGui::IsItemDeactivatedAfterEdit()) {
            if (v != g_paramEditBefore) {
              pushSet(p.id, g_paramEditHadOverride, g_paramEditBefore, v);
            } else if (!g_paramEditHadOverride) {
              // drag returned to the exact start value: erase the mid-drag residue so the
              // slot stays override-free (no command — nothing changed).
              if (sel->overrides.erase(p.id)) sw::doc::bumpLibRevision();
            }
          }
          animateContextMenu(cur->id, sel->id, p.id, /*animated=*/false,
                             {{p.id, had, preV}});  // reset target: this slot, its committed value
          // P3 MIDI-learn affordance (free-constant Float only): click → learn mode for (cur,child,p);
          // the NEXT live MIDI/OSC event binds that CC to this param (TiXL MidiInput TeachTrigger).
          // Bound shows ✓ (click to unbind); mid-learn shows "…". Wired/animated params are already driven.
          const bool learning = sw::midibind::learnActive();
          const sw::midibind::LearnTarget lt = sw::midibind::pendingLearn();
          const bool learningThis =
              learning && lt.childId == sel->id && lt.slotId == p.id;
          const bool boundThis = sw::midibind::isParamBound(sel->id, p.id);
          ImGui::SameLine();
          ImGui::PushID(("midilearn:" + p.id).c_str());
          const char* lbl = learningThis ? "MIDI…" : (boundThis ? "MIDI\xE2\x9C\x93" : "MIDI");
          if (ImGui::SmallButton(lbl)) {
            if (boundThis) {
              sw::midibind::unbindParam(sel->id, p.id);  // ✓ click = unbind
            } else if (learningThis) {
              sw::midibind::cancelLearn();               // re-click while learning = cancel
            } else {
              sw::midibind::beginLearn(cur->id, sel->id, p.id);  // arm learn for this param
            }
          }
          sw::eye::recordItem(("midilearn:" + p.id).c_str());
          ImGui::PopID();
        }
      }
      if (!any) ImGui::TextDisabled("(no editable parameters)");

      // --- Gradient widget (B-track gap: inspector Gradient 色帶編輯 widget) ---
      // Port of TiXL GradientEditor.cs. Two cases:
      //   A. The node has a Gradient OUTPUT port → show a read-only bar preview from the resident cook
      //      result (the cooked SwGradient for this node's resident path). Editing the underlying
      //      Color/Pos/Interpolation Float params (above) updates the bar live each frame.
      //   B. The node has a Gradient INPUT port that is NOT wired → show a placeholder (no stored
      //      SwGradient for unwired inputs; connection required to drive this slot).
      //   C. Wired Gradient INPUT → show source name (identical to wired Float path, line ~193).
      //   (Interactive editing of the bar itself — stop add/drag/remove — is shown for outputs only,
      //    using the live cooked SwGradient; returning true bumps libRevision so the preview follows.)
      {
        bool hasGradientPort = false;
        bool hasGradientOutput = false;
        for (const sw::PortSpec& p : spec->ports) {
          if (p.dataType != "Gradient") continue;
          hasGradientPort = true;
          if (!p.isInput) { hasGradientOutput = true; break; }
        }
        if (hasGradientPort) {
          ImGui::Separator();
          // Case A: show gradient bar from the live cooked result for THIS child's resident path.
          if (hasGradientOutput) {
            const std::string rPath = sw::doc::residentPathFor(sel->id);
            const sw::SwGradient* cooked =
                sw::framecook::residentCookedGradient(rPath.c_str());
            if (cooked) {
              // drawGradientWidget edits in-place; we pass a COPY so the display stays read-only
              // for the "output preview" case (the source of truth is the Float Color/Pos params).
              // FORK vs TiXL: TiXL GradientEditor.Draw() edits the gradient slot directly; sw stores
              // gradient state in Float params (Color1..4 / Pos1..4), not a standalone SwGradient.
              // A future enhancement could wire modifications back to Float param overrides.
              sw::SwGradient displayCopy = *cooked;
              ImGui::TextDisabled("Gradient preview");
              sw::ui::drawGradientWidget(displayCopy, 240.0f);
              sw::eye::recordItem("gradient:preview");
            } else {
              ImGui::TextDisabled("Gradient (not yet cooked)");
            }
          } else {
            // Case B / C: Gradient input ports — show wired-source name or placeholder.
            for (const sw::PortSpec& p : spec->ports) {
              if (p.dataType != "Gradient" || !p.isInput) continue;
              if (const sw::SymbolConnection* w = sw::connectionToInput(*cur, sel->id, p.id)) {
                const sw::SymbolChild* src = sw::childById(*cur, w->srcChild);
                ImGui::TextDisabled("%s <- %s", p.name.c_str(),
                                    src ? src->symbolId.c_str() : "?");
              } else {
                ImGui::TextDisabled("%s (connect a gradient source)", p.name.c_str());
              }
            }
          }
        }
      }

      // S2 (批次7) per-output controls (= TiXL EditNodeOutputDialog, the minimal output-dimension UI).
      // For each output the referenced symbol defines: a "Disabled" checkbox (freeze the value /
      // Command no-op) + a "Trigger" dropdown (None/Always/Animated DirtyFlagTrigger override). Both
      // push undoable, sparse commands (back-to-default erases the key). Output dimension lives here,
      // NOT the node-context menu — bypass is node-dimension (right-click), these are per-output.
      const sw::Symbol* def = sw::doc::g_lib().find(sel->symbolId);
      if (def && !def->outputDefs.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("Outputs");
        static const char* kTrig[] = {"None", "Always", "Animated"};
        for (const sw::SlotDef& od : def->outputDefs) {
          ImGui::PushID(od.id.c_str());
          ImGui::TextUnformatted(od.name.c_str());
          // Disabled checkbox
          auto dit = sel->disabledOutputs.find(od.id);
          bool disabled = dit != sel->disabledOutputs.end() && dit->second;
          if (ImGui::Checkbox("Disabled", &disabled)) {
            auto cmd = std::make_unique<sw::SetOutputDisabledCommand>(sw::doc::g_lib(), cur->id, sel->id,
                                                                      od.id, disabled);
            if (!cmd->refused()) {
              sw::g_commands.push(std::move(cmd));
              sw::doc::bumpLibRevision();
            }
          }
          sw::eye::recordItem(("outdisable:" + od.id).c_str());
          // Trigger dropdown
          auto tit = sel->triggerOverrides.find(od.id);
          sw::TriggerOverride curT =
              tit != sel->triggerOverrides.end() ? tit->second : sw::TriggerOverride::None;
          int ti = (int)curT;
          if (ImGui::Combo("Trigger", &ti, kTrig, 3)) {
            auto cmd = std::make_unique<sw::SetOutputTriggerCommand>(
                sw::doc::g_lib(), cur->id, sel->id, od.id, (sw::TriggerOverride)ti);
            if (!cmd->refused()) {
              sw::g_commands.push(std::move(cmd));
              sw::doc::bumpLibRevision();
            }
          }
          sw::eye::recordItem(("outtrigger:" + od.id).c_str());
          ImGui::PopID();
        }
      }
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
