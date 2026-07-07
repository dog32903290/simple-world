// ui/layout_dock — programmatic default dock layout. See layout_dock.h for the WHY.
// TiXL parity: LayoutHandling.cs (the default is a shipped layout0.json ImGui ini blob; sw rebuilds
// the equivalent tree with DockBuilder because io.IniFilename is nullptr — "DockBuilder-not-ini_fork").
#include "ui/layout_dock.h"

#include <string_view>

#include "imgui.h"
#include "imgui_internal.h"  // DockBuilder* (early end-user API, imgui_internal only)

namespace sw::ui {
namespace {

// ─────────────────────────────────────────────────────────────────────────────────────────────
//  ▼▼▼ 柏為可調 — 預設佈局 partition 表 ▼▼▼  (data-driven, 鐵律 7: add a window = add a row)
//
//  The default dock tree is described as a flat list of SPLITS off the central (canvas) node,
//  then a list of WINDOW→REGION assignments. To retune the layout, edit these two tables only —
//  no builder-code changes. Every ratio is the fraction the SPLIT-OFF side takes (0..1).
//
//  Tree shape (TiXL standard edge-dock, canvas fills the middle):
//      ┌────────────────── Toolbar (top strip) ──────────────────┐
//      │ ┌────────────────────────────────┬─────────────────────┐│
//      │ │                                │  Inspector (top)    ││
//      │ │        CANVAS  (central,       ├─────────────────────┤│
//      │ │        passthru底板)           │  Output / Render /  ││
//      │ │                                │  Theme / Variation /││
//      │ │                                │  Asset  (tabs, bot) ││
//      │ ├────────────────────────────────┴─────────────────────┤│
//      │ │            Timeline  (bottom strip)                   ││
//      │ └──────────────────────────────────────────────────────┘│
//      └─────────────────────────────────────────────────────────┘
// ─────────────────────────────────────────────────────────────────────────────────────────────

// A named region carved off the central node. Splits apply in ORDER, each off the region named
// in `from` ("" = the root/central node at that moment). ImGuiDir_ says which edge; `ratio` is the
// fraction the NEW region (the `name` side) takes of `from`.
struct Split {
  const char* name;    // handle for later reference (assign windows here, or split further from it)
  const char* from;    // region to split ("" = current central node)
  ImGuiDir    dir;     // which side `name` lands on
  float       ratio;   // 柏為可調 — fraction taken by the `name` side (0..1)
};

// ORDER MATTERS: splits execute top-to-bottom; later splits can carve off earlier regions.
constexpr Split kSplits[] = {
    // name        from        direction         ratio  (柏為可調)
    {"top",     "",         ImGuiDir_Up,      0.120f},  // Toolbar strip: tall enough for its 3 rows
    //                                                    (New… / Audio In / Play-BPM-Speed). At the
    //                                                    default 720px window that's ~86px; shrink it
    //                                                    if 柏為 later drops the transport row down.
    {"bottom",  "",         ImGuiDir_Down,    0.300f},  // Timeline strip: ~216px @720, matches the
    //                                                    old floating height so the curve editor has
    //                                                    room to plot (a shorter strip clips it).
    {"right",   "",         ImGuiDir_Right,   0.300f},  // right tool column (off what remains = middle)
    //                                                    was 0.240 — too narrow for the Output toolbar's
    //                                                    resolution combo, which used to get pushed past
    //                                                    the panel's right edge and become unclickable
    //                                                    (柏為 couldn't reach it). 0.300 fits combo+Pin+
    //                                                    Fit+1:1 on row 1; Snapshot+bg-swatch still wrap
    //                                                    to row 2 at the default 1024px window — but the
    //                                                    wrap (output_window.cpp sameLineIfFits) keeps
    //                                                    every control inside the panel either way.
    {"right_bt","right",    ImGuiDir_Down,    0.620f},  // right column's lower stack (tabs)
    //                                                    → "right" keeps the top 38%  (Inspector)
    //                                                    → "right_bt" is the bottom 62% (tab group)
};

// Window → region assignment (window title MUST match the ImGui::Begin() string exactly).
// Windows sharing a region become tabs in that dock node. Order here = tab order.
struct Placement {
  const char* window;  // ImGui::Begin title (see each ui/*.cpp)
  const char* region;  // a Split.name above, or "central" for the canvas node
};

constexpr Placement kPlacements[] = {
    {"Toolbar",       "top"},
    {"Inspector",     "right"},       // top-right, always-visible params
    {"Output",        "right_bt"},    // live preview — first tab of the bottom-right stack
    {"Render",        "right_bt"},    // render-to-file settings (tab)
    {"Color Theme",   "right_bt"},    // theme editor (tab)
    {"Variation",     "right_bt"},    // snapshot pool (tab)
    {"Asset Library", "right_bt"},    // asset browser (tab)
    {"Timeline",      "bottom"},      // dope-sheet strip
    // Canvas (##canvas_host) is NOT docked — it is the passthru central底板 drawn over the dockspace.
};

// ─────────────────────────────────────────────────────────────────────────────────────────────
//  ▲▲▲ 柏為可調 partition 表結束 ▲▲▲
// ─────────────────────────────────────────────────────────────────────────────────────────────

bool g_wantRebuild = true;  // true on startup and after resetDockLayout()

// Resolve a region name to its live dock-node id. "central"/"" → the current central node.
// Filled during the build below (small fixed set → linear scan is fine, no map needed).
struct RegionId { const char* name; ImGuiID id; };

}  // namespace

ImGuiID dockspaceId() {
  // A stable GLOBAL id (window-independent). We pass this EXPLICITLY to DockSpaceOverViewport, which
  // uses a non-zero id verbatim (it only derives GetID("DockSpace") when the id is 0 — imgui.cpp:
  // 19591). ImHashStr (not ImGui::GetID) is deliberate: GetID reads the CURRENT window's id stack,
  // which is null when we build the layout outside a frame → segfault. ImHashStr(seed=0) is the same
  // hash ImGui uses for a root/global id and needs no current window.
  return ImHashStr("sw_MainDockSpace");
}

void resetDockLayout() { g_wantRebuild = true; }

void ensureDockLayout(const ImGuiViewport* viewport) {
  const ImGuiID rootId = dockspaceId();

  // Already built this run? A live node WITH children means our tree is standing — nothing to do.
  // (First frame: node absent → build. After resetDockLayout(): g_wantRebuild forces a rebuild.)
  ImGuiDockNode* existing = ImGui::DockBuilderGetNode(rootId);
  const bool built = existing != nullptr && existing->IsSplitNode();
  if (built && !g_wantRebuild) return;
  g_wantRebuild = false;

  // (Re)create the root dockspace node at the viewport size, then carve the tree.
  ImGui::DockBuilderRemoveNode(rootId);
  ImGui::DockBuilderAddNode(rootId, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(rootId, viewport->WorkSize);

  // Region table: "central" starts as the root; each split adds one region + rebinds the source.
  RegionId regions[1 + IM_ARRAYSIZE(kSplits)];
  int regionCount = 0;
  regions[regionCount++] = {"central", rootId};

  auto findRegion = [&](const char* name) -> int {
    const char* key = (name == nullptr || name[0] == '\0') ? "central" : name;
    for (int i = 0; i < regionCount; ++i)
      if (std::string_view(regions[i].name) == key) return i;
    return 0;  // fall back to central (never expected — table is authored consistently)
  };

  for (const Split& s : kSplits) {
    const int srcIdx = findRegion(s.from);
    ImGuiID sideNew = 0, sideKeep = 0;
    // ratio is the fraction the NEW (`name`) side takes of the source node.
    ImGui::DockBuilderSplitNode(regions[srcIdx].id, s.dir, s.ratio, &sideNew, &sideKeep);
    regions[srcIdx].id = sideKeep;                 // the source region shrinks to the kept remainder
    regions[regionCount++] = {s.name, sideNew};    // the split-off side is a new named region
  }

  // Dock each window into its region (by name — works even for windows not submitted this frame,
  // e.g. Render/Variation/Theme which only Begin when visible; they appear docked once shown).
  for (const Placement& p : kPlacements) {
    const int idx = findRegion(p.region);
    ImGui::DockBuilderDockWindow(p.window, regions[idx].id);
  }

  ImGui::DockBuilderFinish(rootId);
}

}  // namespace sw::ui
