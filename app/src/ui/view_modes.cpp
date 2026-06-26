// ui/view_modes — see view_modes.h. P6 演出/Focus mode state + toggles + keymap handlers.
#include "ui/view_modes.h"

#include "imgui.h"

#include "app/document.h"  // sw::doc::g_status (status-line text)

namespace sw::ui {

bool g_focusMode = false;
bool g_showChrome = true;

// Leaf-inversion seam: main.cpp registers the platform fn at startup so this ui-zone file
// never includes platform/. nullptr until wired (safe: handleOsFullScreen guards).
static void (*s_osFullScreenFn)() = nullptr;

void setOsFullScreenFn(void (*fn)()) { s_osFullScreenFn = fn; }

// Chrome-visible state captured when focus mode was entered, restored on exit (TiXL UiConfig.cs
// _uiStateBeforeFocusMode). F12 -> hide -> F12 returns you to exactly what you had, even if you
// separately flipped chrome with Shift+Esc meanwhile.
static bool s_showChromeBeforeFocus = true;

bool editorChromeVisible() { return g_showChrome && !g_focusMode; }

void toggleFocusMode() {
  if (!g_focusMode) {
    s_showChromeBeforeFocus = g_showChrome;  // remember current chrome state
    g_focusMode = true;
    g_showChrome = false;  // focus forces chrome hidden (editorChromeVisible double-guards anyway)
  } else {
    g_focusMode = false;
    g_showChrome = s_showChromeBeforeFocus;  // restore what was showing before
  }
}

void toggleShowChrome() {
  // Independent of focus mode (TiXL ToggleAllUiElements). If focus is on, this still flips the
  // underlying chrome bit so leaving focus restores the user's last explicit choice.
  g_showChrome = !g_showChrome;
}

// ---------------------------------------------------------------------------
// Keymap handlers (= TiXL FactoryKeyMap.cs:84 ToggleFocusMode / :82 ToggleAllUiElements).
// Both are GLOBAL actions (no NeedsWindowFocus in FactoryKeyMap); ui/keymap's processFrame already
// gates them behind !io.WantTextInput, so text fields are not stolen.
// ---------------------------------------------------------------------------
bool handleToggleFocusMode() {
  // Bare F12, no modifiers (FactoryKeyMap.cs:84 KeyCombination(Key.F12)); exclude any modifier.
  const ImGuiIO& io = ImGui::GetIO();
  if (io.KeyCtrl || io.KeyAlt || io.KeyShift) return false;
  if (!ImGui::IsKeyPressed(ImGuiKey_F12, false)) return false;
  toggleFocusMode();
  sw::doc::g_status = g_focusMode ? "focus mode" : "editor";
  return true;
}

bool handleToggleAllUiElements() {
  // Shift+Esc (FactoryKeyMap.cs:82). Bare Esc is a gesture-cancel elsewhere (annotation/quick_add),
  // so require Shift and exclude Ctrl/Alt.
  const ImGuiIO& io = ImGui::GetIO();
  if (!io.KeyShift || io.KeyCtrl || io.KeyAlt) return false;
  if (!ImGui::IsKeyPressed(ImGuiKey_Escape, false)) return false;
  toggleShowChrome();
  sw::doc::g_status = g_showChrome ? "UI shown" : "UI hidden";
  return true;
}

bool handleOsFullScreen() {
  // Bare F11, no modifiers (modes.md [polish]; TiXL _pWindow->toggleFullScreen).
  // Platform fn is wired by main.cpp via setOsFullScreenFn (leaf-inversion seam).
  const ImGuiIO& io = ImGui::GetIO();
  if (io.KeyCtrl || io.KeyAlt || io.KeyShift) return false;
  if (!ImGui::IsKeyPressed(ImGuiKey_F11, false)) return false;
  if (s_osFullScreenFn) s_osFullScreenFn();
  return true;
}

}  // namespace sw::ui
