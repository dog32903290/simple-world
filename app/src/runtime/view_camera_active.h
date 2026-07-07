// runtime/view_camera_active — the OUTPUT-CAMERA OVERRIDE host scope (phase-B of "Output camera orbit").
//
// PROBLEM this closes: the render path has NO global mutable camera. The DEFAULT output camera
// (EvaluationContext.SetDefaultCamera — eye=(0,0,DefaultCameraDistance), fov=45°) is rebuilt
// function-locally at EVERY consumer that needs the "no Camera-op in scope" fallback, via
// field_camera's defaultLayerCameraForward(aspect). To let the Output window's orbit/zoom/pan
// (a phase-C mouse lane) actually move what you see, those fallbacks must read a mutable ViewCamera
// when one is engaged, and stay byte-identical to the hard-wired default when it is not.
//
// MECHANISM — a VERBATIM copy of the C1 LiveCameraScope pattern (point_ops_camera_scope.h), itself a
// copy of the S3a LiveCtxVarScope pattern: a thread_local "active view-camera override" the cook DRIVER
// SETS around the whole cook (PointGraph::cook / cookResident, right after seedFrameResolution), that
// the fallback sites READ via activeViewCameraForward(aspect). Engaged → toForward(override,aspect);
// UNENGAGED (nullptr, the production default) → defaultLayerCameraForward(aspect) EXACTLY as before,
// byte-identical to every pre-phase-B frame (the parity floor).
//
// PRECEDENCE (the parity-critical seam): this override REPLACES ONLY the DEFAULT camera — the exact
// `defaultLayerCameraForward` call each consumer makes in its "no Camera op in scope" branch (else /
// !hasCamera / cam==nullptr). A wired Camera op still wins: LiveCameraScope (C1) / it.hasCamera /
// cc.hasCamera short-circuit BEFORE the default branch is reached, so the ViewCamera override never
// touches a stamped-Camera item. This mirrors TiXL: OutputWindow's orbit drives EvaluationContext's
// SetDefaultCamera (the ambient camera), which a pushed Camera op overrides for its subtree
// (CameraInteraction.cs writes CameraPosition/Target that SetDefaultCamera reads; a Camera op pushes its
// own WorldToCamera on top). So override < Camera-op, exactly like default < Camera-op.
//
// ★FLAT-RESIDENT MIRROR HARD GATE (the S2c blood lesson, same as C1): the scope MUST be engaged on BOTH
// cook legs (flat cook() + resident cookResident() = production). A resident-only miss = resident
// consumers read the default while the override is set = a prod-only "orbit does nothing" black-hole.
// activeViewCameraForward is SHARED by both legs — not forked; only the scope-engage site is mirrored.
//
// runtime leaf (ARCHITECTURE.md): pure CPU + the view_camera / field_camera math. No UI, no upward deps.
// Its OWN tiny header (NOT the at-cap point_ops.h) so both cook drivers + the fallback ops can include it.
#pragma once

#include "runtime/view_camera.h"  // ViewCamera, toForward; field_camera.h transitively (LayerCameraForward)

namespace sw {

// RAII guard — engage a ViewCamera override for the enclosing cook. Construct once at cook entry (the SAME
// spot seedFrameResolution runs); the whole cook body (every sub-cook: flat/resident buffer, resident value
// cook, command/tex executors) then sees the override through liveViewCameraOverride(). Destroy at cook exit
// pops it back to null (the default). Nests correctly (saves+restores the prior override) — a defensive
// property; production only ever engages one at the top. Constructing from a null pointer (no override this
// frame) engages NOTHING → liveViewCameraOverride() stays null → every fallback is byte-identical to today.
struct ViewCameraScope {
  explicit ViewCameraScope(const ViewCamera* cam);  // nullptr → transparent (no override)
  ~ViewCameraScope();
  ViewCameraScope(const ViewCameraScope&) = delete;
  ViewCameraScope& operator=(const ViewCameraScope&) = delete;
 private:
  const ViewCamera* prev_;
  bool engaged_;
};

// The live OUTPUT-CAMERA override, or nullptr when no override is engaged (the production default). Consulted
// ONLY by activeViewCameraForward — a consumer should call that helper, not read this directly, so the
// default-fallback stays a single codepath.
const ViewCamera* liveViewCameraOverride();

// The DEFAULT output-camera FORWARD pair (WorldToCamera + CameraToClipSpace) for `aspect` — the SINGLE
// replacement for a `defaultLayerCameraForward(aspect)` call at a "no Camera op in scope" fallback site.
//   override engaged → toForward(*override, aspect)   (the mutable orbit camera)
//   no override      → defaultLayerCameraForward(aspect)   (byte-identical to the hard-wired default)
// toForward(makeDefaultViewCamera(), aspect) == defaultLayerCameraForward(aspect) bit-for-bit (the phase-A
// parity tooth, --selftest-view-camera (1)), so engaging an UNMOVED override is ALSO byte-identical — the
// override only diverges once the camera is actually dragged. Same aspect the caller already passed.
LayerCameraForward activeViewCameraForward(float aspect);

// --selftest-view-camera-override entry (view_camera_override_selftest.cpp). Drives the override through the
// scope end-to-end (engage → activeViewCameraForward diverges from default → clear → back to byte-identical),
// biting BOTH the "override takes effect" and the "cleared override does not leak" faces. injectBug leaks a
// stale override past clear (the exact regression that would break every camera golden). PURE MATH, zero GPU.
int runViewCameraOverrideSelfTest(bool injectBug);

}  // namespace sw
