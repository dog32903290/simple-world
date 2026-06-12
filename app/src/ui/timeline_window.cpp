// ui/timeline_window — timeline window SHELL (S6): lane collection, view-mode toggle, zoom/pan,
// adaptive ruler, playhead scrub, context menu, Delete routing. The two views live in
// timeline_dopesheet.cpp / timeline_curve_editor.cpp; ALL curve mutation in timeline_edit.cpp
// (executePending — 批次7 BUG-B 律). See timeline_internal.h for the contract.
#include "ui/timeline_window.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"

#include "app/command.h"     // g_commands: timeline-focused Cmd+Z/Cmd+Shift+Z (undo/redo)
#include "app/document.h"
#include "app/frame_cook.h"  // transportPosition / transportScrub (the shared playhead surface)
#include "runtime/compound_graph.h"
#include "runtime/curve.h"
#include "runtime/graph.h"  // findSpec (port display names)
#include "ui/timeline_internal.h"
#include "verify/eye/eye.h"  // one-line hooks: content/playhead/mode rects for the hand

namespace sw::ui {

namespace {

constexpr float kLaneLabelW = 150.0f;  // left gutter width for "child.input" labels
constexpr float kRulerH = 20.0f;       // time ruler height at the top
constexpr float kCurveAreaH = 180.0f;  // curve-editor content height (fixed strip, fork 具名)

// Resolve a lane's human label "<childTitle>.<inputName>" (English on screen; eye keys ASCII).
std::string laneLabel(const Symbol& sym, int childId, const std::string& inputId) {
  const SymbolChild* c = childById(sym, childId);
  std::string childName = "?";
  std::string inputName = inputId;
  if (c) {
    const Symbol* def = sw::doc::g_lib.find(c->symbolId);
    childName = childReadableName(*c, def ? def->name : c->symbolId);
    if (const sw::NodeSpec* spec = sw::findSpec(c->symbolId)) {
      for (const sw::PortSpec& p : spec->ports)
        if (p.isInput && p.id == inputId) { inputName = p.name; break; }
    }
  }
  return childName + "." + inputName;
}

// Wheel zoom + right-drag pan. ALL input writes the *T targets; tl::dampView eases the drawn
// values toward them each frame (= TiXL ScalableCanvas: HandleInteraction mutates ScaleTarget/
// ScrollTarget, DampScaling animates Scale/Scroll — 批次9 closes the "no damping" fork).
// Zoom factor = tl::zoomDeltaFromWheel (integer 1.2 steps, [0.02,100] — cs:453-477), anchored at
// the cursor (ApplyZoomDelta cs:382-415: focus from the DRAWN transform, target-anchor formula
// ScrollTarget += (focus - ScrollTarget) * (z-1)/z). Dope view zooms X only (TimeLineCanvas
// override cs:335-357); curve view zooms both, Alt = Y-only, Shift = X-only (cs:396-406).
// Pan: right-drag -> ScrollTarget -= delta / ScaleTarget (cs:261-274); horizontal wheel pans X
// (touchpad path cs:283, speed fork 60px).
void handleZoomPan(tl::ViewState& v, const tl::Geom& g) {
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 m = ImGui::GetMousePos();
  const bool inContent = ImGui::IsWindowHovered() &&
                         m.x >= g.x0 && m.x <= g.x1 && m.y >= g.y0 - kRulerH && m.y <= g.y1;
  if (inContent && io.MouseWheel != 0.0f) {
    const double z = tl::zoomDeltaFromWheel(io.MouseWheel);
    const bool zoomX = !(v.curveMode && io.KeyAlt);
    const bool zoomY = v.curveMode && !io.KeyShift;
    if (zoomX) {
      // Scale clamp [0.01,5000] = TimeLineCanvas branch of ClampScaleToValidRange (cs:303-311);
      // no-change-after-clamp skips the scroll anchor too (ApplyZoomDelta early-out, cs:388-390).
      const double clamped = std::clamp(v.pxPerBarT * z, 0.01, 5000.0);
      if (clamped != v.pxPerBarT) {
        const double focus = g.xToTime(m.x);  // drawn transform (= InverseTransformPositionFloat)
        v.pxPerBarT = clamped;
        v.scrollBarsT += (focus - v.scrollBarsT) * (z - 1.0) / z;
      }
    }
    if (zoomY) {
      // FORK 具名: TiXL's curve canvas skips the scale clamp (IsCurveCanvas early-out, cs:305-306);
      // ours keeps [1e-4,1e6] as a NaN guard on pxPerUnit.
      const double clamped = std::clamp(v.pxPerUnitT * z, 1e-4, 1e6);
      if (clamped != v.pxPerUnitT) {
        const double focus = g.yToValue(m.y);
        v.pxPerUnitT = clamped;
        v.valueBottomT += (focus - v.valueBottomT) * (z - 1.0) / z;
      }
    }
  }
  if (inContent && io.MouseWheelH != 0.0f)
    v.scrollBarsT += (double)io.MouseWheelH * 60.0 / v.pxPerBarT;
  if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
    v.scrollBarsT -= (double)io.MouseDelta.x / v.pxPerBarT;
    if (v.curveMode) v.valueBottomT += (double)io.MouseDelta.y / v.pxPerUnitT;
  }
}

// Bar/beat/tick ruler (= TiXL BeatTimeRaster via tl::computeRaster; replaces the 批次8 pow2-ladder
// fork). Lines run the full content height (TiXL DrawTimeTicks), labels sit in the ruler strip.
void drawRuler(ImDrawList* dl, const tl::Geom& g, float rulerY) {
  dl->AddRectFilled(ImVec2(g.x0, rulerY), ImVec2(g.x1, rulerY + kRulerH), IM_COL32(28, 30, 38, 255));
  static std::vector<tl::RasterTick> ticks;
  tl::computeRaster(g.pxPerBar, g.scrollBars, (double)(g.x1 - g.x0), ticks);
  for (const tl::RasterTick& t : ticks) {
    const float x = g.timeToX(t.bars);
    if (x < g.x0 - 1 || x > g.x1 + 1) continue;
    const int la = (int)(t.lineAlpha * 255.0f);
    dl->AddLine(ImVec2(x, rulerY), ImVec2(x, g.y1), IM_COL32(60, 64, 76, la));
    if (t.label[0]) {
      const int ta = (int)(t.labelAlpha * 255.0f);
      dl->AddText(ImVec2(x + 2, rulerY + 2), IM_COL32(150, 154, 166, ta), t.label);
    }
  }
}

// Gather the in/out interpolation modes present in the selection (= TiXL
// GetSelectedKeyframeInterpolationTypes, CurveEditing.cs:480-490) for the menu checkmarks.
std::set<int> selectedInterps(const Symbol& sym, const std::vector<tl::SelKey>& sel) {
  std::set<int> out;
  for (const tl::SelKey& k : sel) {
    const Animator::CurveArray* arr = sym.animator.curvesFor(k.childId, k.inputId);
    if (!arr || k.index >= (int)arr->size()) continue;
    auto it = (*arr)[k.index].table().find(Curve::roundTime(k.time));
    if (it == (*arr)[k.index].table().end()) continue;
    out.insert((int)it->second.inInterpolation);
    out.insert((int)it->second.outInterpolation);
  }
  return out;
}

// Context menu (= TiXL CurveEditing.DrawContextMenu, CurveEditing.cs:98-295, trimmed to the S6
// scope: interpolation switch + delete; copy/paste/space-evenly stay locked). Items only RECORD
// into pending. Right-DRAG (pan) must not open it (CustomComponents.Menus.cs:138 gate).
// FORK 具名: Tangent has no menu item, same as TiXL — it's authored by dragging the handles.
void drawContextMenu(tl::State& s, const Symbol& sym) {
  ImGuiIO& io = ImGui::GetIO();
  if (ImGui::IsWindowHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
      io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < 16.0f && !s.selection.empty())
    ImGui::OpenPopup("tl_ctx");
  if (!ImGui::BeginPopup("tl_ctx")) return;
  const std::set<int> modes = selectedInterps(sym, s.selection);
  ImGui::TextDisabled("Interpolation...");
  // (label, KeyInterpolation) rows = TiXL menu order; "Smooth (Clamped)"=Smooth, "Smooth"=Cubic.
  static const struct { const char* label; KeyInterpolation mode; } kRows[] = {
      {"Linear", KeyInterpolation::Linear},
      {"Smooth (Clamped)", KeyInterpolation::Smooth},
      {"Smooth", KeyInterpolation::Cubic},
      {"Horizontal", KeyInterpolation::Horizontal},
      {"Constant", KeyInterpolation::Constant},
  };
  for (const auto& r : kRows) {
    if (ImGui::MenuItem(r.label, nullptr, modes.count((int)r.mode) > 0))
      s.pending.setInterp = (int)r.mode;
    sw::eye::recordItem((std::string("tlctx:") + r.label).c_str());
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Delete Keyframes")) s.pending.deleteSelected = true;
  sw::eye::recordItem("tlctx:Delete Keyframes");
  ImGui::EndPopup();
}

}  // namespace

void drawTimelineWindow() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + 20.0f, vp->WorkPos.y + vp->WorkSize.y - 240.0f),
                          ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(640.0f, 220.0f), ImGuiCond_FirstUseEver);
  ImGui::Begin("Timeline");

  sw::Symbol* sym = sw::doc::currentSymbol();
  if (!sym) { ImGui::TextDisabled("(no composition)"); ImGui::End(); return; }
  const std::string symbolId = sym->id;
  tl::State& s = tl::state();
  // Selection is scoped to ONE composition (修5): switching symbols drops it, otherwise stale
  // (childId,inputId) pairs leak across compositions and silently select foreign keys whose ids
  // happen to coincide (refuter 盲區3).
  if (s.lastSymbolId != symbolId) {
    s.selection.clear();
    s.lastSymbolId = symbolId;
  }
  tl::pruneSelection(s, *sym);

  // Collect the lanes from the current symbol's Animator (one per channel of each animated input).
  std::vector<tl::Lane> lanes;
  for (const auto& [childId, byInput] : sym->animator.all()) {
    for (const auto& [inputId, curves] : byInput) {
      for (int idx = 0; idx < (int)curves.size(); ++idx) {
        tl::Lane ln;
        ln.childId = childId;
        ln.inputId = inputId;
        ln.index = idx;
        ln.label = laneLabel(*sym, childId, inputId);
        // Multi-channel (Vec) group: channel suffix per TiXL convention (.x/.y/.z/.w =
        // TableView.cs:17-20; same X/Y/Z/W order as DopeSheetArea.CurveNames, cs:549-552).
        if (curves.size() > 1) {
          static const char* kChan[4] = {".x", ".y", ".z", ".w"};
          ln.label += idx < 4 ? std::string(kChan[idx]) : "[" + std::to_string(idx) + "]";
        }
        lanes.push_back(std::move(ln));
      }
    }
  }
  if (lanes.empty()) {
    ImGui::TextDisabled("No animated parameters in this composition.");
    ImGui::TextWrapped("Right-click a parameter in the Inspector and choose Animate.");
    ImGui::End();
    return;
  }

  // --- toolbar row: view-mode toggle (= TiXL TimeLineCanvas.Modes switch) ---
  if (ImGui::Button(s.view.curveMode ? "Dope Sheet" : "Curves")) {
    s.view.curveMode = !s.view.curveMode;
    s.view.needFit = true;  // refit V on every entry (TimelineCurveEditor fitVerticalOnly)
  }
  sw::eye::recordItem("tl_mode");
  ImGui::SameLine();
  ImGui::TextDisabled(s.view.curveMode ? "wheel zoom (alt=Y shift=X) / right-drag pan"
                                       : "wheel zoom / right-drag pan");

  // --- geometry ---
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float availW = ImGui::GetContentRegionAvail().x;
  if (s.view.lastLaneCount != (int)lanes.size()) {
    s.view.lastLaneCount = (int)lanes.size();
    s.view.needFit = true;
  }
  // Damp the drawn canvas toward the input targets BEFORE building the frame's geometry
  // (= TiXL UpdateCanvas order: DampScaling first, HandleInteraction after — ScalableCanvas.cs:44).
  tl::dampView(s.view, std::max(50.0, (double)availW - kLaneLabelW), (double)kCurveAreaH,
               (double)ImGui::GetIO().DeltaTime);
  // Vertical fit on curve-view entry / lane-set change (= TiXL SetVerticalScopeToCanvasArea,
  // paddingFraction 0.15, TimelineCurveEditor.cs:47-56 — writes the TARGETS, the fit animates in).
  if (s.view.curveMode && s.view.needFit) {
    double mn = 1e300, mx = -1e300;
    for (const tl::Lane& ln : lanes) {
      const Animator::CurveArray* arr = sym->animator.curvesFor(ln.childId, ln.inputId);
      if (!arr || ln.index >= (int)arr->size()) continue;
      for (const auto& [u, vdef] : (*arr)[ln.index].table()) {
        (void)u;
        mn = std::min(mn, vdef.value);
        mx = std::max(mx, vdef.value);
      }
    }
    if (mn > mx) { mn = 0.0; mx = 1.0; }
    if (mx - mn < 1e-6) { mn -= 0.5; mx += 0.5; }
    const double pad = (mx - mn) * 0.15;
    mn -= pad; mx += pad;
    s.view.pxPerUnitT = kCurveAreaH / (mx - mn);
    s.view.valueBottomT = mn;
    s.view.needFit = false;
  }
  tl::Geom g;
  g.x0 = origin.x + kLaneLabelW;
  g.x1 = origin.x + std::max(50.0f + kLaneLabelW, availW);
  g.y0 = origin.y + kRulerH;
  g.y1 = g.y0 + (s.view.curveMode ? kCurveAreaH : lanes.size() * tl::kLaneH);
  g.pxPerBar = s.view.pxPerBar;
  g.scrollBars = s.view.scrollBars;
  g.pxPerUnit = s.view.pxPerUnit;
  g.valueBottom = s.view.valueBottom;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  // eye hook: the content rect (hand aims wheel-zoom / pan / fence gestures here).
  sw::eye::recordRect("tl_content", g.x0, g.y0, g.x1, g.y1);

  drawRuler(dl, g, origin.y);

  // --- the active view (record-only; mutations land in executePending below) ---
  if (s.view.curveMode) tl::drawCurveEditor(*sym, lanes, g, dl);
  else tl::drawDopeSheet(*sym, lanes, g, dl);

  // --- playhead: vertical line at transport.position, ruler strip drags it (shared surface) ---
  const double playPos = sw::framecook::transportPosition();
  const float playX = g.timeToX(playPos);
  if (playX >= g.x0 - 2 && playX <= g.x1 + 2) {
    dl->AddLine(ImVec2(playX, origin.y), ImVec2(playX, g.y1), IM_COL32(255, 90, 90, 255), 1.5f);
    dl->AddTriangleFilled(ImVec2(playX - 5, origin.y), ImVec2(playX + 5, origin.y),
                          ImVec2(playX, origin.y + 7), IM_COL32(255, 90, 90, 255));
  }
  ImGui::SetCursorScreenPos(ImVec2(g.x0, origin.y));
  ImGui::InvisibleButton("##playhead", ImVec2(g.x1 - g.x0, kRulerH));
  sw::eye::recordRect("tl_playhead", g.x0, origin.y, g.x1, origin.y + kRulerH);
  if (ImGui::IsItemActive()) {
    double t = std::max(0.0, g.xToTime(ImGui::GetMousePos().x));
    sw::framecook::transportScrub(t);  // SAME control surface as the toolbar Pos drag
  }

  // Snap indicator: 1s-fading vertical line at the last snap target (= ValueSnapHandler.
  // DrawSnapIndicator: opacity (1 - age) * 0.4, color StatusAnimated orange 255/117/0).
  if (ImGui::GetTime() - s.snapStamp < 1.0) {
    const float a = (1.0f - (float)(ImGui::GetTime() - s.snapStamp)) * 0.4f;
    const float sx = g.timeToX(s.snapBars);
    if (sx >= g.x0 - 1 && sx <= g.x1 + 1) {
      dl->AddRectFilled(ImVec2(sx, origin.y), ImVec2(sx + 1, g.y1),
                        IM_COL32(255, 117, 0, (int)(a * 255.0f)));
      sw::eye::recordRect("tl_snap", sx, origin.y, sx + 1, g.y1);
    }
  }

  // Reserve the laid-out height so the window scrolls/sizes correctly.
  ImGui::SetCursorScreenPos(ImVec2(origin.x, g.y1 + 4));
  ImGui::Dummy(ImVec2(1, 1));

  drawContextMenu(s, *sym);
  handleZoomPan(s.view, g);

  // Delete / Backspace (Mac 雷) removes ALL selected keys via one undoable group command.
  if (!s.selection.empty() && ImGui::IsWindowFocused() &&
      (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)))
    s.pending.deleteSelected = true;

  // Cmd+Z / Cmd+Shift+Z while the TIMELINE is focused. The canvas handler (editor_ui.cpp) is
  // window-scoped by IsWindowFocused, so after a timeline gesture the undo key was dead until
  // 柏為 clicked back into the canvas (批次8 活體驗收 bug). Same io.KeyCtrl detection as
  // editor_ui (ConfigMacOSXBehaviors swaps Cmd into io.KeyCtrl); the command stack is shared,
  // so an undone command may be a graph command -> g_relayout, same as the canvas handler.
  {
    ImGuiIO& io = ImGui::GetIO();
    // Dead while a key/tangent drag is live (批次9 順手): the live drag restores/re-applies from
    // its own pre-drag snapshot every frame, so a mid-drag undo gets stomped immediately and the
    // release would push a STALE before/after pair on top of the rewound stack.
    if (ImGui::IsWindowFocused() && io.KeyCtrl && !io.WantTextInput &&
        !s.drag.active && !s.tan.active &&
        ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
      if (io.KeyShift) sw::g_commands.redo();
      else             sw::g_commands.undo();
      sw::doc::g_status = io.KeyShift ? "redo" : "undo";
      sw::doc::g_relayout = true;
    }
  }

  // BUG-B 律: every recorded mutation executes HERE, after all curve.table() iterators closed.
  tl::executePending(symbolId, *sym, g);

  ImGui::End();
}

}  // namespace sw::ui
