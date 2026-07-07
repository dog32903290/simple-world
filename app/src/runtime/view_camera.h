// runtime/view_camera — PURE-DATA + PURE-MATH mutable output camera (eye/target/roll) and the
// mouse-drag orbit/zoom/pan math. ZONE: runtime (pure computation). NO Metal / platform include —
// plain float[3]/float math so the whole interaction model is deterministically testable BEFORE any
// GPU / UI / seam is wired. This is the phase-A load-bearing LEAF for "Output camera orbit": any
// orbit UI (a later lane) drives THIS struct, and the default value here is byte-identical to the
// hard-wired default camera the render path already uses, so an override-absent output stays parity.
//
// PARITY AUTHORITY: external/tixl/Editor/Gui/Interaction/Camera/CameraInteraction.cs
//   - orbit  : ApplyOrbitVelocity (the pure distance-preserving 3D branch, CameraInteraction.cs:282-290
//              + the axis-angle rot pair cs:265-267): a rotated view direction, |Position-Target| held.
//   - zoom   : HandleMouseWheel (the plain non-CameraSpeed branch cs:216-230): scale |Position-Target|
//              by (1 ± ZoomSpeed·frameFactor).
//   - pan    : Pan (cs:308-327): translate BOTH eye+target along the view Left/Up axes.
//   - view axes : ViewAxis.ComputeForCamera (cs:401-410): Left/Up derived from (Target-Position) and a
//                 roll-rolled world-up, so orbit rotates around the camera's own screen axes.
// Interaction CONSTANTS from external/tixl/Editor/Gui/Interaction/Camera/CameraInteractionParameters.cs.
//
// CONVENTION: this file OWNS no matrices — toForward() delegates ALL matrix construction to
// field_camera.h (lookAtRH / perspectiveFovRH), so the ROW-MAJOR / row-vector convention and the
// LookAtRH/PerspectiveFovRH element order are the SAME single source of truth. toForward() of the
// DEFAULT ViewCamera is required to equal defaultLayerCameraForward(aspect) bit-for-bit (the parity
// tooth) — that is why the default eye/target/fov/near/far below are copied from EvaluationContext.
// SetDefaultCamera, exactly as field_camera.cpp's defaultLayerCameraForward does.
#pragma once

#include "runtime/field_camera.h"  // LayerCameraForward, lookAtRH, perspectiveFovRH, defaultCameraDistance

namespace sw {

// A mutable output-window camera. eye = TiXL CameraPosition, target = CameraTarget, roll = CameraRoll.
// Defaults are EvaluationContext.SetDefaultCamera (CameraInteraction.ResetCamera cs:413-418):
//   eye=(0,0,DefaultCameraDistance), target=(0,0,0), roll=0. These MUST match defaultLayerCameraForward
//   so an untouched ViewCamera renders byte-identical to the hard-wired default (the parity tooth).
struct ViewCamera {
  float eye[3]{0.0f, 0.0f, 0.0f};   // set to (0,0,DefaultCameraDistance) by makeDefaultViewCamera()
  float target[3]{0.0f, 0.0f, 0.0f};
  float roll{0.0f};                 // v1 fork: orbit/zoom/pan never change roll (TiXL orbit doesn't either)
};

// Build a ViewCamera at the TiXL default pose (eye on +z at DefaultCameraDistance). A plain aggregate
// literal can't call defaultCameraDistance() at compile time, so the default distance is filled here.
ViewCamera makeDefaultViewCamera();

// The camera's screen axes, derived from (target-eye) and a roll-rolled world-up — a faithful port of
// CameraInteraction.ViewAxis.ComputeForCamera (cs:401-410). Left/Up are the axes orbit rotates around
// and pan slides along. viewDistance = target-eye (NOT normalized, like TiXL's ViewDistance).
struct ViewAxes {
  float left[3];
  float up[3];
  float viewDistance[3];  // target - eye (un-normalized)
};
ViewAxes computeViewAxes(const ViewCamera& cam);

// Orbit: rotate `eye` around `target` by a drag (dx,dy in *pixels*). Faithful to CameraInteraction:
// the pixel delta becomes an orbit velocity `orbit = (dx, -dy) · kOrbitSensitivity` (cs:158-163, the
// MouseDelta.Y sign flip + the -0.1f constant), then ApplyOrbitVelocity's pure-3D branch rotates the
// view direction: rotAroundX=CreateFromAxisAngle(Left, orbit.y), rotAroundY=CreateFromAxisAngle(Up,
// orbit.x), rot=rotAroundX·rotAroundY, newViewDir=Transform(viewDir,rot), then
//   eye = target - normalize(newViewDir)·|target-eye|   (distance preserved). roll untouched.
void applyOrbitDrag(ViewCamera& cam, float dx, float dy);

// Zoom: scale the eye→target distance by one mouse-wheel notch (>0 = zoom in / closer). Faithful to
// HandleMouseWheel's non-CameraSpeed branch (cs:216-230): factor = 1 + ZoomSpeed·frameFactor;
// wheel<0 → viewDistance·=factor (further), wheel>0 → viewDistance/=factor (closer);
//   eye = target + viewDistance. `frameFactor` mirrors TiXL's per-frame ImGui.DeltaTime scaling; the
// caller passes the frame's dt (default 1/60 in the header for a UI-less call). target/roll untouched.
void applyZoom(ViewCamera& cam, float wheel, float frameFactor = 1.0f / 60.0f);

// Pan: translate BOTH eye and target along the view Left/Up axes by a drag (dx,dy in pixels). Faithful
// to Pan (cs:308-327): factorX=dx/RenderWindowHeight, factorY=dy/RenderWindowHeight,
//   length=|target-eye|;  delta = Left·factorX·length + Up·factorY·length;  eye+=delta; target+=delta.
// (TiXL multiplies length by UserSettings.CameraSpeed; sw has no per-user CameraSpeed yet → treated as
//  1.0, a NAMED FORK. Dropping it only rescales pan speed, never the geometry.)
void applyPan(ViewCamera& cam, float dx, float dy);

// The FORWARD camera pair (WorldToCamera + CameraToClipSpace) this camera projects with — the SAME
// return type + math as field_camera's defaultLayerCameraForward, but reading THIS camera's eye/target
// (up derived from roll via computeViewAxes) instead of the hard-wired default. fov/near/far are the
// SetDefaultCamera constants (45°, 0.01, 1000) so the DEFAULT ViewCamera reproduces
// defaultLayerCameraForward(aspect) bit-for-bit. `aspect` = output width/height.
LayerCameraForward toForward(const ViewCamera& cam, float aspect);

// --selftest-view-camera entry (view_camera_selftest.cpp). PURE MATH, zero GPU.
int runViewCameraSelfTest(bool injectBug);

}  // namespace sw
