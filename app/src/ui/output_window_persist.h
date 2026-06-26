#pragma once
// ui/output_window_persist — the bridge between the Output window's SESSION-ONLY view globals (pin /
// resolution / background) and the app-zone OutputWindowStore that document.cpp saves/loads per
// project (out-window-persistence). Split out of output_window.cpp (the coordinator) to keep that
// file under its line-count ratchet; this TU is the ONLY producer/consumer of the store, so the
// dependency stays clean (ui -> app). Faithful to TiXL OutputWindow.SaveStateTo / LoadStateFrom.
namespace sw::ui {

// Mirror the live view globals INTO the app store (called every frame; cheap value copies). The
// caller passes its current globals; the store is what the next Save writes to the project sidecar.
// `bg` is the RGBA Command-view background color (output_window.cpp g_viewBackground).
void captureOutputWindowState(int pinnedNode, int selectedResIndex, const float bg[4]);

// If document.cpp armed the restore latch (project open / new), push the restored store state back
// into the globals and return true (the caller writes the out-params back to its globals and the
// resolution override is already re-applied here). Returns false (no writes) when nothing is pending.
// `pinnedNode` is validated against the current symbol (a dangling saved id restores as 0 = unpinned).
bool restoreOutputWindowStateIfPending(int& pinnedNode, int& selectedResIndex, float bg[4]);

}  // namespace sw::ui
