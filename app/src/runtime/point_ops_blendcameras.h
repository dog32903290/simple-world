// runtime/point_ops_blendcameras — BlendCameras: slerp-blend N referenced cameras by a float Index.
// TiXL authority: Operators/Lib/render/camera/BlendCameras.cs + Core/Operator/Interfaces/ICamera.cs
// (CameraDefinition.Blend :38-73, ExtractCameraQuaternion :76-107, BuildProjectionMatrices :110-130).
//
// MECHANISM — TiXL hands live ICamera instances through Slot<Object> references; sw's cook drivers
// hand the STRUCTURAL identity instead (CmdCookCtx::cameraRefs = wired upstream (opType, resolved
// params), the camera-A CameraRef gather seam). This leaf rebuilds each referenced camera's
// CameraDefinition from that identity, blends per ICamera.cs, builds the blended worldToCamera, and
// stamps the subtree items via the rigid-matrix LookAtRH decomposition (eye = camToWorld row3, up =
// camToWorld row1, target = eye − camToWorld row2 — exact for any rigid w2c, the
// point_ops_camerawithrotation proof generalized through the inverse).
//
// FORKS (named):
//   fork-blendcameras-ref-types-v1 — supported reference types = Camera / CameraWithRotation (the ops
//     whose CameraDefinition this lane rebuilt). An unsupported ref (nested BlendCameras, Ortho, a
//     non-camera) = TiXL's "That's not a camera" error leg (cs:51-55) → the subtree is NOT evaluated
//     (empty chain), faithful to the early return.
//   fork-blendcameras-mixed-aspect-fallback — a ref whose raw AspectRatio < 0.0001 means "output
//     aspect" (Camera.cs:52-56), which is only known at the executor. Both refs defaulted → stamp 0
//     (executor output aspect, == TiXL); both explicit → stamp the lerp (== TiXL); MIXED → stamp 0
//     (TiXL would lerp the resolved output aspect with the explicit one — one scalar stamp cannot
//     carry that; the fork picks the output aspect).
//   fork-blendcameras-lensshift-drawrail-drop — blended LensShift is dropped from the per-item stamp
//     (the family fork); the VALUE rail keeps it (buildProjectionMatrices emits M31/M32).
//
// runtime leaf: pure math + the cmd-op cook. No UI, no upward deps.
#pragma once

#include <map>
#include <string>

#include "runtime/field_camera.h"  // Mat4
#include "runtime/point_graph.h"   // CmdCookCtx / RenderCommand

namespace sw {

// = TiXL CameraDefinition (ICamera.cs:18-36, ctor defaults). Angles in RADIANS where TiXL stores
// radians (FieldOfView, Roll); RotationOffset in DEGREES (BuildProjectionMatrices converts, :125-127).
struct SwCameraDefinition {
  float nearFar[2] = {0.0f, 0.0f};
  float lensShift[2] = {0.0f, 0.0f};
  float positionOffset[3] = {0.0f, 0.0f, 0.0f};
  float position[3] = {0.0f, 0.0f, 2.4142135f};  // GraphicsMath.DefaultCameraDistance (ICamera.cs:28)
  float target[3] = {0.0f, 0.0f, 0.0f};
  float up[3] = {0.0f, 1.0f, 0.0f};
  float aspectRatio = -1.0f;  // raw; < 0.0001 = "output aspect" sentinel (Camera.cs:52-56)
  float fovRad = 45.0f * 3.14159265358979323846f / 180.0f;
  float roll = 0.0f;
  float rotationOffset[3] = {0.0f, 0.0f, 0.0f};
  bool offsetAffectsTarget = false;
};

// Rebuild a referenced camera op's CameraDefinition from its structural identity (opType + resolved
// params). Camera → Camera.cs:58-71; CameraWithRotation → CameraWithRotation.cs:92-108 (Target =
// position + TransformNormal(UnitZ, R), the +Z definition — distinct from its PUSHED matrices).
// Returns false for an unsupported type (fork-blendcameras-ref-types-v1).
bool cameraDefinitionFromParams(const std::string& opType,
                                const std::map<std::string, float>& params, SwCameraDefinition& out);

// = CameraDefinition.Blend (ICamera.cs:38-73): clamp f, lerp position, slerp the extracted camera
// orientations (shortest path), rebuild Target/Up from the blended quaternion, lerp the scalars.
SwCameraDefinition blendCameraDefinitions(const SwCameraDefinition& a, const SwCameraDefinition& b,
                                          float f);

// = CameraDefinition.BuildProjectionMatrices (ICamera.cs:110-130): camToClip = PerspectiveFovRH +
// LensShift M31/M32; worldToCamera = LookAtRH(eye,target,up)·rollRotation·additionalRotation·
// additionalTranslation. Uses d.aspectRatio raw (PerspectiveFovRH clamps, GraphicsMath.cs:29-32).
void buildProjectionMatrices(const SwCameraDefinition& d, Mat4& outC2C, Mat4& outW2C);

// Decompose a RIGID worldToCamera into the per-item stamp (eye/target/up): camToWorld = inverse(w2c);
// eye = row3, up = row1, target = eye − row2. LookAtRH(eye,target,up) reproduces w2c exactly.
void cameraStampFromW2C(const Mat4& w2c, float outEye[3], float outTarget[3], float outUp[3]);

// The Command→Command cook (BlendCameras.cs:24-106). Public for the golden's true cook-through.
RenderCommand cookBlendCameras(CmdCookCtx& c);

void registerBlendCamerasOp();

// -bug seam (true cook-path corrosion): when true the cook DROPS the fractional blend (blend = 0 →
// camA only, cs:74's frac lost) — every mid-blend probe flips to the camA endpoint → RED.
bool& blendCamerasBugDropFraction();

}  // namespace sw
