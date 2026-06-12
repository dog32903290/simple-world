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

// Wheel zoom + right-drag pan (= TiXL ScalableCanvas, fork: no scale/scroll damping).
// Zoom factor 1.2^wheel = ComputeZoomDeltaFromMouseWheel (ScalableCanvas.cs:454-470), anchored at
// the cursor (ApplyZoomDelta cs:382-415). Dope view zooms X only (TimeLineCanvas.ApplyZoomDelta
// override cs:335-357); curve view zooms both, Alt = Y-only, Shift = X-only (cs:396-406).
// Pan: right-drag (cs:261-274); horizontal wheel pans X (touchpad path cs:283, speed fork 60px).
void handleZoomPan(tl::ViewState& v, const tl::Geom& g) {
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 m = ImGui::GetMousePos();
  const bool inContent = ImGui::IsWindowHovered() &&
                         m.x >= g.x0 && m.x <= g.x1 && m.y >= g.y0 - kRulerH && m.y <= g.y1;
  if (inContent && io.MouseWheel != 0.0f) {
    const double z = std::pow(1.2, (double)io.MouseWheel);
    const bool zoomX = !(v.curveMode && io.KeyAlt);
    const bool zoomY = v.curveMode && !io.KeyShift;
    if (zoomX) {
      const double focus = g.xToTime(m.x);
      v.pxPerBar = std::clamp(v.pxPerBar * z, 0.05, 5000.0);
      v.scrollBars = focus - (double)(m.x - g.x0) / v.pxPerBar;  // keep the bar under the cursor
    }
    if (zoomY) {
      const double focus = g.yToValue(m.y);
      v.pxPerUnit = std::clamp(v.pxPerUnit * z, 1e-4, 1e6);
      v.valueBottom = focus - (double)(g.y1 - m.y) / v.pxPerUnit;
    }
  }
  if (inContent && io.MouseWheelH != 0.0f)
    v.scrollBars += (double)io.MouseWheelH * 60.0 / v.pxPerBar;
  if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
    v.scrollBars -= (double)io.MouseDelta.x / v.pxPerBar;
    if (v.curveMode) v.valueBottom += (double)io.MouseDelta.y / v.pxPerUnit;
  }
}

// Adaptive bar ruler: pick the smallest step from {…,0.25,0.5,1,2,4,…} that keeps ticks >= 50px
// apart (= TiXL TimeRaster idea, implementation fork 具名: pow2 ladder, no beat subdivision).
void drawRuler(ImDrawList* dl, const tl::Geom& g, float rulerY) {
  dl->AddRectFilled(ImVec2(g.x0, rulerY), ImVec2(g.x1, rulerY + kRulerH), IM_COL32(28, 30, 38, 255));
  double step = 0.25;
  while (step * g.pxPerBar < 50.0 && step < 1e6) step *= 2.0;
  const double tStart = std::floor(g.xToTime(g.x0) / step) * step;
  const double tEnd = g.xToTime(g.x1);
  for (double t = tStart; t <= tEnd + step * 0.5; t += step) {
    const float x = g.timeToX(t);
    if (x < g.x0 - 1 || x > g.x1 + 1) continue;
    dl->AddLine(ImVec2(x, rulerY), ImVec2(x, g.y1), IM_COL32(60, 64, 76, 255));
    char buf[24];
    if (step < 1.0) snprintf(buf, sizeof(buf), "%.2f", t);
    else snprintf(buf, sizeof(buf), "%d", (int)std::llround(t));
    dl->AddText(ImVec2(x + 2, rulerY + 2), IM_COL32(150, 154, 166, 255), buf);
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
        if (curves.size() > 1) ln.label += "[" + std::to_string(idx) + "]";
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
  // Vertical fit on curve-view entry / lane-set change (= TiXL SetVerticalScopeToCanvasArea,
  // paddingFraction 0.15, TimelineCurveEditor.cs:47-56).
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
    s.view.pxPerUnit = kCurveAreaH / (mx - mn);
    s.view.valueBottom = mn;
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

  // Reserve the laid-out height so the window scrolls/sizes correctly.
  ImGui::SetCursorScreenPos(ImVec2(origin.x, g.y1 + 4));
  ImGui::Dummy(ImVec2(1, 1));

  drawContextMenu(s, *sym);
  handleZoomPan(s.view, g);

  // Delete / Backspace (Mac 雷) removes ALL selected keys via one undoable group command.
  if (!s.selection.empty() && ImGui::IsWindowFocused() &&
      (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)))
    s.pending.deleteSelected = true;

  // BUG-B 律: every recorded mutation executes HERE, after all curve.table() iterators closed.
  tl::executePending(symbolId, *sym, g);

  ImGui::End();
}

}  // namespace sw::ui
