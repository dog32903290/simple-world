// ui/timeline_curve_editor — the curve-editor view (S6 second view mode): value axis + curve
// polylines + keys at (time,value) + in/out tangent handles on selected keys.
// = TiXL TimelineCurveEditor (TimelineCurveEditor.cs) + CurvePoint (CurvePoint.cs), trimmed:
// no SampleCache (we resample per frame), no snap handlers, no weighted-tension drag, no
// cross-view hover link (forks 具名 in the report). Tangent math is ported VERBATIM:
//   handle direction = (-cos a, sin a) in canvas units (CurvePoint.ComputeScreenTangent, cs:313-328)
//   in  rawAngle = PI/2  - atan2(-vx, -vy)   (HandleTangentDrag, cs:192)
//   out rawAngle = -PI/2 - atan2( vx,  vy)   (cs:193)
// which matches the runtime sampler's slope = tan(angle) (curve.cpp slopeFromAngle).
// RECORD-ONLY: gestures land in state().pending; timeline_edit::executePending mutates (BUG-B 律).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"

#include "runtime/compound_graph.h"
#include "runtime/curve.h"
#include "ui/timeline_internal.h"
#include "verify/eye/eye.h"  // one-line hooks: key/tangent rects for the hand

namespace sw::ui::tl {
namespace {

// Lane palette = TiXL DopeSheetArea.CurveColors (4-cycle).
const ImU32 kCurveColors[4] = {
    IM_COL32(255, 70, 70, 200), IM_COL32(60, 230, 80, 200),
    IM_COL32(60, 120, 255, 220), IM_COL32(200, 230, 230, 160)};

// Tangent handle screen offset (= CurvePoint.ComputeScreenTangent cs:313-328): canvas direction
// (-cos a, sin a) scaled into screen space, length = segmentPixels/3 * tension, min 5px.
ImVec2 tangentScreenOffset(const Geom& g, double angle, float tension, double segWidthBars) {
  const float dx = (float)(-std::cos(angle) * g.pxPerBar);
  const float dy = (float)(-(std::sin(angle) * g.pxPerUnit));  // screen y down = canvas value up
  const float len = std::sqrt(dx * dx + dy * dy);
  if (len < 0.001f) return ImVec2(0, 0);
  const float segPx = (float)(segWidthBars * g.pxPerBar);
  const float target = std::max(segPx / 3.0f * tension, 5.0f);
  return ImVec2(dx * target / len, dy * target / len);
}

// One draggable tangent handle; returns nothing — records tanBegin/tanUpdate into pending.
void drawTangentHandle(State& s, const Symbol& sym, const Geom& g, ImDrawList* dl,
                       const SelKey& key, ImVec2 keyPos, ImVec2 offset, bool inSide) {
  const ImVec2 hc(keyPos.x + offset.x, keyPos.y + offset.y);
  dl->AddLine(keyPos, hc, IM_COL32(160, 160, 170, 180));
  ImGui::SetCursorScreenPos(ImVec2(hc.x - 5, hc.y - 5));
  ImGui::PushID(inSide ? "tin" : "tout");
  ImGui::InvisibleButton("##tan", ImVec2(10, 10));
  const bool hovered = ImGui::IsItemHovered();
  sw::eye::recordRect(inSide ? "tlc_tan_in" : "tlc_tan_out", hc.x - 5, hc.y - 5, hc.x + 5, hc.y + 5);
  if (ImGui::IsItemActivated() && !s.tan.active && !s.drag.active)
    stageTangentDrag(s, sym, key, inSide);  // liveness driven by the executor (raw mouse-down)
  dl->AddCircleFilled(hc, hovered ? 4.0f : 2.5f, IM_COL32(235, 235, 245, 230));
  ImGui::PopID();
}

}  // namespace

void drawCurveEditor(Symbol& sym, const std::vector<Lane>& lanes, const Geom& g, ImDrawList* dl) {
  State& s = state();
  ImGuiIO& io = ImGui::GetIO();
  const bool modShift = io.KeyShift, modCmd = io.KeyCtrl;  // Mac 雷: io.KeyCtrl = Cmd

  dl->AddRectFilled(ImVec2(g.x0, g.y0), ImVec2(g.x1, g.y1), IM_COL32(18, 20, 26, 255));

  // Value grid: nice-step horizontal lines + labels (= TiXL HorizontalRaster, simplified ladder).
  {
    double step = 0.001;
    while (step * g.pxPerUnit < 30.0 && step < 1e9) step *= 2.0;  // pow2 ladder (ruler 同款 fork)
    const double vLo = g.yToValue(g.y1), vHi = g.yToValue(g.y0);
    for (double v = std::floor(vLo / step) * step; v <= vHi + step * 0.5; v += step) {
      const float y = g.valueToY(v);
      if (y < g.y0 - 1 || y > g.y1 + 1) continue;
      dl->AddLine(ImVec2(g.x0, y), ImVec2(g.x1, y), IM_COL32(46, 50, 60, 255));
      char buf[32];
      snprintf(buf, sizeof(buf), "%g", v);
      dl->AddText(ImVec2(g.x0 + 3, y - 14), IM_COL32(120, 124, 136, 255), buf);
    }
  }

  // Lane legend in the left gutter (replaces the dope view's per-row labels).
  for (size_t li = 0; li < lanes.size(); ++li) {
    dl->AddText(ImVec2(g.x0 - 148.0f, g.y0 + 4 + li * 16.0f),
                kCurveColors[li % 4] | IM_COL32(0, 0, 0, 255), lanes[li].label.c_str());
  }

  // Background fence: drag on empty area = rubber-band over (t,v) points (= TiXL fence in
  // CurveEditor mode, UpdateSelectionForCanvasArea cs:507-529).
  // AllowOverlap is LOAD-BEARING (same press-steal as the dope lane bg): without it this bg
  // claims ActiveId on the press frame and key/tangent buttons never activate.
  ImGui::SetCursorScreenPos(ImVec2(g.x0, g.y0));
  ImGui::SetNextItemAllowOverlap();
  ImGui::InvisibleButton("##curvebg", ImVec2(g.x1 - g.x0, g.y1 - g.y0));
  // Double-click empty curve area = insert a keyframe on EVERY visible lane curve at that time
  // (batch 9; gesture FORK 具名: TiXL uses Alt+click, TimelineCurveEditor.cs:299-313 — ours mirrors
  // the dope view's double-click). Semantics = TiXL InsertNewKeyframe: time snapped via the U snap
  // handler (cs:440-443, all attractors, no exclusions), value = the curve's SAMPLED value at that
  // time — NOT the clicked v (cs:337, AddKeyframeCommand same law). Record-only; executePending
  // pushes one "Insert keyframes" macro (undo = one step).
  if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
    static std::vector<double> anchors;
    collectSnapAnchors(sym, s, g, /*excludeSelected=*/false, anchors);
    double t = std::max(0.0, g.xToTime(ImGui::GetMousePos().x));
    t = std::max(0.0, snapDragTime(t, g.pxPerBar, anchors, /*snappingDisabled=*/false));
    for (const Lane& ln : lanes)
      s.pending.insertKeys.push_back(SelKey{ln.childId, ln.inputId, ln.index, t});
  }
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, kDragLatchPx) &&
      !s.fence.active && !s.drag.active && !s.tan.active) {
    s.fence.active = true;
    s.fence.start = io.MouseClickedPos[ImGuiMouseButton_Left];
    s.fence.mode = modShift ? 1 : (modCmd ? 2 : 0);
  }

  dl->PushClipRect(ImVec2(g.x0, g.y0), ImVec2(g.x1, g.y1), true);

  static std::vector<ImVec2> poly;  // reused polyline buffer (= TiXL _polylineBuffer idea)
  for (size_t li = 0; li < lanes.size(); ++li) {
    const Lane& ln = lanes[li];
    const Animator::CurveArray* arr = sym.animator.curvesFor(ln.childId, ln.inputId);
    if (!arr || ln.index >= (int)arr->size()) continue;
    const Curve& curve = (*arr)[ln.index];
    const ImU32 col = kCurveColors[li % 4];

    // Curve polyline: resample every 3px across the visible range (fork: no SampleCache).
    poly.clear();
    for (float x = g.x0; x <= g.x1; x += 3.0f)
      poly.push_back(ImVec2(x, g.valueToY(curve.sample(g.xToTime(x)))));
    if (poly.size() >= 2)
      dl->AddPolyline(poly.data(), (int)poly.size(), col, ImDrawFlags_None, 1.4f);

    // Keys at (time, value) + selection/drag wiring (same contract as the dope view).
    int kiSeq = 0;
    // Neighbor times for tangent handle segment widths (CurvePoint segment-proportional length).
    double prevU = 0.0;
    bool hasPrev = false;
    for (auto it = curve.table().begin(); it != curve.table().end(); ++it) {
      const double u = it->first;
      const VDefinition& vdef = it->second;
      const ImVec2 kp(g.timeToX(u), g.valueToY(vdef.value));
      if (kp.x < g.x0 - 12 || kp.x > g.x1 + 12) {
        prevU = u; hasPrev = true; ++kiSeq; continue;
      }
      const bool isSel = isSelected(s, ln.childId, ln.inputId, ln.index, u);
      SelKey k{ln.childId, ln.inputId, ln.index, u};

      // Tangent handles BEFORE the key button so the key wins hover on overlap. ID scoped per
      // key — several selected keys each draw a handle pair.
      if (isSel) {
        auto next = std::next(it);
        const double segIn = hasPrev ? (u - prevU) : 1.0;
        const double segOut = next != curve.table().end() ? (next->first - u) : 1.0;
        ImGui::PushID((int)(900000 + li * 4000 + kiSeq));
        drawTangentHandle(s, sym, g, dl, k, kp,
                          tangentScreenOffset(g, vdef.inTangentAngle, vdef.tensionIn, segIn),
                          /*inSide=*/true);
        drawTangentHandle(s, sym, g, dl, k, kp,
                          tangentScreenOffset(g, vdef.outTangentAngle, vdef.tensionOut, segOut),
                          /*inSide=*/false);
        ImGui::PopID();
      }

      ImGui::SetCursorScreenPos(ImVec2(kp.x - 6, kp.y - 6));
      ImGui::PushID((int)(li * 1000 + kiSeq));
      ImGui::InvisibleButton("##ckey", ImVec2(12, 12));
      const bool hovered = ImGui::IsItemHovered();
      sw::eye::recordRect(("tlc_key:" + std::to_string(ln.childId) + ":" + ln.inputId + ":" +
                           std::to_string(kiSeq)).c_str(),
                          kp.x - 6, kp.y - 6, kp.x + 6, kp.y + 6);
      if (ImGui::IsItemActivated())
        s.suppressDragFromClick = selectOnClickOrDrag(s, k, isSel, modShift, modCmd);
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, kDragLatchPx) &&
          !s.drag.active && !s.tan.active && !s.suppressDragFromClick)
        stageDrag(s, sym, io.MouseClickedPos[ImGuiMouseButton_Left], &k);  // k = snap reference
      dl->AddCircleFilled(kp, isSel ? 5.0f : 3.5f,
                          isSel ? IM_COL32(255, 210, 90, 255)
                                : hovered ? IM_COL32(235, 238, 245, 255) : col);
      ImGui::PopID();
      prevU = u;
      hasPrev = true;
      ++kiSeq;
    }
  }
  dl->PopClipRect();

  // Fence liveness (shared semantics with the dope view; here the fence tests (t,v) points).
  if (s.fence.active) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      s.fence.end = ImGui::GetMousePos();
      const float fx0 = std::min(s.fence.start.x, s.fence.end.x);
      const float fx1 = std::max(s.fence.start.x, s.fence.end.x);
      const float fy0 = std::min(s.fence.start.y, s.fence.end.y);
      const float fy1 = std::max(s.fence.start.y, s.fence.end.y);
      if (s.fence.mode == 0) s.selection.clear();
      for (size_t li = 0; li < lanes.size(); ++li) {
        const Lane& ln = lanes[li];
        const Animator::CurveArray* arr = sym.animator.curvesFor(ln.childId, ln.inputId);
        if (!arr || ln.index >= (int)arr->size()) continue;
        for (const auto& entry : (*arr)[ln.index].table()) {
          const double u = entry.first;
          const float kx = g.timeToX(u), ky = g.valueToY(entry.second.value);
          if (kx < fx0 || kx > fx1 || ky < fy0 || ky > fy1) continue;
          const bool already = isSelected(s, ln.childId, ln.inputId, ln.index, u);
          if (s.fence.mode == 2) {
            s.selection.erase(std::remove_if(s.selection.begin(), s.selection.end(),
                                             [&](const SelKey& e) {
                                               return e.childId == ln.childId &&
                                                      e.inputId == ln.inputId && e.index == ln.index &&
                                                      Curve::roundTime(e.time) == Curve::roundTime(u);
                                             }),
                              s.selection.end());
          } else if (!already) {
            s.selection.push_back(SelKey{ln.childId, ln.inputId, ln.index, u});
          }
        }
      }
      dl->AddRectFilled(ImVec2(fx0, fy0), ImVec2(fx1, fy1), IM_COL32(120, 160, 255, 30));
      dl->AddRect(ImVec2(fx0, fy0), ImVec2(fx1, fy1), IM_COL32(120, 160, 255, 160));
    } else {
      s.fence.active = false;
    }
  }
}

}  // namespace sw::ui::tl
