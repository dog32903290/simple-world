// ui/menu_bar — the imgui top menu bar (ImGui::BeginMainMenuBar), TiXL-shaped.
// Zone: ui. Depends on app(document/command) + ui(view_modes / tool-window toggles) + verify(thin hook).
//
// = TiXL Editor/Gui/AppMenuBar.cs top-level order (File / Edit / View). TiXL is an imgui app
// with NO native menu, so 照TiXL gives us this imgui bar. The macOS native NSMenu (app/menu.cpp)
// is a SEPARATE concern TiXL does not cover; see the fork note in menu.cpp.
//
// Data-driven (鐵律7): one row per item in a local table, one builder turns a table into a menu.
// Adding an item = adding one row. Mirrors menu.cpp's template-array buildMenu pattern, but emits
// imgui MenuItems (which DO get screen rects → the eye/hand can reach them) instead of NSMenu rows.
//
// CRITICAL — imgui MenuItem shortcut strings are DISPLAY-ONLY: they do NOT register key handlers.
// The real key bindings stay where they already live, untouched:
//   • Cmd+N/O/S/Shift+S  → NSMenu File key-equivalents (app/menu.cpp; the ONLY binding for these).
//   • Cmd+Z / Cmd+Shift+Z → imgui canvas (editor_ui_canvas_edit.cpp; independent of any menu).
// The shortcut text below is cosmetic parity only.
#include "ui/editor_ui.h"

#include "ui/asset_browser.h"    // assetBrowserVisible (View toggle: a per-window visibility bool)
#include "ui/theme_editor.h"     // themeEditorVisible  (View toggle)
#include "ui/variation_panel.h"  // variationPanelVisible (View toggle)
#include "ui/view_modes.h"       // g_showChrome / toggleShowChrome / toggleFocusMode + fullscreen seam

#include <string>

#include "imgui.h"

#include "app/command.h"   // g_commands undo/redo + canUndo/canRedo + lastUndoName/lastRedoName
#include "app/document.h"  // doNew/doOpen/doSave/doSaveAs + g_relayout/g_status
#include "verify/eye/eye.h"

namespace sw::ui {

// Platform fullscreen reuses the same leaf-inversion seam view_modes already owns (setOsFullScreenFn
// is wired by main.cpp). handleOsFullScreen() calls it on F11; the View item triggers the same path.
// We expose a tiny local trampoline so the menu table can name a void() action.
namespace {

void doFullscreen() {
  // Reuse the F11 handler's wired platform fn. It is gated on the F11 key, so we cannot call it
  // directly; instead view_modes stores the fn behind setOsFullScreenFn. We expose it through a
  // dedicated menu trampoline that view_modes provides.
  sw::ui::menuOsFullScreen();
}

// One menu leaf as data: title, display-only shortcut, an optional bound toggle bool, and an
// optional action fn. Exactly one of {toggle, action} is set per row (toggle rows render with a
// check mark; action rows render plain). cbName is unused at runtime (kept for symmetry / debug).
struct ImMenuItem {
  const char* title;
  const char* shortcut;  // DISPLAY ONLY (see file header) — never registers a handler
  bool* toggle;          // non-null → checkbox MenuItem bound to this bool
  void (*action)();      // non-null → plain MenuItem invoking this
};

// Emit one item; record its rect under the given eye key so the hand can reach it.
void drawItem(const char* menuName, const ImMenuItem& it, bool enabled) {
  if (it.toggle) {
    ImGui::MenuItem(it.title, it.shortcut, it.toggle, enabled);
  } else if (ImGui::MenuItem(it.title, it.shortcut, false, enabled) && it.action) {
    it.action();
  }
  // eye key: "menubar:<Menu>:<title>" — leaf row rect (drawn only while the menu is open).
  std::string key = std::string("menubar:") + menuName + ":" + it.title;
  sw::eye::recordItem(key.c_str());
}

// Build one top-level menu from a table. Template recovers N so callers omit the count, exactly
// like menu.cpp::buildMenu. enabledFn lets a row gate itself (Undo greys when the stack is empty).
template <unsigned long N>
void buildImMenu(const char* name, const ImMenuItem (&items)[N], bool (*enabledFn)(const ImMenuItem&) = nullptr) {
  if (ImGui::BeginMenu(name)) {
    for (unsigned long i = 0; i < N; ++i)
      drawItem(name, items[i], enabledFn ? enabledFn(items[i]) : true);
    ImGui::EndMenu();
  }
  // eye key for the top-level HEADER itself (always present on the bar): "menubar:<Menu>".
  std::string headerKey = std::string("menubar:") + name;
  sw::eye::recordItem(headerKey.c_str());
}

// ---- The menu tables. Add a row to grow a menu (鐵律7). --------------------------------------

// File (= AppMenuBar.cs File group). Actions reuse the SAME sw::doc fns the floating Toolbar
// and the NSMenu use — no duplicated logic. Shortcuts are display-only; the real Cmd+N/O/S still
// fire through the NSMenu key-equivalents (kept alive on purpose, see menu.cpp fork note).
const ImMenuItem kFile[] = {
    {"New", "Cmd+N", nullptr, [] { sw::doc::doNew(); }},
    {"Open...", "Cmd+O", nullptr, [] { sw::doc::doOpen(); }},
    {"Save", "Cmd+S", nullptr, [] { sw::doc::doSave(); }},
    {"Save As...", "Cmd+Shift+S", nullptr, [] { sw::doc::doSaveAs(); }},
};

// Edit (= AppMenuBar.cs Edit group: Undo/Redo). Reuses the EXACT g_commands path the canvas
// Cmd+Z and the right-click context menu use. Rows gate themselves via canUndo/canRedo (greyed
// when the stack is empty), matching TiXL CanUndo/CanRedo gating.
const ImMenuItem kEdit[] = {
    {"Undo", "Cmd+Z", nullptr,
     [] { sw::g_commands.undo(); sw::doc::g_status = "undo"; sw::doc::g_relayout = true; }},
    {"Redo", "Cmd+Shift+Z", nullptr,
     [] { sw::g_commands.redo(); sw::doc::g_status = "redo"; sw::doc::g_relayout = true; }},
};

// View (= AppMenuBar.cs View group: per-window visibility toggles + Toggle-All + Fullscreen +
// Focus Mode). TiXL's five toggles are bound to its persisted window flags; sw binds to its OWN
// real tool-window visibility bools (the windows sw actually has). Toggle-All = toggleShowChrome
// (TiXL ToggleAllUiElements), Fullscreen = the OS fullscreen seam, Focus = toggleFocusMode.
const ImMenuItem kView[] = {
    {"Assets Window", nullptr, &sw::ui::assetBrowserVisible(), nullptr},
    {"Variation Window", nullptr, &sw::ui::variationPanelVisible(), nullptr},
    {"Theme Window", nullptr, &sw::ui::themeEditorVisible(), nullptr},
    {"Toggle All UI", "Shift+Esc", nullptr, [] { sw::ui::toggleShowChrome(); }},
    {"Fullscreen", "F11", nullptr, [] { doFullscreen(); }},
    {"Focus Mode", "F12", nullptr, [] { sw::ui::toggleFocusMode(); }},
};

bool editEnabled(const ImMenuItem& it) {
  // "Undo" enabled iff canUndo; "Redo" enabled iff canRedo. Title compare keeps the table flat.
  if (std::string(it.title) == "Undo") return sw::g_commands.canUndo();
  if (std::string(it.title) == "Redo") return sw::g_commands.canRedo();
  return true;
}

}  // namespace

void drawMenuBar() {
  if (ImGui::BeginMainMenuBar()) {
    buildImMenu("File", kFile);
    buildImMenu("Edit", kEdit, editEnabled);
    buildImMenu("View", kView);
    ImGui::EndMainMenuBar();
  }
}

}  // namespace sw::ui
