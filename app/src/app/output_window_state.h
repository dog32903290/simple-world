// app/output_window_state — persisted Output-window view state (pin / resolution / background).
// Zone: app (product behavior: cross-session memory). Pure data + crude_json round-trip; no imgui,
// no Metal, no graph — so the save→reload golden runs headless.
//
// = TiXL Editor/Gui/Windows/Output/OutputWindowState.cs (the per-symbol UI state TiXL serializes
//   into the .t3ui Settings block as the "OutputWindows" array, via SymbolUiJson.WriteAllToJson /
//   ReadAllFromJson). TiXL gathers it with OutputWindow.SaveStateTo / LoadStateFrom (OutputWindow.cs
//   :588-624) and ViewSelectionPinning.SaveStateTo / LoadStateFrom (ViewSelectionPinning.cs:299-314).
//
// What this gap (out-window-persistence, render-output-page OUT-09) makes durable: the Output
// window's *view* state — "what I'm looking at" (the pin), the requested resolution, and the
// Command-view background color — which sw has so far kept SESSION-ONLY (editor_ui.h g_pinnedNode,
// output_window_resolution g_selectedResIndex, output_window.cpp g_viewBackground). Persisting it
// per-project lets a reopened .swproj resume the same view, exactly like TiXL.
//
// FORK (named) "per-project sidecar, not inside the .swproj graph file":
//   - Truly better or just different? Faithful-to-TiXL AND cleaner: TiXL keeps this in the .t3ui UI
//     file, SEPARATE from the .t3 graph file (the SymbolUi vs Symbol split). sw's analogue is a
//     sidecar JSON next to the project ("<project>.swproj.ui"), keeping view state OUT of the
//     cook-core graph serialization (compound_save / libToJsonV2). This matches TiXL's own UI/graph
//     split and avoids reaching into cook-core for a pure-UI setting.
//   - Break downstream parity? No. The .swproj bytes are untouched (libToJsonV2 unchanged), so the
//     dirty-snapshot, golden round-trips, and graph parity all stay byte-identical. A missing sidecar
//     loads as Defaults (no-op), so old projects open exactly as before.
//
// SCOPED-OUT vs TiXL (documented, schema forward-compatible): camera (position/target/roll/speed)
//   and gizmo (ShowGizmos / TransformGizmoMode) fields. sw's Output window holds no camera/gizmo
//   session state to persist yet — field_camera RequestedResolution-coupled modes are DEFERRED
//   (render-output-page OUT-01), and there is no output-window gizmo. The JSON is an object with a
//   version, so those keys can be added later with no migration (same posture as user_settings.h).
#pragma once

#include <string>
#include <vector>

namespace sw::settings {

// One persisted Output-window instance's view state. Mirrors TiXL OutputWindowState's relevant
// fields. Defaults match TiXL's `Defaults` (OutputWindowState.cs:18-48): not pinned, Fill
// resolution, 0.1 grey background. A default-constructed value is the "nothing saved" state.
struct OutputWindowState {
  // Pinning (TiXL OutputWindowState.IsPinned / PinnedInstancePath). sw pins by the compound CHILD
  // id (int) within the current symbol — not a Guid path — because g_pinnedNode is a single child
  // id (editor_ui.h). isPinned==false => follow selection (the cook default).
  bool isPinned = false;
  int  pinnedNode = 0;  // the child id g_pinnedNode held (0 == not pinned)

  // Resolution (TiXL OutputWindowState.ResolutionTitle/Width/Height/UseAsAspectRatio). We persist
  // the preset TITLE ("Fill" | "1080p" | ...) and restore by matching it against the preset table
  // (TiXL LoadStateFrom: FindByTitle first). Width/Height/useAsAspectRatio are kept for a faithful
  // round-trip and as the fallback when a title no longer exists (TiXL's `?? new Resolution(...)`).
  std::string resolutionTitle = "Fill";
  int  resolutionWidth = 0;
  int  resolutionHeight = 0;
  bool resolutionUseAsAspectRatio = true;

  // Command-view background color RGBA (TiXL OutputWindowState.BackgroundColor, default 0.1 grey).
  float backgroundColor[4] = {0.1f, 0.1f, 0.1f, 1.0f};

  bool operator==(const OutputWindowState& o) const;
  bool operator!=(const OutputWindowState& o) const { return !(*this == o); }
};

// The persisted collection (TiXL serializes a LIST under "OutputWindows" — one per OutputWindow
// instance). sw has a single Output window today (out-multi-window is a separate polish gap), so the
// store holds one state at index 0; the array shape is kept so multi-window lands without migration.
class OutputWindowStore {
 public:
  // The (single, today) persisted state. Reading is always safe — defaults until something is set.
  const OutputWindowState& state() const { return states_.empty() ? kDefault_ : states_.front(); }
  void setState(const OutputWindowState& s);

  // ---- JSON round-trip (crude_json) — pure, no disk so the selftest round-trips headless. --------
  // Schema: { "version": 1, "OutputWindows": [ {state...}, ... ] } — the "OutputWindows" array name
  // is TiXL's exact key (OutputWindowState.cs:79), so the shape reads familiar to a TiXL eye.
  std::string toJson() const;
  bool        fromJson(const std::string& json);  // false on parse failure (store left empty)

  // ---- Disk persistence (per-project sidecar JSON; mirrors user_settings::save/load) -------------
  bool save(const std::string& path) const;
  bool load(const std::string& path);  // missing/empty file => empty store (=> Defaults), true

 private:
  static const OutputWindowState kDefault_;
  std::vector<OutputWindowState> states_;
};

// The sidecar path for a project: "<project>.swproj" -> "<project>.swproj.ui". Empty in -> empty out
// (an unsaved/anon project has no sidecar to write).
std::string outputWindowStatePathFor(const std::string& projectPath);

// ---- The live process store (app-owned; the ui layer is the only producer/consumer) -------------
// Dependency direction stays clean (ui -> app): the Output window (ui) CAPTURES its session globals
// (pin/resolution/background) into this store each frame and RESTORES from it on project-load; the
// document layer (app) only saves/loads the store to/from the project sidecar — it never reaches up
// into the ui zone. Mirrors user_settings::settings() (process-wide Meyers singleton).
OutputWindowStore& outputWindowStore();

// Set true when a project's sidecar was just loaded, so the ui pushes the restored state into its
// session globals on the next frame (and clears the latch). Driven by document::doOpenPath.
bool& outputWindowStateRestorePending();

// Document hooks (app zone, called from document.cpp): persist the current store to the project
// sidecar on save; load the sidecar into the store on open (arming the restore latch). Missing
// sidecar on open => Defaults armed (so a project with no saved view state restores to TiXL defaults,
// not stale globals from the previously-open project).
void saveOutputWindowStateFor(const std::string& projectPath);
void loadOutputWindowStateFor(const std::string& projectPath);

// doNew hook: a fresh project has no saved view state — reset the store to Defaults and arm restore
// so the Output window drops the prior project's pin/resolution/background.
void resetOutputWindowStateToDefaults();

}  // namespace sw::settings
