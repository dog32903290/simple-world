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

void stageDrag(State& s, const Symbol& sym, ImVec2 mouseStart) {
  s.drag = DragState{};
  s.drag.mouseStart = mouseStart;
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
