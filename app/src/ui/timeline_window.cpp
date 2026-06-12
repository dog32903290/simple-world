// ui/timeline_window — dope-sheet timeline draw + key gestures (S3 GUI). See header for范圍.
#include "ui/timeline_window.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"

#include "app/animation_commands.h"
#include "app/command.h"
#include "app/document.h"
#include "app/frame_cook.h"        // transportPosition / transportScrub (the shared playhead surface)
#include "runtime/compound_graph.h"
#include "runtime/curve.h"
#include "runtime/graph.h"          // findSpec (port display names)
#include "verify/eye/eye.h"         // one-line hooks: lane/key rects + playhead for the hand

namespace sw::ui {
namespace {

// --- Layout constants (pixels) ---
constexpr float kLaneLabelW = 150.0f;  // left gutter width for "child.input" labels
constexpr float kLaneH = 22.0f;        // per-lane height
constexpr float kRulerH = 20.0f;       // time ruler height at the top
constexpr float kKeyR = 5.0f;          // keyframe diamond half-size (hit + draw)
constexpr double kBarsPerScreen = 16.0; // how many bars the content area shows (no zoom yet — first cut)

// One animated channel to draw: which (child,input) + array index, plus its resolved label.
struct Lane {
  int childId;
  std::string inputId;
  int index;
  std::string label;
};

// Selected key (session-only): identifies a (lane, time) in the current symbol. -1 childId = none.
int g_selChild = -1;
std::string g_selInput;
int g_selIndex = 0;
double g_selTime = 0.0;
bool g_hasSel = false;

// Drag state for moving the selected key (one MoveKeyframe command per drag, pushed on release).
bool g_dragging = false;
double g_dragOrigTime = 0.0;   // the key's time when the drag started (command's oldTime)
float g_dragOrigValue = 0.0f;  // its value at drag start (so a pure horizontal drag keeps value)

void clearSelection() { g_hasSel = false; g_selChild = -1; g_selInput.clear(); }

// Deferred mutation (BUG-B 修): a key gesture's command mutates the very std::map we're iterating
// (MoveKeyframe -> removeAt -> map::erase; AddKeyframe -> addOrUpdate -> map::insert), invalidating
// the range-for iterator -> heap-use-after-free on ++it. Standard imgui pattern: the per-lane loop
// only RECORDS one pending action; we push the command AFTER the loop closes, when no iterator is live.
struct PendingAction {
  enum Kind { None, Move, Add } kind = None;
  int childId = -1;
  std::string inputId;
  int index = 0;
  double oldTime = 0.0;   // Move: the key's original time / Add: the new key's time
  double newTime = 0.0;   // Move only
  float value = 0.0f;     // Move only (the value carried across)
};

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

  // Collect the lanes from the current symbol's Animator (one per channel of each animated input).
  std::vector<Lane> lanes;
  for (const auto& [childId, byInput] : sym->animator.all()) {
    for (const auto& [inputId, curves] : byInput) {
      for (int idx = 0; idx < (int)curves.size(); ++idx) {
        Lane ln;
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

  // --- geometry: content rect (right of the label gutter, below the ruler) ---
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const float availW = ImGui::GetContentRegionAvail().x;
  const float contentX0 = origin.x + kLaneLabelW;
  const float contentX1 = origin.x + availW;
  const float contentW = std::max(50.0f, contentX1 - contentX0);
  const float rulerY = origin.y;
  const float lanesY0 = rulerY + kRulerH;

  // time(bars) <-> screen X. Fixed window [0, kBarsPerScreen] across contentW (no scroll/zoom yet).
  auto timeToX = [&](double bars) { return contentX0 + (float)(bars / kBarsPerScreen) * contentW; };
  auto xToTime = [&](float x) { return (double)((x - contentX0) / contentW) * kBarsPerScreen; };

  ImDrawList* dl = ImGui::GetWindowDrawList();

  // BUG-B: any keyframe mutation is deferred to here, executed only after the per-lane loop closes.
  PendingAction pending;

  // Ruler: a bar tick + label every bar.
  dl->AddRectFilled(ImVec2(contentX0, rulerY), ImVec2(contentX1, lanesY0),
                    IM_COL32(28, 30, 38, 255));
  for (int bar = 0; bar <= (int)kBarsPerScreen; ++bar) {
    float x = timeToX((double)bar);
    dl->AddLine(ImVec2(x, rulerY), ImVec2(x, lanesY0 + lanes.size() * kLaneH),
                IM_COL32(60, 64, 76, 255));
    if (bar % 2 == 0) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%d", bar);
      dl->AddText(ImVec2(x + 2, rulerY + 2), IM_COL32(150, 154, 166, 255), buf);
    }
  }

  // --- lanes + keyframes ---
  for (size_t li = 0; li < lanes.size(); ++li) {
    const Lane& ln = lanes[li];
    const float laneY = lanesY0 + li * kLaneH;
    const float laneMidY = laneY + kLaneH * 0.5f;

    // Label gutter.
    dl->AddText(ImVec2(origin.x + 2, laneY + 3), IM_COL32(210, 214, 224, 255), ln.label.c_str());
    // Lane background stripe (alternating).
    dl->AddRectFilled(ImVec2(contentX0, laneY), ImVec2(contentX1, laneY + kLaneH),
                      (li & 1) ? IM_COL32(24, 26, 33, 255) : IM_COL32(20, 22, 28, 255));

    // eye hook: the lane row rect (hand can target the empty lane to double-click-add).
    sw::eye::recordRect(("tl_lane:" + std::to_string(ln.childId) + ":" + ln.inputId).c_str(),
                        contentX0, laneY, contentX1, laneY + kLaneH);

    const Animator::CurveArray* arr = sym->animator.curvesFor(ln.childId, ln.inputId);
    if (!arr || ln.index >= (int)arr->size()) continue;
    const Curve& curve = (*arr)[ln.index];

    // Each key = a clickable diamond at (timeToX(u), laneMidY).
    int kiSeq = 0;
    for (const auto& [u, vdef] : curve.table()) {
      float kx = timeToX(u);
      ImVec2 kpos(kx, laneMidY);
      const bool isSel = g_hasSel && g_selChild == ln.childId && g_selInput == ln.inputId &&
                         g_selIndex == ln.index && Curve::roundTime(g_selTime) == u;

      // Invisible button for hit-test (Mac/imgui-stable; no OS mouse needed for the hand).
      ImGui::SetCursorScreenPos(ImVec2(kx - kKeyR, laneMidY - kKeyR));
      ImGui::PushID((int)(li * 1000 + kiSeq));
      ImGui::InvisibleButton("##key", ImVec2(kKeyR * 2, kKeyR * 2));
      const bool hovered = ImGui::IsItemHovered();
      // eye hook: this key's rect, keyed by lane+sequence (ASCII-stable).
      sw::eye::recordRect(("tl_key:" + std::to_string(ln.childId) + ":" + ln.inputId + ":" +
                           std::to_string(kiSeq)).c_str(),
                          kx - kKeyR, laneMidY - kKeyR, kx + kKeyR, laneMidY + kKeyR);

      // Single click -> select this key (and the selected lane).
      if (ImGui::IsItemActivated()) {
        g_hasSel = true;
        g_selChild = ln.childId; g_selInput = ln.inputId; g_selIndex = ln.index; g_selTime = u;
        g_dragging = false;
        g_dragOrigTime = u;
        g_dragOrigValue = (float)vdef.value;
      }
      // Drag -> move time (horizontal). Value stays put (no vertical-value drag in the first cut —
      // value editing is the Inspector's slider / playhead-write path, 報告具名).
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        g_dragging = true;
      }
      // Release after a drag -> RECORD a MoveKeyframe (deferred; the command erases this map node,
      // so it must not run while we're iterating curve.table() — BUG-B).
      if (g_dragging && ImGui::IsItemDeactivated()) {
        double newTime = std::max(0.0, xToTime(ImGui::GetMousePos().x));
        if (Curve::roundTime(newTime) != Curve::roundTime(g_dragOrigTime)) {
          pending.kind = PendingAction::Move;
          pending.childId = ln.childId; pending.inputId = ln.inputId; pending.index = ln.index;
          pending.oldTime = g_dragOrigTime; pending.newTime = newTime; pending.value = g_dragOrigValue;
          g_selTime = newTime;  // keep the selection on the moved key
        }
        g_dragging = false;
      }

      // Draw the diamond.
      ImU32 col = isSel ? IM_COL32(255, 210, 90, 255)
                        : hovered ? IM_COL32(220, 224, 235, 255) : IM_COL32(150, 180, 240, 255);
      ImVec2 c = kpos;
      dl->AddQuadFilled(ImVec2(c.x, c.y - kKeyR), ImVec2(c.x + kKeyR, c.y),
                        ImVec2(c.x, c.y + kKeyR), ImVec2(c.x - kKeyR, c.y), col);
      ImGui::PopID();
      ++kiSeq;
    }

    // Double-click on the empty lane background = add a key at that time (on the curve's value there).
    ImGui::SetCursorScreenPos(ImVec2(contentX0, laneY));
    ImGui::PushID((int)(li + 9000));
    ImGui::InvisibleButton("##lanebg", ImVec2(contentW, kLaneH));
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
      double t = std::max(0.0, xToTime(ImGui::GetMousePos().x));
      // RECORD an Add (deferred): the command's addOrUpdate mutates the curve whose table() we just
      // iterated above; `curve` is still a live reference here. Run it after the lane loop (BUG-B).
      pending.kind = PendingAction::Add;
      pending.childId = ln.childId; pending.inputId = ln.inputId; pending.index = ln.index;
      pending.oldTime = t;
      g_hasSel = true;
      g_selChild = ln.childId; g_selInput = ln.inputId; g_selIndex = ln.index; g_selTime = t;
    }
    ImGui::PopID();
  }

  // BUG-B: execute the single recorded mutation now that the per-lane loop (and its curve.table()
  // iterators) have all gone out of scope. At most one key gesture fires per frame.
  if (pending.kind == PendingAction::Move) {
    sw::g_commands.push(std::make_unique<sw::MoveKeyframeCommand>(
        sw::doc::g_lib, symbolId, pending.childId, pending.inputId, pending.index, pending.oldTime,
        pending.newTime, pending.value));
  } else if (pending.kind == PendingAction::Add) {
    sw::g_commands.push(std::make_unique<sw::AddKeyframeCommand>(
        sw::doc::g_lib, symbolId, pending.childId, pending.inputId, pending.index, pending.oldTime));
  }

  // --- playhead: a vertical line at transport.position, draggable to scrub (shared surface) ---
  const float lanesY1 = lanesY0 + lanes.size() * kLaneH;
  const double playPos = sw::framecook::transportPosition();
  const float playX = timeToX(playPos);
  if (playX >= contentX0 - 2 && playX <= contentX1 + 2) {
    dl->AddLine(ImVec2(playX, rulerY), ImVec2(playX, lanesY1), IM_COL32(255, 90, 90, 255), 1.5f);
    dl->AddTriangleFilled(ImVec2(playX - 5, rulerY), ImVec2(playX + 5, rulerY),
                          ImVec2(playX, rulerY + 7), IM_COL32(255, 90, 90, 255));
  }
  // A grab strip over the ruler = drag to scrub (= toolbar's transportScrub, can't drift).
  ImGui::SetCursorScreenPos(ImVec2(contentX0, rulerY));
  ImGui::InvisibleButton("##playhead", ImVec2(contentW, kRulerH));
  sw::eye::recordRect("tl_playhead", contentX0, rulerY, contentX1, rulerY + kRulerH);
  if (ImGui::IsItemActive()) {
    double t = std::max(0.0, xToTime(ImGui::GetMousePos().x));
    sw::framecook::transportScrub(t);  // SAME control surface as the toolbar Pos drag
  }

  // Reserve the laid-out height so the window scrolls/sizes correctly.
  ImGui::SetCursorScreenPos(ImVec2(origin.x, lanesY1 + 4));
  ImGui::Dummy(ImVec2(1, 1));

  // --- Delete (and Backspace, the Mac雷) removes the selected key via an undoable command ---
  if (g_hasSel && ImGui::IsWindowFocused() &&
      (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
    sw::g_commands.push(std::make_unique<sw::DeleteKeyframeCommand>(
        sw::doc::g_lib, symbolId, g_selChild, g_selInput, g_selIndex, g_selTime));
    clearSelection();
  }

  ImGui::End();
}

}  // namespace sw::ui
