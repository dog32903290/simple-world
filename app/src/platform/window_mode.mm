// platform/window_mode — see window_mode.h.
// Objective-C++ glue for OS-level window fullscreen. Compiled WITHOUT ARC (-fno-objc-arc)
// to match the manual-lifetime convention of dialogs.mm / image_decode.mm / eye.mm.
#import <AppKit/AppKit.h>
#include "platform/window_mode.h"

namespace sw::platform {

void toggleOsFullScreen() {
  // Mirrors TiXL _pWindow->toggleFullScreen(nullptr) (modes.md [polish]).
  // NSApp.mainWindow is always the one NS::Window created in app_delegate.cpp.
  NSWindow* win = [NSApplication sharedApplication].mainWindow;
  if (win) [win toggleFullScreen:nil];
}

}  // namespace sw::platform
