// runtime/point_ops_shiftcamera — ShiftCamera command op (camera-B lane).
// TiXL authority: external/tixl/Operators/Lib/render/camera/ShiftCamera.cs (GUID 1a8d2a8d) + ShiftCamera.t3.
//
// ShiftCamera.cs:32-43 Update: read the AMBIENT context.CameraToClipSpace, add Translation.X/Y to
// M31/M32 and (double)Translation.Z/1000.0 to M33 (a raw projection-matrix nudge, e.g. a lens-shift /
// stereo offset), eval the Command subtree under the shifted matrix, restore. SW is retained-mode
// per-item (no context stack), so cookShiftCamera ACCUMULATES the delta onto every subtree item's
// clipShift[3] (render_command.h) where !hasCamera; the RenderTarget executor applies it after
// composing the item's cameraToClipSpace (default or stamped camera).
//
// FORKS (named):
//   - fork-shiftcamera-scale-dead-inputs: TiXL's Scale/UniformScale inputs are DEAD in the live Update
//     body (`var s = Scale*UniformScale` computed then never used — the Matrix.Transformation block
//     ShiftCamera.cs:24-30 is commented out). Dropped from the SW spec (same precedent as the
//     SetW/Visibility dead-input drops in node_registry_point_modify_attributes.cpp).
//   - fork-shiftcamera-matrix-kinds: the shift reaches the kinds that compose a per-item camera matrix
//     (Layer2d + Mesh) — the same scope as the Camera/OrthographicCamera stamps.
#pragma once

namespace sw {

void registerShiftCameraOp();

// --selftest-shiftcamera golden (both legs shape: math closed-form + resident-terminal render flip).
// TOOTH B (math): the shifted projection moves a z=0-plane point's NDC.x by exactly -t.X (hand-derived
//   from perspectiveFovRH's M34=-1 w-divide: z_cam/w = -1) and NDC.z by -t.Z/1000. TOOTH A (render,
//   through the PRODUCTION resident terminal): a Layer2d quad under ShiftCamera(t.X) slides left by
//   t.X in NDC — the center probe flips to background, the shifted-center probe flips to quad color.
// injectBug: drop the clipShift stamp in the REAL cook (wrapper over cookShiftCamera) → the quad stays
//   centered → both probes read the unshifted image → RED. did-not-trip → return 0 (NO-BITE latch).
int runShiftCameraSelfTest(bool injectBug);

}  // namespace sw
