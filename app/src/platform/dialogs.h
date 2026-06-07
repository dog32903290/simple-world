#pragma once
#include <string>

namespace sw {

// Synchronous "you have unsaved changes" prompt. Three buttons, in this order:
// Save / Don't Save / Cancel. Synchronous (NSAlert runModal) so it can be used
// inside terminate/close menu callbacks where an async ImGui modal cannot.
enum class UnsavedChoice { Save, DontSave, Cancel };
UnsavedChoice askUnsaved();

// Single-button error alert (e.g. load/save failure). Synchronous.
void showError(const std::string& message);

// Install a native NSWindowDelegate on the given NSWindow* so the red close
// button is intercepted. shouldClose() runs synchronously when the user clicks
// close; returning false keeps the window open (used to run the unsaved guard).
// metal-cpp does not expose NSWindowDelegate, so this lives in Obj-C++.
void installCloseGuard(void* nsWindow, bool (*shouldClose)());

}  // namespace sw
