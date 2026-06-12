// ui/timeline_select — the timeline's selection state + helpers (S6; split out of timeline_edit
// along the "who owns the selection" seam, ARCHITECTURE rule 4). Holds the session singleton and
// everything that reads/edits `State::selection` WITHOUT mutating curves; all curve mutation
// stays in timeline_edit.cpp (executePending — 批次7 BUG-B 律).
// Contract + TiXL mapping in timeline_internal.h.
#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "runtime/compound_graph.h"
#include "runtime/curve.h"
#include "ui/timeline_internal.h"
#include "ui/timeline_window.h"  // timelineSelectionJson (the eye state.json surface)

namespace sw::ui {

// eye state.json surface (批次9): the selection as machine-checkable json, so a driver can
// PROVE "that click selected the key" instead of guessing from pixel color (D2 k1 懸案).
// Composed here — the selection owner — and handed to main's state composer as one string
// (eye.h: the caller composes the json; verify stays a leaf).
std::string timelineSelectionJson() {
  const tl::State& s = tl::state();
  std::string j = "{\"count\": " + std::to_string(s.selection.size()) + ", \"keys\": [";
  for (size_t i = 0; i < s.selection.size(); ++i) {
    const tl::SelKey& k = s.selection[i];
    j += std::string(i ? ", " : "") + "{\"childId\": " + std::to_string(k.childId) +
         ", \"inputId\": \"" + k.inputId + "\", \"index\": " + std::to_string(k.index) +
         ", \"time\": " + std::to_string(Curve::roundTime(k.time)) + "}";
  }
  return j + "]}";
}

}  // namespace sw::ui

namespace sw::ui::tl {
namespace {

bool sameKey(const SelKey& a, const SelKey& b) {
  return a.childId == b.childId && a.inputId == b.inputId && a.index == b.index &&
         Curve::roundTime(a.time) == Curve::roundTime(b.time);
}

}  // namespace

State& state() {
  static State s;
  return s;
}

bool isSelected(const State& s, int childId, const std::string& inputId, int index, double time) {
  const double rt = Curve::roundTime(time);
  for (const SelKey& k : s.selection)
    if (k.childId == childId && k.index == index && Curve::roundTime(k.time) == rt &&
        k.inputId == inputId)
      return true;
  return false;
}

bool selectOnClickOrDrag(State& s, const SelKey& k, bool alreadySelected, bool shift, bool cmd) {
  if (cmd) {  // = TiXL: ctrl(cmd) click deselects; either way the press can't start a drag
    if (alreadySelected)
      s.selection.erase(std::remove_if(s.selection.begin(), s.selection.end(),
                                       [&](const SelKey& e) { return sameKey(e, k); }),
                        s.selection.end());
    return true;
  }
  if (!alreadySelected) {
    if (!shift) s.selection.clear();
    s.selection.push_back(k);
  }
  return false;
}

void paramKeysAtTime(const Symbol& sym, int childId, const std::string& inputId, double time,
                     std::vector<SelKey>& out) {
  // 修4: the channel group at one rounded time (= TiXL FindParameterKeysAtPosition,
  // DopeSheetArea.cs:976-987; contract in timeline_internal.h). FORK 具名 "roundTime equality":
  // TiXL matches |U-u| < 1/120 — ours uses the TimePrecision slot, the same identity every other
  // selection structure (SelKey/isSelected/dedupe) already keys on.
  out.clear();
  const double rt = Curve::roundTime(time);
  const Animator::CurveArray* arr = sym.animator.curvesFor(childId, inputId);
  if (!arr) return;
  for (int idx = 0; idx < (int)arr->size(); ++idx)
    if ((*arr)[idx].hasKeyAt(rt)) out.push_back(SelKey{childId, inputId, idx, rt});
}

bool selectParamKeysOnClickOrDrag(State& s, const Symbol& sym, const SelKey& k,
                                  bool alreadySelected, bool shift, bool cmd) {
  // 修4 (contract in timeline_internal.h): dope-view selection acts on the WHOLE channel group —
  // same modifier semantics as selectOnClickOrDrag, applied to every sibling key at k's time.
  std::vector<SelKey> group;
  paramKeysAtTime(sym, k.childId, k.inputId, k.time, group);
  if (group.empty()) group.push_back(k);
  if (cmd) {  // cmd-deselect drops the whole group (= TiXL's FindParameterKeysAtPosition loop)
    if (alreadySelected)
      for (const SelKey& gk : group)
        s.selection.erase(std::remove_if(s.selection.begin(), s.selection.end(),
                                         [&](const SelKey& e) { return sameKey(e, gk); }),
                          s.selection.end());
    return true;
  }
  if (!alreadySelected) {
    if (!shift) s.selection.clear();
    for (const SelKey& gk : group)
      if (!isSelected(s, gk.childId, gk.inputId, gk.index, gk.time)) s.selection.push_back(gk);
  }
  return false;
}

void dedupeSelection(std::vector<SelKey>& sel) {
  // O(n^2) over the selection — n is human-scale. Keeps first occurrence, preserves order.
  std::vector<SelKey> out;
  out.reserve(sel.size());
  for (const SelKey& k : sel) {
    bool dup = false;
    for (const SelKey& e : out)
      if (sameKey(e, k)) { dup = true; break; }
    if (!dup) out.push_back(k);
  }
  sel.swap(out);
}

void pruneSelection(State& s, const Symbol& sym) {
  s.selection.erase(std::remove_if(s.selection.begin(), s.selection.end(),
                                   [&](const SelKey& k) {
                                     const Animator::CurveArray* arr =
                                         sym.animator.curvesFor(k.childId, k.inputId);
                                     if (!arr || k.index < 0 || k.index >= (int)arr->size())
                                       return true;
                                     return !(*arr)[k.index].hasKeyAt(Curve::roundTime(k.time));
                                   }),
                    s.selection.end());
  // One key = one entry, ALWAYS: merges/clobbers can leave two entries naming the same key, and a
  // duplicated selection inflates runDeleteSelected's count into the RemoveAnimation misroute
  // (修1②③/修5). Pruning runs every frame, so the ghost never survives a frame.
  dedupeSelection(s.selection);
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) s.suppressDragFromClick = false;
}

void stageDrag(State& s, const Symbol& sym, const Geom& g, ImVec2 mouseStart, const SelKey* grab) {
  s.drag = DragState{};
  s.drag.mouseStart = mouseStart;
  // 修5: freeze the grab point in time/value space with the DRAWN transform of the stage frame —
  // the per-frame mapping is then absolute (dragDeltaFromMouse), drift-free under damping.
  s.drag.mouseStartTime = g.xToTime(mouseStart.x);
  s.drag.mouseStartValue = g.yToValue(mouseStart.y);
  std::map<std::pair<int, std::string>, bool> seen;
  for (const SelKey& k : s.selection) {
    const Animator::CurveArray* arr = sym.animator.curvesFor(k.childId, k.inputId);
    if (!arr || k.index < 0 || k.index >= (int)arr->size()) continue;
    auto it = (*arr)[k.index].table().find(Curve::roundTime(k.time));
    if (it == (*arr)[k.index].table().end()) continue;
    DragKey dk;
    dk.key = k;
    dk.startTime = it->first;
    dk.startValue = it->second.value;
    dk.def = it->second;
    s.drag.keys.push_back(std::move(dk));
    if (!seen[{k.childId, k.inputId}]) {
      seen[{k.childId, k.inputId}] = true;
      s.drag.before.push_back(GroupSnap{k.childId, k.inputId, *arr});
    }
  }
  s.drag.active = !s.drag.keys.empty();
  // Snap reference = the grabbed key's original time (= TiXL snaps the dragged vDef's U,
  // DopeSheetArea.cs:925-936); fall back to the first staged key.
  if (s.drag.active)
    s.drag.refStartTime = grab ? Curve::roundTime(grab->time) : s.drag.keys[0].startTime;
}

void stageTangentDrag(State& s, const Symbol& sym, const SelKey& k, bool inSide) {
  s.tan = TangentDrag{};
  const Animator::CurveArray* arr = sym.animator.curvesFor(k.childId, k.inputId);
  if (!arr) return;
  s.tan.key = k;
  s.tan.inSide = inSide;
  s.tan.before.push_back(GroupSnap{k.childId, k.inputId, *arr});
  s.tan.active = true;
}

}  // namespace sw::ui::tl
