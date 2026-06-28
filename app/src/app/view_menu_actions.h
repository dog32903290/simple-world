// app/view_menu_actions — the View-menu LEAF-INVERSION SEAM (ARCHITECTURE.md "leaf seam").
// Zone: app. The native NSMenu (app/menu.cpp) wants View items (Assets/Variation/Theme windows,
// Toggle-All UI, Focus Mode), but those toggles live in the UI zone (ui/view_modes,
// ui/asset_browser, …) and app MUST NOT include ui/* (ui→app is one-way). So app declares a small
// set of fn-ptr hooks here; the UI registers the real fns at startup (where ui headers ARE
// includable — see ui/view_menu_register.cpp, mounted by main.cpp). The native menu items call
// through the registered pointers. Mirrors the existing setOsFullScreenFn inversion.
//
// Fullscreen is NOT in this seam: app/menu.cpp reaches it directly via platform/window_mode
// (app→platform is legal), same call the F11 keymap handler uses.
//
// Each window toggle has an ACTION (flip the bool) and a STATE getter (current visibility) so a
// future native menu validator can show a check mark. The action is the load-bearing behavior.
#pragma once

namespace sw::app {

// One View action's wiring: flip the toggle + read its current on/off state. state may be null for
// actions with no meaningful checked state (Toggle-All UI, Focus Mode — they are momentary flips).
struct ViewMenuAction {
  void (*toggle)() = nullptr;  // perform the toggle (flip the window/mode)
  bool (*state)() = nullptr;   // current checked state, or null if not a stateful toggle
};

// The View actions the native menu offers. UI fills these at startup via registerViewMenuActions.
// nullptr until registered (callers guard) — keeps app/menu.cpp free of any ui/ include.
enum ViewAction {
  kAssetsWindow = 0,
  kVariationWindow,
  kThemeWindow,
  kToggleAllUi,
  kFocusMode,
  kViewActionCount,
};

// Register the real UI fns (called once at startup from the UI side). Pass the full table.
void registerViewMenuActions(const ViewMenuAction (&actions)[kViewActionCount]);

// Native menu accessor: invoke an action's toggle (no-op if unregistered). Used by menu.cpp.
void invokeViewAction(ViewAction which);

// Native menu accessor: read an action's checked state (false if unregistered or stateless).
bool viewActionState(ViewAction which);

}  // namespace sw::app
