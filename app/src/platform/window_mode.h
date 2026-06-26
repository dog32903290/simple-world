// platform/window_mode — OS-level window mode (fullscreen toggle).
// Zone: platform. macOS only: wraps NSWindow toggleFullScreen: — a pure AppKit call that
// belongs at the platform boundary, not in ui/ or app/ (depends on AppKit internals).
// TiXL parity: TiXL Editor toggles via _pWindow->toggleFullScreen(nullptr) (modes.md [polish]).
#pragma once

namespace sw::platform {

// Ask the OS to toggle the main application window between normal and fullscreen.
// On macOS this calls [NSApp.mainWindow toggleFullScreen:nil], which triggers the
// native Space animation. Called by ui/keymap's F11 handler + the menu "Fullscreen" item.
void toggleOsFullScreen();

}  // namespace sw::platform
