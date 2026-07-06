// ui/layout_dock — programmatic default dock layout (TiXL Layouts, ini-free adaptation).
// Zone: ui. Pure imgui DockBuilder; no Metal/cook/runtime/platform deps.
//
// WHY programmatic (not ini): TiXL persists each layout as JSON = window configs +
//   ImGui.SaveIniSettingsToMemory() (LayoutHandling.cs:230-232). The shipped default is
//   layout0.json; on resize TiXL just re-applies the configs and lets ImGui's dock engine
//   keep the ratios (LayoutHandling.UpdateAfterResize:74-87 → WindowManager.UpdateAppWindowSize).
//   sw keeps io.IniFilename = nullptr (harness coordinate determinism, CONTEXT_PACK §three),
//   so there is no ini to restore. Instead we rebuild the SAME default tree programmatically
//   with DockBuilder on first frame. Fork (named): "DockBuilder-not-ini_fork" — same end state
//   (windows docked into a proportional tree, ImGui keeps ratios on resize), different source.
//
// The default layout + all split ratios live in ONE data-driven table (kPartitions) at the top
// of layout_dock.cpp — 柏為 tunes the numbers there; adding a window = adding one row (鐵律 7).
#pragma once

#include "imgui.h"

namespace sw::ui {

// Stable dockspace id shared by the DockSpaceOverViewport host call (main.cpp) and the builder.
// Both MUST pass this exact id so the builder's tree lands under the same dockspace the host submits.
ImGuiID dockspaceId();

// Build the default dock tree ONCE (idempotent: no-op if the dockspace node already exists with
// children, i.e. already built this run). Call BEFORE DockSpaceOverViewport each frame — DockBuilder
// requires being called before the dockspace node is submitted (imgui.cpp:19605). `viewport` sizes
// the root node; pass ImGui::GetMainViewport().
void ensureDockLayout(const ImGuiViewport* viewport);

// Force a rebuild on the next ensureDockLayout() call (used by the selftest to build twice and
// compare, and available if a future "reset layout" action is wired). Does not itself build.
void resetDockLayout();

}  // namespace sw::ui
