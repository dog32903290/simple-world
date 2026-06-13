// ui/fence_preview — see fence_preview.h.
// Zone: ui. Depends on runtime(compound_graph: Symbol/SymbolChild) + imgui + imgui-node-editor.
// Pure overlap predicate + a per-frame overlay draw; never mutates graph or selection.
#include "ui/fence_preview.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_node_editor.h"

#include "runtime/compound_graph.h"  // Symbol / SymbolChild (id + canvas x/y)
#include "ui/node_style.h"           // nodeSelectedBorderColor / nodeRounding (match the skin)

namespace ed = ax::NodeEditor;

namespace sw::ui {

// ---------------------------------------------------------------------------
// Pure predicate: do two axis-aligned rects overlap? (= TiXL ImRect.Overlaps,
// used in MagGraphView.HandleSelectionFenceUpdate:315 to decide fence membership.)
// Rects are given as (min,max); touching edges count as overlap (TiXL uses Min<Max
// strict on one side, <= on the other; we use the symmetric "not disjoint" form which
// agrees for any non-degenerate box and is the same test imgui-node-editor's built-in
// fence applies to commit the selection on release — so the preview never lies).
// ---------------------------------------------------------------------------
static bool rectsOverlap(const ImVec2& aMin, const ImVec2& aMax,
                         const ImVec2& bMin, const ImVec2& bMax) {
  if (aMax.x < bMin.x || bMax.x < aMin.x) return false;
  if (aMax.y < bMin.y || bMax.y < aMin.y) return false;
  return true;
}

namespace {

// Fence gesture state (mirrors SelectionFence.States: Inactive / PressedButNotMoved / Updated).
// We DON'T reconstruct CompletedAsArea/Click — the vendored node-editor's built-in fence owns
// the actual selection commit on release (named fork "preview-only-overlay", see header).
enum class FenceState { Inactive, PressedButNotMoved, Active };
FenceState g_state = FenceState::Inactive;
ImVec2 g_startScreen{};  // press point in screen space (= SelectionFence._startPositionInScreen)

// Verify latch (see header): covered child ids from the last Active frame, kept past release.
std::vector<int> g_lastCovered;

// TiXL UserSettings.cs:111 — ClickThreshold = 5px (named fork: hardwired, no UserSettings yet).
constexpr float kClickThreshold = 5.0f;

// Screen-space rect of a child node, via the node-editor's own transform (same source the
// hover-blink border uses, node_draw.cpp:132-135). Returns false if the node has no size yet.
bool childScreenRect(int childId, ImVec2& outMin, ImVec2& outMax) {
  ImVec2 pos = ed::GetNodePosition(childId);
  ImVec2 sz = ed::GetNodeSize(childId);
  if (sz.x <= 0.0f || sz.y <= 0.0f) return false;
  outMin = ed::CanvasToScreen(pos);
  outMax = ed::CanvasToScreen(ImVec2(pos.x + sz.x, pos.y + sz.y));
  return true;
}

}  // namespace

void drawFenceSelectionPreview(const sw::Symbol* cur, bool hostHovered) {
  if (!cur) { g_state = FenceState::Inactive; return; }

  const ImGuiIO& io = ImGui::GetIO();
  const bool leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);

  // --- State machine (= SelectionFence.UpdateAndDraw:29-75) ---
  if (g_state == FenceState::Inactive) {
    // Arm only on a fresh left press that BEGINS a fence: mouse over the canvas host (NOT over a
    // floating Inspector/Output/Timeline window — hostHovered, measured by the caller before
    // ed::Begin), no node/pin under the cursor, Alt not held. (= SelectionFence.cs:31-38: bail
    // if any item hovered / window not hovered / not left-down / KeyAlt.) The node-editor's
    // built-in fence arms on the same condition (left press on background) so the preview tracks it.
    const bool pressedNow = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    const bool overNode = ed::GetHoveredNode() || ed::GetHoveredPin();
    if (pressedNow && hostHovered && !overNode && !io.KeyAlt) {
      g_startScreen = io.MouseClickedPos[ImGuiMouseButton_Left];
      g_state = FenceState::PressedButNotMoved;
    }
    return;
  }

  // Released (anywhere, even off-window) -> hand the commit to the built-in fence; reset.
  if (!leftDown || io.KeyAlt) { g_state = FenceState::Inactive; return; }

  // Below click threshold = still a click, not a drag (SelectionFence.cs:64-68).
  const ImVec2 mouse = io.MousePos;
  const float dx = mouse.x - g_startScreen.x, dy = mouse.y - g_startScreen.y;
  if (g_state == FenceState::PressedButNotMoved &&
      (dx * dx + dy * dy) < kClickThreshold * kClickThreshold) {
    return;
  }
  g_state = FenceState::Active;

  // --- Fence rect (BoundsUnclamped = RectBetweenPoints(start, mouse), SelectionFence.cs:56) ---
  const ImVec2 fMin(std::min(g_startScreen.x, mouse.x), std::min(g_startScreen.y, mouse.y));
  const ImVec2 fMax(std::max(g_startScreen.x, mouse.x), std::max(g_startScreen.y, mouse.y));

  ImDrawList* dl = ImGui::GetWindowDrawList();

  // Live highlight: every node whose screen rect overlaps the fence = "release-now selects this"
  // (= TiXL HandleSelectionFenceUpdate:312-336, which AddSelection's exactly these items every
  //  Updated frame). We draw the selection-border outline (node_style: white, TiXL UiColors.Selection)
  //  rather than mutating ed's selection, so we don't fight the built-in fence (fork: preview-only).
  const ImU32 hi = nodeSelectedBorderColor();
  const float tixlScale = (ed::GetCurrentZoom() > 0.0001f) ? (1.0f / ed::GetCurrentZoom()) : 1.0f;
  const float rounding = nodeRounding(tixlScale);
  g_lastCovered.clear();
  for (const sw::SymbolChild& child : cur->children) {
    ImVec2 nMin, nMax;
    if (!childScreenRect(child.id, nMin, nMax)) continue;
    if (!rectsOverlap(nMin, nMax, fMin, fMax)) continue;
    dl->AddRect(nMin, nMax, hi, rounding, 0, 2.0f);  // 2px = TiXL selection outline weight
    g_lastCovered.push_back(child.id);               // latch the covered set (verify surface)
  }

  // The fence box itself (SelectionFence.cs:72-73): filled Color(0.1f) + a 1px Color(0,0,0,0.4)
  // border one pixel outside. Color(0.1f) = rgba(0.1,0.1,0.1,0.1) (T3 single-float Color ctor).
  dl->AddRectFilled(fMin, fMax, IM_COL32(26, 26, 26, 26));
  dl->AddRect(ImVec2(fMin.x - 1, fMin.y - 1), ImVec2(fMax.x + 1, fMax.y + 1),
              IM_COL32(0, 0, 0, 102));
}

bool fenceActive() { return g_state == FenceState::Active; }

const std::vector<int>& fenceLastCovered() { return g_lastCovered; }

std::string fenceLastCoveredJson() {
  std::vector<int> ids = g_lastCovered;
  std::sort(ids.begin(), ids.end());
  std::string s = "[";
  for (size_t i = 0; i < ids.size(); ++i) s += (i ? ", " : "") + std::to_string(ids[i]);
  s += "]";
  return s;
}

// ---------------------------------------------------------------------------
// runFenceSelfTest — the pure overlap predicate (membership truth table) + injectBug.
// ---------------------------------------------------------------------------
int runFenceSelfTest(bool injectBug) {
  int fail = 0;
  // A 100x100 fence box at (0,0)..(100,100). Cases: clearly inside / partial overlap /
  // edge-touch / clearly outside. (Matches TiXL ImRect.Overlaps membership.)
  struct Case { ImVec2 nMin, nMax; bool want; const char* note; };
  const Case cases[] = {
      {{10, 10}, {40, 40},   true,  "fully inside"},
      {{80, 80}, {140, 140}, true,  "partial corner overlap"},
      {{-20, 40}, {0, 60},   true,  "edge-touch on the left (x: -20..0 meets 0)"},
      {{200, 0}, {260, 100}, false, "clearly to the right (gap on x)"},
      {{0, 200}, {100, 260}, false, "clearly below (gap on y)"},
      {{-60, -60}, {-10, -10}, false, "clearly above-left"},
  };
  const ImVec2 fMin(0, 0), fMax(100, 100);
  for (const Case& c : cases) {
    bool got = rectsOverlap(c.nMin, c.nMax, fMin, fMax);
    // BUG: drop the y-axis disjoint test -> the "clearly below" case wrongly reports overlap.
    if (injectBug) got = !(c.nMax.x < fMin.x || fMax.x < c.nMin.x);
    if (got != c.want) {
      std::printf("[fencepreview] overlap([%g,%g..%g,%g]) = %d expect %d (%s) -> FAIL\n",
                  c.nMin.x, c.nMin.y, c.nMax.x, c.nMax.y, got, c.want, c.note);
      ++fail;
    }
  }
  if (injectBug) {
    std::printf("[fencepreview] injectBug fail=%d (expected nonzero) -> %s\n", fail,
                fail > 0 ? "PASS (red-proof)" : "FAIL");
    return fail > 0 ? 1 : 0;
  }
  std::printf("[fencepreview] overlap cases=%d, fail=%d -> %s\n", (int)(sizeof(cases) / sizeof(cases[0])),
              fail, fail == 0 ? "PASS" : "FAIL");
  return fail;
}

}  // namespace sw::ui
