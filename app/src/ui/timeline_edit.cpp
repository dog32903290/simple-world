// ui/timeline_edit — the timeline's ONLY mutation point (S6). Views record gestures into
// state().pending; executePending runs them here AFTER every curve.table() iterator closed
// (批次7 BUG-B heap-UAF 律). Live drags follow the Inspector live-write precedent: mutate the
// definition curves directly each frame (+ bumpLibRevision so the resident graph follows), and
// push ONE SetCurveGroupSnapshotCommand (before/after) on release.
//
// = TiXL mapping:
//   drag lifecycle         = ITimeObjectManipulation Start/Update/CompleteDragCommand
//                            (DopeSheetArea.cs:1051-1083); restore+reapply replaces TiXL's
//                            "mutate U then RebuildCurveTables" (no incremental drift, fork 具名)
//   drag clamp             = FORK 具名 "rigid clamp at 0": TiXL moves keys unclamped (U += dt,
//                            cs:1056-1066, negative times allowed); our timeline starts at 0, so
//                            the GROUP offset is clamped once (applyDragOffset) — per-key clamping
//                            piled dragged keys onto t=0 and merged them (refuter 修1, 資料毀損鏈)
//   axis latch             = CurveInputEditing.MoveDirections (TimelineCurveEditor.cs:439-449);
//                            cmd(io.KeyCtrl) allows both axes (cs:453-459)
//   dope vertical drag     = FORK 具名 "value-nudge": TiXL keeps the dope drag horizontal-only
//                            (UpdateDragCommand(dt, 0), DopeSheetArea.cs:938); ours latches
//                            vertical to dv = -dyPx * 0.01 * max(1,|v0|) so 柏為 can nudge values
//                            without switching views. Full-value editing = the Curves view.
//   tangent drag           = CurvePoint.HandleTangentDrag (CurvePoint.cs:171-300): side -> Tangent,
//                            mirror when !broken, cmd breaks, broken promotes the other side
//                            Linear -> Tangent; NO UpdateTangents mid-drag (修2/修3);
//                            weighted/tension/snaps stay locked
//   interpolation switch   = CurveEditing.OnLinear/OnSmooth/OnCubic/OnHorizontal/OnConstant
//                            (CurveEditing.cs:397-462) + UpdateAllTangents
//   delete selection       = AnimationOperations.DeleteSelectedKeyframesFromAnimationParameters
//                            (AnimationOperations.cs:57-99): ALL keys of an input selected ->
//                            RemoveAnimation (driver back to Constant), else delete keys
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "app/animation_commands.h"
#include "app/command.h"
#include "app/document.h"
#include "runtime/compound_graph.h"
#include "runtime/curve.h"
#include "ui/timeline_internal.h"

namespace sw::ui::tl {
namespace {

constexpr double kPi = 3.14159265358979323846;

Curve* curveAt(Symbol& sym, const SelKey& k) {
  Animator::CurveArray* arr = sym.animator.curvesFor(k.childId, k.inputId);
  if (!arr || k.index < 0 || k.index >= (int)arr->size()) return nullptr;
  return &(*arr)[k.index];
}

// = TiXL CurveEditing.On* (CurveEditing.cs:397-462), field-for-field.
void applyInterpSemantics(VDefinition& v, KeyInterpolation mode) {
  v.tensionIn = 1.0f;
  v.tensionOut = 1.0f;
  v.weighted = false;
  switch (mode) {
    case KeyInterpolation::Linear:
      v.brokenTangents = true;
      v.inInterpolation = KeyInterpolation::Linear;
      v.outInterpolation = KeyInterpolation::Linear;
      break;
    case KeyInterpolation::Smooth:
    case KeyInterpolation::Cubic:
      v.brokenTangents = false;
      v.inInterpolation = mode;
      v.outInterpolation = mode;
      break;
    case KeyInterpolation::Horizontal:
      v.brokenTangents = false;
      v.inInterpolation = KeyInterpolation::Horizontal;
      v.inTangentAngle = 0.0;
      v.outInterpolation = KeyInterpolation::Horizontal;
      v.outTangentAngle = kPi;
      break;
    case KeyInterpolation::Constant:
      v.brokenTangents = true;
      v.outInterpolation = KeyInterpolation::Constant;  // OUT only (TiXL OnConstant, cs:439-449)
      break;
    case KeyInterpolation::Tangent:
      break;  // authored by handle drag only (= TiXL: no menu item)
  }
}

// --- multi-key drag (live wrapper: mouse -> dt/dv, core in applyDragOffset) ---

void applyDragLive(State& s, Symbol& sym, const Geom& g, ImVec2 mouse) {
  ImGuiIO& io = ImGui::GetIO();
  const float dxPx = mouse.x - s.drag.mouseStart.x;
  const float dyPx = mouse.y - s.drag.mouseStart.y;
  if (s.drag.axis == 0) {
    if (std::fabs(dxPx) < kDragLatchPx && std::fabs(dyPx) < kDragLatchPx) return;
    s.drag.axis = std::fabs(dxPx) >= std::fabs(dyPx) ? 1 : 2;  // TiXL MoveDirections latch
  }
  const bool both = s.view.curveMode && io.KeyCtrl;  // cmd allows both (TimelineCurveEditor.cs:453)
  const bool allowH = both || s.drag.axis == 1;
  const bool allowV = both || s.drag.axis == 2;
  double dt = allowH ? (double)dxPx / g.pxPerBar : 0.0;
  double dv = 0.0;
  if (allowV) {
    if (s.view.curveMode) {
      dv = -(double)dyPx / g.pxPerUnit;
    } else {
      // dope value-nudge fork (header comment): rate scales with the first key's magnitude.
      const double v0 = s.drag.keys.empty() ? 0.0 : s.drag.keys[0].startValue;
      dv = -(double)dyPx * 0.01 * std::max(1.0, std::fabs(v0));
    }
  }
  // Time snap (= DopeSheetArea.HandleCurvePointDragging cs:925-936): the grabbed key's tentative
  // time is pulled to raster ticks / playhead / non-selected keys, Shift disables; the snapped
  // offset moves the whole rigid group (修1 contract intact — one dt for everyone).
  if (allowH && !s.drag.keys.empty()) {
    static std::vector<double> anchors;
    collectSnapAnchors(sym, s, g, /*excludeSelected=*/true, anchors);
    bool did = false;
    const double snapped =
        snapDragTime(s.drag.refStartTime + dt, g.pxPerBar, anchors, io.KeyShift, &did);
    if (did) {
      dt = snapped - s.drag.refStartTime;
      s.snapStamp = ImGui::GetTime();  // window draws the fading indicator (ValueSnapHandler)
      s.snapBars = snapped;
    }
  }
  applyDragOffset(s, sym, dt, dv);
  sw::doc::bumpLibRevision();  // resident projection follows the live drag (Inspector precedent)
}

// --- tangent drag (live wrapper: mouse -> angle, write in applyTangentDrag) ---

void applyTangentLive(State& s, Symbol& sym, const Geom& g, ImVec2 mouse) {
  Curve* c = curveAt(sym, s.tan.key);
  if (!c) return;
  auto it = c->table().find(Curve::roundTime(s.tan.key.time));
  if (it == c->table().end()) return;

  const ImVec2 kp(g.timeToX(it->first), g.valueToY(it->second.value));
  // Canvas-space mouse vector (value axis up). = CurvePoint.HandleTangentDrag (cs:189-193).
  const double vx = (double)(mouse.x - kp.x) / g.pxPerBar;
  const double vy = -(double)(mouse.y - kp.y) / g.pxPerUnit;
  const double angle = s.tan.inSide ? kPi / 2.0 - std::atan2(-vx, -vy)
                                    : -kPi / 2.0 - std::atan2(vx, vy);
  applyTangentDrag(*c, s.tan.key.time, s.tan.inSide, angle,
                   ImGui::GetIO().KeyCtrl);  // cmd breaks tangents (cs:257)
  sw::doc::bumpLibRevision();
}

void finishTangent(State& s, Symbol& sym, const std::string& symbolId) {
  std::vector<CurveGroupEdit> edits;
  for (const GroupSnap& gs : s.tan.before) {
    const Animator::CurveArray* now = sym.animator.curvesFor(gs.childId, gs.inputId);
    if (!now) continue;
    edits.push_back(CurveGroupEdit{gs.childId, gs.inputId, gs.before, *now});
  }
  auto cmd = std::make_unique<SetCurveGroupSnapshotCommand>(sw::doc::g_lib, symbolId,
                                                            std::move(edits), "Edit Tangents");
  if (!cmd->refused()) sw::g_commands.push(std::move(cmd));
  s.tan = TangentDrag{};
}

// --- one-shot ops (pure command construction; after-states computed on COPIES) ---

void runSetInterpolation(State& s, Symbol& sym, const std::string& symbolId, int mode) {
  std::map<std::pair<int, std::string>, std::vector<SelKey>> groups;
  for (const SelKey& k : s.selection) groups[{k.childId, k.inputId}].push_back(k);
  std::vector<CurveGroupEdit> edits;
  for (auto& [gid, keys] : groups) {
    const Animator::CurveArray* arr = sym.animator.curvesFor(gid.first, gid.second);
    if (!arr) continue;
    Animator::CurveArray after = *arr;
    for (const SelKey& k : keys) {
      if (k.index < 0 || k.index >= (int)after.size()) continue;
      auto it = after[k.index].table().find(Curve::roundTime(k.time));
      if (it != after[k.index].table().end())
        applyInterpSemantics(it->second, (KeyInterpolation)mode);
    }
    for (Curve& c : after) c.updateTangents();  // = TiXL UpdateAllTangents after the menu action
    edits.push_back(CurveGroupEdit{gid.first, gid.second, *arr, std::move(after)});
  }
  auto cmd = std::make_unique<SetCurveGroupSnapshotCommand>(sw::doc::g_lib, symbolId,
                                                            std::move(edits), "Set Interpolation");
  if (!cmd->refused()) sw::g_commands.push(std::move(cmd));
}

}  // namespace

// --- gesture core (exported seams, contract in timeline_internal.h) ---

double applyDragOffset(State& s, Symbol& sym, double dt, double dv) {
  // ONE clamp for the whole group (rigid translation, fork 具名 in the header): the most-left
  // selected key stops at 0 and everyone keeps their relative spacing — selected keys can never
  // merge into each other, so the selection stays one-entry-per-key (修1①②).
  if (!s.drag.keys.empty()) {
    double minStart = s.drag.keys[0].startTime;
    for (const DragKey& dk : s.drag.keys) minStart = std::min(minStart, dk.startTime);
    dt = std::max(dt, -minStart);
  }
  // Restore the pre-drag arrays, then re-apply the full offset (no incremental drift).
  for (const GroupSnap& gs : s.drag.before)
    sym.animator.setCurves(gs.childId, gs.inputId, gs.before);
  for (const DragKey& dk : s.drag.keys)
    if (Curve* c = curveAt(sym, dk.key)) c->removeAt(dk.startTime);
  s.selection.clear();
  for (const DragKey& dk : s.drag.keys) {
    Curve* c = curveAt(sym, dk.key);
    if (!c) continue;
    const double newT = Curve::roundTime(dk.startTime + dt);
    VDefinition v = dk.def;
    v.u = newT;
    v.value = dk.startValue + dv;
    c->addOrUpdate(newT, v);  // collision with an UNSELECTED key clobbers it (= AddOrUpdateV 律)
    s.selection.push_back(SelKey{dk.key.childId, dk.key.inputId, dk.key.index, newT});
  }
  return dt;
}

void finishDrag(State& s, SymbolLibrary& lib, CommandStack& stack, const std::string& symbolId,
                Symbol& sym) {
  std::vector<CurveGroupEdit> edits;
  for (const GroupSnap& gs : s.drag.before) {
    const Animator::CurveArray* now = sym.animator.curvesFor(gs.childId, gs.inputId);
    if (!now) continue;
    edits.push_back(CurveGroupEdit{gs.childId, gs.inputId, gs.before, *now});
  }
  auto cmd = std::make_unique<SetCurveGroupSnapshotCommand>(lib, symbolId, std::move(edits),
                                                            "Move Keys");
  if (!cmd->refused()) stack.push(std::move(cmd));
  s.drag = DragState{};
}

void applyTangentDrag(Curve& c, double keyTime, bool inSide, double angle, bool breakTangents) {
  auto it = c.table().find(Curve::roundTime(keyTime));
  if (it == c.table().end()) return;
  VDefinition& v = it->second;  // in-place field mutation: map structure untouched (safe)

  if (inSide) {
    v.inInterpolation = KeyInterpolation::Tangent;
    v.inTangentAngle = angle;
  } else {
    v.outInterpolation = KeyInterpolation::Tangent;
    v.outTangentAngle = angle;
  }
  if (breakTangents) v.brokenTangents = true;
  if (!v.brokenTangents) {
    // Mirror the angle to the opposite side (cs:273-287); tensions stay (unweighted fork).
    if (inSide) {
      v.outInterpolation = KeyInterpolation::Tangent;
      v.outTangentAngle = v.inTangentAngle + kPi;
    } else {
      v.inInterpolation = KeyInterpolation::Tangent;
      v.inTangentAngle = v.outTangentAngle + kPi;
    }
  } else {
    // Broken: promote the opposite side Linear -> Tangent (cs:289-298) so the segment leaves the
    // linear fast path and the side keeps an authored angle from here on (修3).
    KeyInterpolation& other = inSide ? v.outInterpolation : v.inInterpolation;
    if (other == KeyInterpolation::Linear) other = KeyInterpolation::Tangent;
  }
  // NO updateTangents here (修2, contract in timeline_internal.h): it would re-mirror boundary
  // keys and destroy the authored angle. Tangent-mode angles only; neighbors are untouched
  // (their auto angles depend on values/times, which a tangent drag never changes).
}

void runDeleteSelected(State& s, SymbolLibrary& lib, CommandStack& stack,
                       const std::string& symbolId, Symbol& sym) {
  // Group the selection by (childId, inputId).
  std::map<std::pair<int, std::string>, std::vector<SelKey>> groups;
  for (const SelKey& k : s.selection) groups[{k.childId, k.inputId}].push_back(k);
  auto macro = std::make_unique<MacroCommand>("Delete Keyframes");
  for (auto& [gid, keys] : groups) {
    const Animator::CurveArray* arr = sym.animator.curvesFor(gid.first, gid.second);
    if (!arr) continue;
    size_t total = 0;
    for (const Curve& c : *arr) total += c.count();
    // UNIQUE existing selected keys only (修1③): raw selection can carry duplicates/ghosts after
    // merges — size() would inflate past `total` and misroute a PARTIAL delete into
    // RemoveAnimation, killing the unselected keys too. FORK 具名: TiXL tests all-selected
    // per CURVE (AnimationOperations.cs:81-84); ours per (child,input) group — a Vec input only
    // collapses to RemoveAnimation when EVERY channel's keys are selected (safer, kept).
    std::set<std::pair<int, double>> uniq;
    for (const SelKey& k : keys)
      if (k.index >= 0 && k.index < (int)arr->size() &&
          (*arr)[k.index].hasKeyAt(Curve::roundTime(k.time)))
        uniq.insert({k.index, Curve::roundTime(k.time)});
    if (!uniq.empty() && uniq.size() >= total) {
      // Every key of the input selected -> remove the whole animation (driver back to Constant,
      // = TiXL RemoveAnimationsCommand branch, AnimationOperations.cs:80-84).
      auto rm = std::make_unique<RemoveAnimationCommand>(lib, symbolId, gid.first, gid.second);
      if (!rm->refused()) macro->add(std::move(rm));
    } else {
      Animator::CurveArray after = *arr;
      for (const SelKey& k : keys)
        if (k.index >= 0 && k.index < (int)after.size()) after[k.index].removeAt(k.time);
      for (Curve& c : after) c.updateTangents();
      auto ed = std::make_unique<SetCurveGroupSnapshotCommand>(
          lib, symbolId,
          std::vector<CurveGroupEdit>{CurveGroupEdit{gid.first, gid.second, *arr, std::move(after)}},
          "Delete Keyframes");
      if (!ed->refused()) macro->add(std::move(ed));
    }
  }
  if (!macro->empty()) stack.push(std::move(macro));
  s.selection.clear();
}

void runInsertKeys(SymbolLibrary& lib, CommandStack& stack, const std::string& symbolId,
                   const std::vector<SelKey>& at) {
  // = TiXL HandleCreateNewKeyframes: one AddKeyframesCommand per visible curve, wrapped in a
  // MacroCommand "Insert keyframes" (TimelineCurveEditor.cs:242-247) -> single undo step.
  auto macro = std::make_unique<MacroCommand>("Insert keyframes");
  for (const SelKey& k : at)
    macro->add(std::make_unique<AddKeyframeCommand>(lib, symbolId, k.childId, k.inputId, k.index,
                                                    k.time));  // refused-safe: doIt no-ops
  if (!macro->empty()) stack.push(std::move(macro));
}

void executePending(const std::string& symbolId, Symbol& sym, const Geom& g) {
  State& s = state();
  Pending p = s.pending;
  s.pending = Pending{};

  // Drag liveness is centralized HERE (not on imgui item identity): keys reorder/move under the
  // cursor mid-drag, so item ids are unstable — raw mouse-down is the authoritative signal.
  if (s.drag.active) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
      applyDragLive(s, sym, g, ImGui::GetMousePos());
    else
      finishDrag(s, sw::doc::g_lib, sw::g_commands, symbolId, sym);
  }
  if (s.tan.active) {
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
      applyTangentLive(s, sym, g, ImGui::GetMousePos());
    else
      finishTangent(s, sym, symbolId);
  }

  if (p.addKey) {
    auto cmd = std::make_unique<AddKeyframeCommand>(sw::doc::g_lib, symbolId, p.addAt.childId,
                                                    p.addAt.inputId, p.addAt.index, p.addAt.time);
    sw::g_commands.push(std::move(cmd));  // refused-safe: doIt no-ops, undo no-ops
  }
  if (!p.insertKeys.empty())
    runInsertKeys(sw::doc::g_lib, sw::g_commands, symbolId, p.insertKeys);
  if (p.deleteSelected && !s.selection.empty())
    runDeleteSelected(s, sw::doc::g_lib, sw::g_commands, symbolId, sym);
  if (p.setInterp >= 0 && !s.selection.empty()) runSetInterpolation(s, sym, symbolId, p.setInterp);
}

}  // namespace sw::ui::tl
