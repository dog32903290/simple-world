// ui/view_modes — P6 演出/Focus view modes (modes.md [core]/[important]).
// Zone: ui. Mirrors TiXL Editor/Gui/Windows/Layouts/UiConfig.cs (FocusMode + ToggleAllUiElements)
// and Player/Program.cs (fullscreen output). Session-only state — like the output pin, "how I'm
// looking", not "what I built"; NEVER serialized into .swproj.
//
// Three related toggles sharing one "hide chrome" underlay:
//   • g_focusMode  (F12     = TiXL ToggleFocusMode):     hide all editor windows AND let the shell
//                                                        blit the live render fullscreen BEHIND the
//                                                        (still-interactive) canvas. In-editor演出.
//   • g_showChrome (Shift+Esc = TiXL ToggleAllUiElements): hide toolbar/inspector/timeline/output/
//                                                        variation windows; canvas always drawn.
//   • Player mode  (--play  = TiXL Player exe):          shell-level fullscreen-only output (no
//                                                        canvas); lives in app/main.cpp (present path).
//
// The handlers are KeyEntry-shaped (bool()) so ui/keymap's data-driven table references them as
// rows (鐵律7) without growing keymap.cpp's body.
#pragma once

namespace sw::ui {

extern bool g_focusMode;   // F12; default false
extern bool g_showChrome;  // Shift+Esc; default true (chrome visible)

// True when the editor windows should draw this frame: chrome shown AND focus mode off.
// The canvas is ALWAYS drawn regardless (it runs the keymap, so F12 can toggle back).
bool editorChromeVisible();

// Toggle helpers. focus on -> save chrome state + hide; focus off -> restore the saved state.
void toggleFocusMode();
void toggleShowChrome();

// KeyEntry-shaped handlers (return true if they fired). Referenced by ui/keymap's kKeyTable.
bool handleToggleFocusMode();      // F12
bool handleToggleAllUiElements();  // Shift+Esc

}  // namespace sw::ui
