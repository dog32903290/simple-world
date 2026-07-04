// runtime/point_ops_camerawithrotation — CameraWithRotation: the rotation-driven camera push.
// TiXL authority: Operators/Lib/render/camera/CameraWithRotation.cs (+ CameraWithRotation.t3 defaults).
//
// TiXL builds the pushed matrices DIRECTLY from position + a rotation matrix (cs:110-117):
//   camToClipSpace = PerspectiveFovRH(FOV.rad, aspect, clip.x, clip.y); M31/M32 = LensShift (cs:110-112)
//   worldToCamera  = CreateTranslation(-Position) * rotationMatrix     (cs:114-115)
// with rotationMatrix from Euler (heading·pitch·roll, cs:78-84 — NOT CreateFromYawPitchRoll, the axes
// apply in reverse order, cs:73-75) or a Quaternion (cs:87-88).
//
// SW INTEGRATION — the draw rail stamps eye/target/up (render_command.h camEye/camTarget/camUp; the
// executor rebuilds WorldToCamera via LookAtRH). T(−pos)·R is EXACTLY LookAtRH-representable when R is
// orthonormal: LookAtRH(pos, pos − Rcol2, Rcol1) reconstructs it element-for-element (zAxis=Rcol2 ⇒
// xAxis=cross(Rcol1,Rcol2)=Rcol0, yAxis=Rcol1, translation −pos·R — the RH orthonormal basis identity).
// cameraWithRotationStamp() emits that decomposition; the VALUE rail (resident_camera_value_cook)
// consumes cameraWithRotationMatrices() (the raw T(−pos)·R + projection) directly.
//
// FORKS (named):
//   fork-camerawithrotation-lensshift-drawrail-drop — the per-item stamp carries no lens shift (the
//     Camera-family precedent: Camera.cs's lensShift embellishment was dropped from the stamp), so the
//     DRAW rail renders with LensShift=(0,0); the VALUE rail's matrices keep M31/M32 (cs:111-112) since
//     they are free there. .t3 default IS (0,0) → byte-identical for unauthored graphs.
//   fork-camerawithrotation-nonunit-quat-renormalized — a NON-UNIT RotationQuaternion yields a
//     non-orthonormal R (System.Numerics CreateFromQuaternion does not normalize); the LookAtRH stamp
//     decomposition re-normalizes, so the DRAW rail sees the normalized rotation (the VALUE rail keeps
//     the raw R). Unit quaternions (the meaningful inputs) are exact on both rails.
//   fork-camerawithrotation-no-point-rail-scope — not a C1 camera-scope writer v1 (point ops under it
//     read the default camera), same as OrthographicCamera; the C1 scope only speaks eye/target/up.
//   Up / PositionOffset / AlsoOffsetTarget / RotationOffset are DEAD for the pushed matrices in TiXL
//     itself (cs:114-115 uses only position+rotation; they live on the CameraDefinition used by
//     BlendCameras/ActionCamera referencing) — carried on the spec for interface parity.
//
// runtime leaf: pure math + the cmd-op cook. No UI, no upward deps.
#pragma once

#include "runtime/field_camera.h"  // Mat4
#include "runtime/point_graph.h"   // CmdCookCtx / RenderCommand (the cook signature)

namespace sw {

// The op's resolved inputs (defaults = CameraWithRotation.t3 DefaultValues).
struct CameraWithRotationParams {
  float position[3] = {0.0f, 0.0f, 2.4141f};  // .t3 (note: 2.4141, NOT the 2.4142135 Camera default)
  int rotationMode = 0;                        // 0=Euler 1=Quaternion (cs:120-124)
  float rotation[3] = {0.0f, 0.0f, 0.0f};      // degrees (Euler mode)
  float rotationFactor[3] = {1.0f, 1.0f, 1.0f};
  float rotationOffset2[3] = {0.0f, 0.0f, 0.0f};
  float quaternion[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // .t3 default (X,Y,Z,W)
  float fovDeg = 45.0f;
  float clipNear = 0.01f;
  float clipFar = 1000.0f;
  float aspectIn = -1.0f;      // < 0.0001 → output aspect (cs:53-57; .t3 default -1)
  float lensShift[2] = {0.0f, 0.0f};
};

// The rotation matrix (cs:61-90): Euler mode → euler = Rotation·RotationFactor + RotationOffset2
// (cs:66), R = heading·pitch·roll = RotX(euler.Y)·RotY(euler.X)·RotZ(euler.Z) (cs:81-84, row-vector);
// Quaternion mode → CreateFromQuaternion(RotationQuaternion) (cs:87-88, NOT normalized).
Mat4 cameraWithRotationRotation(const CameraWithRotationParams& p);

// The PUSHED matrix pair (cs:110-117): outW2C = T(−position)·R; outC2C = PerspectiveFovRH(fov, aspect,
// near, far) with M31/M32 = LensShift. aspect = aspectIn<0.0001 ? fallbackAspect : aspectIn (cs:53-57).
void cameraWithRotationMatrices(const CameraWithRotationParams& p, float fallbackAspect, Mat4& outW2C,
                                Mat4& outC2C);

// The draw-rail per-item stamp: the LookAtRH decomposition of T(−pos)·R (header note) —
// eye = position, target = position − Rcol2, up = Rcol1.
void cameraWithRotationStamp(const CameraWithRotationParams& p, float outEye[3], float outTarget[3],
                             float outUp[3]);

// Read the op's resolved Float params off a cmd cook ctx into the struct (.t3 defaults on absent keys).
CameraWithRotationParams readCameraWithRotationParams(const CmdCookCtx& c);

// The Command→Command cook (stamps the subtree, cookCamera precedent). Public so the golden can
// cook-through the EXACT registered fn with a hand CmdCookCtx.
RenderCommand cookCameraWithRotation(CmdCookCtx& c);

void registerCameraWithRotationOp();

// -bug seam (true cook-path corrosion): when true the Euler branch composes the rotation in the WRONG
// order TiXL's own comment warns about (CreateFromYawPitchRoll axis order, cs:73-76) — the multi-axis
// Euler probe's stamp/matrices flip → RED. OFF in production.
bool& cameraWithRotationBugYawPitchRollOrder();

}  // namespace sw
