// ui/timeline_dopesheet — the dope-sheet view (S6): lanes + key diamonds + rubber-band fence +
// multi-key drag initiation. = TiXL DopeSheetArea (DopeSheetArea.cs), trimmed to our scope:
// no pin/curve-expand header icons, no per-key value popup, no snapping (forks 具名 in the report).
// RECORD-ONLY: gestures land in state().pending; timeline_edit::executePending mutates (BUG-B 律).
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "imgui.h"

#include "runtime/compound_graph.h"
#include "runtime/curve.h"
#include "ui/timeline_internal.h"
#include "verify/eye/eye.h"  // one-line hooks: lane/key rects for the hand

namespace sw::ui::tl {
namespace {

// Rubber-band selection (= TiXL DopeSheetArea.UpdateSelectionForArea, cs:995-1049): keys whose
// time is inside the fence's time span AND whose lane row intersects the fence's y span.
// Modes: 0=Replace (clear first) / 1=Add / 2=Remove — TiXL SelectionFence.SelectModes.
void applyFence(State& s, const Symbol& sym, const std::vector<Lane>& lanes, const Geom& g) {
  const float fx0 = std::min(s.fence.start.x, s.fence.end.x);
  const float fx1 = std::max(s.fence.start.x, s.fence.end.x);
  const float fy0 = std::min(s.fence.start.y, s.fence.end.y);
  const float fy1 = std::max(s.fence.start.y, s.fence.end.y);
  const double t0 = g.xToTime(fx0), t1 = g.xToTime(fx1);
  if (s.fence.mode == 0) s.selection.clear();
  for (size_t li = 0; li < lanes.size(); ++li) {
    const float laneY = g.y0 + li * kLaneH;
    if (laneY + kLaneH < fy0 || laneY > fy1) continue;
    const Lane& ln = lanes[li];
    const Animator::CurveArray* arr = sym.animator.curvesFor(ln.childId, ln.inputId);
    if (!arr || ln.index >= (int)arr->size()) continue;
    for (const auto& [u, vdef] : (*arr)[ln.index].table()) {
      (void)vdef;
      if (u < t0 || u > t1) continue;
      // 修4: the fence acts on the whole channel group at this time (= TiXL's layered dope lane:
      // its fence catches the .x/.y/.z siblings in the same row; contract in timeline_internal.h).
      static std::vector<SelKey> group;
      paramKeysAtTime(sym, ln.childId, ln.inputId, u, group);
      for (const SelKey& k : group) {
        const bool already = isSelected(s, k.childId, k.inputId, k.index, k.time);
        if (s.fence.mode == 2) {
          if (already)
            s.selection.erase(std::remove_if(s.selection.begin(), s.selection.end(),
                                             [&](const SelKey& e) {
                                               return e.childId == k.childId && e.inputId == k.inputId &&
                                                      e.index == k.index &&
                                                      Curve::roundTime(e.time) == Curve::roundTime(k.time);
                                             }),
                              s.selection.end());
        } else if (!already) {
          s.selection.push_back(k);
        }
      }
    }
  }
}

}  // namespace

void drawDopeSheet(Symbol& sym, const std::vector<Lane>& lanes, const Geom& g, ImDrawList* dl) {
  State& s = state();
  ImGuiIO& io = ImGui::GetIO();
  // Mac 雷: ConfigMacOSXBehaviors swaps Cmd into io.KeyCtrl — "cmd" below reads io.KeyCtrl.
  const bool modShift = io.KeyShift, modCmd = io.KeyCtrl;

  for (size_t li = 0; li < lanes.size(); ++li) {
    const Lane& ln = lanes[li];
    const float laneY = g.y0 + li * kLaneH;
    const float laneMidY = laneY + kLaneH * 0.5f;

    // Label gutter + alternating stripe.
    dl->AddText(ImVec2(g.x0 - 148.0f, laneY + 3), IM_COL32(210, 214, 224, 255), ln.label.c_str());
    dl->AddRectFilled(ImVec2(g.x0, laneY), ImVec2(g.x1, laneY + kLaneH),
                      (li & 1) ? IM_COL32(24, 26, 33, 255) : IM_COL32(20, 22, 28, 255));
    // eye hook: the lane row rect (hand targets the empty lane to double-click-add).
    // Include ln.index so Vec channels (Center.x/Center.y/Center.z) get distinct keys —
    // B3 fix: without the index all three channels share the same eye key and map/state.json
    // cannot tell them apart (鐵律3: one-line hook, impl stays in verify/).
    sw::eye::recordRect(("tl_lane:" + std::to_string(ln.childId) + ":" + ln.inputId + ":" +
                         std::to_string(ln.index)).c_str(),
                        g.x0, laneY, g.x1, laneY + kLaneH);

    const Animator::CurveArray* arr = sym.animator.curvesFor(ln.childId, ln.inputId);
    if (!arr || ln.index >= (int)arr->size()) continue;
    const Curve& curve = (*arr)[ln.index];

    // Lane background FIRST (so key buttons drawn after take hover precedence via z-order):
    // double-click = add key; click-drag = rubber-band fence (= TiXL dope fence).
    // AllowOverlap is LOAD-BEARING: without it the bg (submitted first) claims ActiveId on the
    // press frame and the key buttons never see IsItemActivated — clicking a key selected
    // nothing (批次8 活體驗收 bug). With it the bg yields presses to whoever held HoveredId
    // last frame (= the key under the cursor), keeping fence/double-click on true empty space.
    ImGui::SetCursorScreenPos(ImVec2(g.x0, laneY));
    ImGui::PushID((int)(li + 9000));
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##lanebg", ImVec2(g.x1 - g.x0, kLaneH));
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      double t = std::max(0.0, g.xToTime(ImGui::GetMousePos().x));
      s.pending.addKey = true;
      s.pending.addAt = SelKey{ln.childId, ln.inputId, ln.index, t};
      s.selection.clear();
      s.selection.push_back(SelKey{ln.childId, ln.inputId, ln.index, t});
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, kDragLatchPx) &&
        !s.fence.active && !s.drag.active) {
      s.fence.active = true;
      s.fence.start = io.MouseClickedPos[ImGuiMouseButton_Left];
      s.fence.mode = modShift ? 1 : (modCmd ? 2 : 0);
    }
    ImGui::PopID();

    // Keys: one clickable diamond per table entry. kiSeq counts the FULL table (offscreen keys
    // skip the button but still advance the sequence -> eye labels stay stable under scroll).
    int kiSeq = 0;
    for (const auto& [u, vdef] : curve.table()) {
      const float kx = g.timeToX(u);
      if (kx < g.x0 - 2 * kKeyR || kx > g.x1 + 2 * kKeyR) { ++kiSeq; continue; }
      const bool isSel = isSelected(s, ln.childId, ln.inputId, ln.index, u);

      ImGui::SetCursorScreenPos(ImVec2(kx - kKeyR, laneMidY - kKeyR));
      ImGui::PushID((int)(li * 1000 + kiSeq));
      ImGui::InvisibleButton("##key", ImVec2(kKeyR * 2, kKeyR * 2));
      const bool hovered = ImGui::IsItemHovered();
      sw::eye::recordRect(("tl_key:" + std::to_string(ln.childId) + ":" + ln.inputId + ":" +
                           std::to_string(kiSeq)).c_str(),
                          kx - kKeyR, laneMidY - kKeyR, kx + kKeyR, laneMidY + kKeyR);

      // Click -> TiXL selection semantics (cmd=deselect / shift=add / plain=replace-unless-
      // selected), applied to the WHOLE channel group at this time (修4: = TiXL
      // FindParameterKeysAtPosition — dope view only; the curve editor stays per-key).
      if (ImGui::IsItemActivated()) {
        SelKey k{ln.childId, ln.inputId, ln.index, u};
        s.suppressDragFromClick = selectParamKeysOnClickOrDrag(s, sym, k, isSel, modShift, modCmd);
      }
      // Drag start -> stage a multi-key drag of the WHOLE selection (= TiXL StartDragCommand).
      // Axis latch + dt/dv application happen in executePending; here we only stage. The grabbed
      // key rides along as the snap reference (批次9 snap).
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, kDragLatchPx) &&
          !s.drag.active && !s.suppressDragFromClick) {
        const SelKey grab{ln.childId, ln.inputId, ln.index, u};
        stageDrag(s, sym, g, io.MouseClickedPos[ImGuiMouseButton_Left], &grab);
      }

      // Diamond (square for Constant-out keys = TiXL's distinct dope icon, simplified).
      ImU32 col = isSel ? IM_COL32(255, 210, 90, 255)
                        : hovered ? IM_COL32(220, 224, 235, 255) : IM_COL32(150, 180, 240, 255);
      if (vdef.outInterpolation == KeyInterpolation::Constant) {
        dl->AddRectFilled(ImVec2(kx - kKeyR + 1, laneMidY - kKeyR + 1),
                          ImVec2(kx + kKeyR - 1, laneMidY + kKeyR - 1), col);
      } else {
        dl->AddQuadFilled(ImVec2(kx, laneMidY - kKeyR), ImVec2(kx + kKeyR, laneMidY),
                          ImVec2(kx, laneMidY + kKeyR), ImVec2(kx - kKeyR, laneMidY), col);
      }
      ImGui::PopID();
      ++kiSeq;
    }
  }

  // Fence liveness: while the button is held, live-apply (= TiXL SelectionFence.States.Updated)
  // and draw the band; on release the fence simply ends (selection already applied).
  if (s.fence.active) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      s.fence.end = ImGui::GetMousePos();
      applyFence(s, sym, lanes, g);
      const ImVec2 fmin(std::min(s.fence.start.x, s.fence.end.x),
                        std::min(s.fence.start.y, s.fence.end.y));
      const ImVec2 fmax(std::max(s.fence.start.x, s.fence.end.x),
                        std::max(s.fence.start.y, s.fence.end.y));
      dl->AddRectFilled(fmin, fmax, IM_COL32(120, 160, 255, 30));
      dl->AddRect(fmin, fmax, IM_COL32(120, 160, 255, 160));
    } else {
      s.fence.active = false;
    }
  }
}

}  // namespace sw::ui::tl
