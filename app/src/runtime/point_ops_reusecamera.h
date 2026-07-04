// runtime/point_ops_reusecamera — ReuseCamera command op (camera-B lane).
// TiXL authority: external/tixl/Operators/Lib/render/camera/ReuseCamera.cs (GUID 484bec1b) +
// ReuseCamera.t3.
//
// ReuseCamera.cs:14-47 Update: renders its Command subtree through ANOTHER camera instance's matrices —
//   var obj = CameraReference.GetValue(context);          // Slot<Object> → an ICamera provider
//   if (obj == null)            { Log.Warning(...); return; }   // :17-21 subtree NOT evaluated
//   if (obj is not ICamera cam) { Log.Warning(...); return; }   // :25-29 same
//   push context.WorldToCamera/CameraToClipSpace = cam's; Command.GetValue(context); restore.  // :31-41
//
// SW seam: the cook DRIVER resolves the "Object" wire's SOURCE node into raw camera params
// (resolveReferencedCamera — ONE shared helper on BOTH cook legs) and hands them on CmdCookCtx
// (hasRefCamera + refCam*); cookReuseCamera stamps them onto its subtree items exactly like cookCamera
// (the executor rebuilds the SAME LookAtRH/PerspectiveFovRH pair TiXL's ICamera carries). The Camera
// NodeSpec grows a "Reference" Object OUTPUT port (TiXL Camera.Reference) as the wire's source.
//
// FORKS (named):
//   - fork-reusecamera-provider-set (point_ops_camera_scope.h): v1 referenced providers = "Camera";
//     OrthographicCamera deferred (the ref stamp carries no ortho fields), OrbitCamera plugs into the
//     same helper's localFxTime seam when it lands.
//   - fork-reusecamera-gather-side-effects: TiXL skips EVALUATING the subtree on a missing/invalid
//     reference; the sw driver gathers the Commands wire before the op runs, so the op DROPS the items
//     instead of never cooking them (same posture as VisibleGizmos' hidden gate).
//   - fork-reusecamera-no-point-scope: like OrthographicCamera, ReuseCamera does not write the C1
//     point-rail LiveCameraScope (draw rail only); the point-rail bridge is a follow-up seam.
#pragma once

namespace sw {

void registerReuseCameraOp();

// --selftest-reusecamera golden (BOTH driver legs): SolidImage→Layer2d(0.6)→ReuseCamera→RenderTarget
// with a standalone Camera(eye z=5) wired Reference→CameraReference. The eye=5 camera shrinks the quad
// to NDC half 0.6/(5·tan22.5°)=0.290 (the Camera golden's closed form) → the NDC-0.45 probe flips to
// background while center stays quad-colored; cooked through BOTH the flat cook() AND the resident
// terminal (the Object gather is driver code — each leg's branch must bite). A second leg removes the
// reference wire → the whole subtree is dropped (cs:17-21) → center probe background.
// injectBug: drop the ref stamp in the real cook (wrapper passes items through unstamped) → default
// camera keeps the quad large → the 0.45 probe reads quad color → RED. did-not-trip → return 0.
int runReuseCameraSelfTest(bool injectBug);

}  // namespace sw
