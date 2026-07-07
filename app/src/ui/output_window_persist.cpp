// ui/output_window_persist — see header. Bridges the Output window's session view globals to the
// app-zone OutputWindowStore (the per-project sidecar document.cpp persists). Zone: ui (reads app +
// runtime). Faithful to TiXL OutputWindow.SaveStateTo / LoadStateFrom (OutputWindow.cs:588-624).
#include "ui/output_window_persist.h"

#include "app/document.h"             // currentSymbolConst (validate a restored pin id)
#include "app/output_window_state.h"  // the app-zone store + restore latch + OutputWindowState
#include "runtime/compound_graph.h"   // childById (does the saved pin still exist?)
#include "ui/output_window_orbit.h"   // Viewer camera capture/restore (the phase-C session ViewCamera)
#include "ui/output_window_resolution.h"  // kResPresets / resolutionIndexForTitle / applyResolutionSelection

// The Fill-baseline window size (TiXL GetWindowSize role), defined in main.cpp (shell owns g_pointGraph).
namespace sw {
bool outputWindowResolution(int& w, int& h);
}  // namespace sw

namespace sw::ui {

void captureOutputWindowState(int pinnedNode, int selectedResIndex, const float bg[4]) {
  settings::OutputWindowState s;
  s.isPinned   = pinnedNode != 0;             // TiXL Pinning.SaveStateTo: IsPinned + PinnedInstancePath
  s.pinnedNode = pinnedNode;                  // sw pins by child id (not a Guid path)
  const ResPreset& p = kResPresets[selectedResIndex];
  s.resolutionTitle = p.title;                // TiXL ResolutionTitle (restored via FindByTitle)
  s.resolutionWidth = p.w;
  s.resolutionHeight = p.h;
  s.resolutionUseAsAspectRatio = p.useAsAspectRatio;
  for (int i = 0; i < 4; ++i) s.backgroundColor[i] = bg[i];
  // Viewer camera (TiXL CameraSelectionHandling.SaveStateTo cs:410-412): mirror the live session
  // ViewCamera's pose so the next Save writes it. Locked-to-Op's camera is node params (in the .swproj
  // graph already), not this — captureViewerCamera reads only the Viewer-mode session camera.
  captureViewerCamera(s.cameraPosition, s.cameraTarget, s.cameraRoll);
  settings::outputWindowStore().setState(s);
}

bool restoreOutputWindowStateIfPending(int& pinnedNode, int& selectedResIndex, float bg[4]) {
  if (!settings::outputWindowStateRestorePending()) return false;
  settings::outputWindowStateRestorePending() = false;
  const settings::OutputWindowState& s = settings::outputWindowStore().state();

  // Pin (TiXL Pinning.LoadStateFrom). Restore only when the saved pinned child still exists in the
  // current symbol — a dangling id would be cleared by the stale-pin guard anyway, but restoring it
  // cleanly here keeps the intent explicit.
  const sw::Symbol* cur = sw::doc::currentSymbolConst();
  pinnedNode = (s.isPinned && s.pinnedNode != 0 && cur && sw::childById(*cur, s.pinnedNode))
                   ? s.pinnedNode : 0;

  // Resolution (TiXL: FindByTitle, else Custom -> we Fill). Write the selection global FIRST, because
  // applyResolutionSelection reads g_selectedResIndex (not a param), then drive the cook override.
  selectedResIndex = resolutionIndexForTitle(s.resolutionTitle.c_str());
  g_selectedResIndex = selectedResIndex;
  int winW = 0, winH = 0;
  sw::outputWindowResolution(winW, winH);
  applyResolutionSelection(winW, winH);

  // Background color (TiXL: _backgroundColor). The per-frame ColorEdit re-engages it on a Command view.
  for (int i = 0; i < 4; ++i) bg[i] = s.backgroundColor[i];

  // Viewer camera (TiXL CameraSelectionHandling.LoadStateFrom cs:415-428): push the restored pose into the
  // live session ViewCamera AND re-engage the cook override so the reopened project shows the same view.
  // A default pose is byte-identical to no override (phase-A parity floor), so an un-orbited project is a
  // no-op restore — divergence only for a project that was actually orbited + saved.
  restoreViewerCamera(s.cameraPosition, s.cameraTarget, s.cameraRoll);
  return true;
}

}  // namespace sw::ui
