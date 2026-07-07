#pragma once
// ui/output_window_orbit — the Output window's 3D mouse-orbit gesture (phase-C of "Output camera orbit").
// Split out of output_window.cpp (the coordinator, at its line-count ratchet) so the orbit math + the two
// interaction modes (Auto → Viewer / Locked-to-Op) live in their own TU. Zone: ui (drives runtime
// view_camera math + the app-zone undo command; reads the shell's setOutputViewCamera seam). Never draws.
//
// PARITY AUTHORITY: external/tixl/Editor/Gui/Windows/Output/CameraSelectionHandling.cs (the Auto/Viewer/
// Locked-to-Op mode semantics) + Editor/Gui/Interaction/Camera/CameraInteraction.cs (the left-drag=orbit /
// wheel=zoom gesture, cs:146-165 + HandleMouseWheel). The orbit/zoom MATH itself is runtime/view_camera.h
// (already ported + parity-toothed in phase-A); this file only ROUTES the gesture to the right target.
//
// TWO MODES under a single "Auto" (柏為 wants one mode covering both usages — CameraSelectionHandling.cs
// AutoUseFirstCam, cs:165-198):
//   • Locked-to-Op : a Camera / OrbitCamera node is SELECTED → the orbit gesture writes that node's
//     parameters back through the UNDOABLE SetOverrideCommand (one drag = one undo, exactly like the
//     inspector jog). cs:169-186 `isCamOpSelected → cameraForManipulation = _firstCamInGraph`.
//   • Viewer : no Camera node selected → the orbit gesture drives a SESSION ViewCamera, pushed to the cook
//     via sw::setOutputViewCamera (the phase-B seam). cs:175/193 `cameraForManipulation = _outputWindowViewCamera`.
//
// VIEW-TYPE GATE (CameraSelectionHandling.PreventImageCanvasInteraction, cs:83/141/184): a 3D view
// (outType "Command" | "Points") takes the orbit gesture; a "Texture2D" view keeps the 2D image-canvas
// pan/zoom. The coordinator decides which by outType and calls handleOutputOrbit() only for a 3D view.

#include <string>

namespace sw { struct SymbolChild; struct Symbol; }

namespace sw::ui {

// The live SESSION Viewer camera (Viewer-mode orbit state). Persisted per project via
// output_window_persist (out-window-persistence, the ViewCamera fields). Reset by resetOutputViewToDefault().
// Exposed so the persistence bridge can capture/restore its eye/target/roll.
struct ViewerOrbitState;               // opaque (holds a runtime::ViewCamera); defined in the .cpp
ViewerOrbitState& viewerOrbitState();  // process-wide (single Output window today)

// Read/write the Viewer camera's 3 floats-per-vec for persistence (out-params filled from the live state,
// or pushed into it). eye[3] / target[3] / roll. captureViewer reads; restoreViewer writes + re-pushes the
// override to the cook so a reopened project resumes the same view.
void captureViewerCamera(float eye[3], float target[3], float& roll);
void restoreViewerCamera(const float eye[3], const float target[3], float roll);

// Is `viewNode` a Camera / OrbitCamera op (→ Locked-to-Op)? nullptr / any other node → false (→ Viewer).
// The single predicate the coordinator + this module agree on (symbolId == "Camera" || "OrbitCamera").
bool isCameraOpNode(const sw::SymbolChild* viewNode);

// Handle THIS FRAME's 3D orbit/zoom gesture over the Output viewport. Called by the coordinator ONLY for a
// 3D view (outType Command/Points), after it has drawn the InvisibleButton that captures the drag. `active`
// = the button IsItemActive() (a live left-drag), `hovered` = IsItemHovered() (wheel target). `parent` =
// the symbol containing `viewNode` (doc::currentSymbolConst); `viewNode` = the pinned/selected node the
// viewport shows (may be null → Viewer / terminal). Routes to Locked-to-Op (node param write-back, undoable)
// when viewNode is a Camera op, else Viewer (session ViewCamera → setOutputViewCamera). Returns true if the
// camera moved this frame (the coordinator can mark the frame dirty; today only informational).
bool handleOutputOrbit(const sw::Symbol* parent, const sw::SymbolChild* viewNode, bool active, bool hovered);

// Reset View (toolbar button). Viewer mode: clear the cook override + reset the session ViewCamera to the
// TiXL default pose (CameraInteraction.ResetCamera, cs:413-418). Locked-to-Op: the camera is node params —
// a reset there belongs to the node's own "reset to default" (dossier follow-up), so this only touches the
// Viewer camera. Safe to call every frame the button is pressed.
void resetOutputViewToDefault();

// --selftest-output-orbit entry (output_window_orbit_selftest.cpp). PURE LOGIC, zero GPU / imgui: drives the
// gesture→param math for BOTH modes (Viewer ViewCamera delta; Locked-to-Op Camera/OrbitCamera param deltas +
// the SetOverrideCommand do/undo round-trip). injectBug breaks the undo (leaves a residue).
int runOutputOrbitSelfTest(bool injectBug);

}  // namespace sw::ui
