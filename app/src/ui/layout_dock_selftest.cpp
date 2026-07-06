// ui/layout_dock_selftest — isolation tooth for the programmatic default dock layout.
// Zone: ui (shell-tier registration). Reached via --selftest-dock-layout (+ the -bug refuter).
//
// What it proves (all headless — a throwaway ImGui context, no window/Metal):
//   (1) DETERMINISM: building the default tree twice yields the SAME dock-node ids for every window
//       (same-startup-same-coords — the whole reason the harness can trust the layout, task §4d).
//   (2) TABLE CONFORMANCE: each of the 8 tool windows lands in a REAL split node (not the root /
//       central node), i.e. every kPlacements row actually docked. Windows sharing a region (the
//       lower-right tab stack) share one dock-node id (tab grouping intact).
//   (3) CANVAS UNTOUCHED: the central node exists and is distinct from every tool window's node
//       (the canvas底板 is never docked over).
// injectBug: rebuild the layout a second time AFTER a resetDockLayout() with a DIFFERENT viewport
//   size but compare against the first ids — no; simpler & sharper: the bug path skips the SECOND
//   ensureDockLayout() build entirely, so the "second build docked the window somewhere" lookup
//   returns the stale/none node and the determinism check bites. See below.
#include "ui/layout_dock.h"

#include <cstdio>
#include <cstring>

#include "imgui.h"
#include "imgui_internal.h"  // DockBuilderGetNode / ImGuiDockNode / FindWindowSettingsByID

#include "runtime/selftest_registry.h"  // REGISTER_SELFTESTS

namespace sw {

namespace {
int g_fail = 0;
void check(bool cond, const char* what) {
  if (!cond) { std::printf("[selftest-dock-layout] FAIL: %s\n", what); ++g_fail; }
}

// The 8 dockable tool windows (must mirror kPlacements in layout_dock.cpp). Kept local — this tooth
// asserts the WINDOWS all dock; the exact tree shape is 柏為-tunable data, not a parity invariant.
const char* const kWindows[] = {
    "Toolbar", "Inspector", "Output", "Render", "Color Theme", "Variation", "Asset Library", "Timeline",
};

// The dock-node id a window is assigned to (0 if not docked). DockBuilderDockWindow records the
// binding in the window's settings even before the window is submitted, so we read it back there.
ImGuiID dockedNodeOf(const char* windowName) {
  // A top-level window's id is ImHashStr(name, 0) (imgui.cpp:19628 uses exactly this for docking).
  // ImHashStr (not ImGui::GetID) because GetID reads the current window's id stack, which is null
  // when we query outside a frame → segfault.
  ImGuiWindowSettings* ws = ImGui::FindWindowSettingsByID(ImHashStr(windowName));
  return ws ? ws->DockId : 0;
}
}  // namespace

int runDockLayoutSelfTest(bool injectBug) {
  g_fail = 0;

  // Headless ImGui context: DockBuilder needs a live context + a built font atlas + a DisplaySize,
  // but NO renderer/backend and NO real frame submission (the builder API runs outside NewFrame).
  IMGUI_CHECKVERSION();
  ImGuiContext* ctx = ImGui::CreateContext();
  ImGui::SetCurrentContext(ctx);
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;                 // never touch disk (mirror the app; harness-safe)
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.DisplaySize = ImVec2(1280.0f, 800.0f);
  unsigned char* px = nullptr;
  int fw = 0, fh = 0;
  io.Fonts->GetTexDataAsRGBA32(&px, &fw, &fh);  // force the atlas to build (else NewFrame asserts)

  // A viewport stand-in: the real ensureDockLayout reads viewport->WorkSize only. Use the main
  // viewport (exists once a context exists) and stamp a WorkSize.
  ImGuiViewport* vp = ImGui::GetMainViewport();
  vp->WorkSize = ImVec2(1280.0f, 776.0f);
  vp->Size = ImVec2(1280.0f, 800.0f);

  // DockBuilderAddNode(…, ImGuiDockNodeFlags_DockSpace) calls DockSpace() internally, which reads
  // the CURRENT window — so the builder must run INSIDE a frame (imgui.cpp:19692-19695). In the app,
  // ensureDockLayout() runs after ImGui::NewFrame() (a current window exists). Mirror that here: wrap
  // each build in NewFrame/EndFrame. No backend/render — EndFrame without RenderDrawData is legal.
  auto buildInFrame = [&]() {
    ImGui::NewFrame();
    sw::ui::ensureDockLayout(vp);
    ImGui::EndFrame();
  };

  const ImGuiID rootId = sw::ui::dockspaceId();

  // ── Build #1 ────────────────────────────────────────────────────────────────────────────────
  sw::ui::resetDockLayout();
  buildInFrame();

  // injectBug: simulate "the partition splits silently failed and every window piled into the root
  // node" (a real regression mode if kSplits/kPlacements ever desync). Re-dock all windows onto the
  // root — the split/central/tab checks below then bite, and the green pass provably depends on the
  // splits actually landing each window in its own sub-node.
  if (injectBug) {
    ImGui::NewFrame();
    for (const char* w : kWindows) ImGui::DockBuilderDockWindow(w, rootId);
    ImGui::EndFrame();
  }

  ImGuiDockNode* root = ImGui::DockBuilderGetNode(rootId);
  check(root != nullptr, "root dockspace node missing after build #1");
  check(root && root->IsSplitNode(), "root is not a split node (tree never carved)");

  // Record where every window landed on build #1.
  ImGuiID first[IM_ARRAYSIZE(kWindows)];
  for (int i = 0; i < IM_ARRAYSIZE(kWindows); ++i) {
    first[i] = dockedNodeOf(kWindows[i]);
    check(first[i] != 0, kWindows[i]);  // (2) every window docked SOMEWHERE
    check(first[i] != rootId, "window docked into the root/central node (should be a split)");
  }

  // (3) canvas central node exists and is none of the tool-window nodes.
  ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(rootId);
  check(central != nullptr, "no central node (canvas底板 has nowhere to sit)");
  if (central) {
    for (int i = 0; i < IM_ARRAYSIZE(kWindows); ++i)
      check(first[i] != central->ID, "a tool window was docked into the central canvas node");
  }

  // The lower-right tab stack (Output/Render/Color Theme/Variation/Asset Library) shares one node.
  // Indices 2..6 in kWindows. Assert they all match Output's node (tab grouping intact).
  for (int i = 3; i <= 6; ++i)
    check(first[i] == first[2], "lower-right tab-stack windows did not share one dock node");

  // ── Build #2 — DETERMINISM (1): reset + rebuild at the SAME size ⇒ the SAME dock-node ids ─────
  // This is the harness-critical property: same startup ⇒ same coordinates (task §4d). The dock-node
  // ids hash the split path, so a stable table gives stable ids on every run.
  sw::ui::resetDockLayout();
  buildInFrame();

  for (int i = 0; i < IM_ARRAYSIZE(kWindows); ++i) {
    const ImGuiID again = dockedNodeOf(kWindows[i]);
    check(again == first[i], kWindows[i]);  // same window ⇒ same node id across rebuilds
  }

  ImGui::DestroyContext(ctx);
  if (g_fail == 0) std::printf("[selftest-dock-layout] PASS\n");
  return g_fail == 0 ? 0 : 1;
}

// High orderBase → APPENDS near the end of --selftest-list (window-follow uses 810).
REGISTER_SELFTESTS(/*orderBase=*/812, {"dock-layout", runDockLayoutSelfTest});

}  // namespace sw
