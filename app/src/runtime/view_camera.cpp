// runtime/view_camera.cpp — see view_camera.h. Pure float[3] camera data + orbit/zoom/pan math; ZERO
// Metal. Every routine is transcribed from external/tixl/Editor/Gui/Interaction/Camera/
// CameraInteraction.cs; matrix construction is DELEGATED to field_camera.h so the row-major /
// row-vector / LookAtRH convention has one owner. Constants from CameraInteractionParameters.cs.
#include "runtime/view_camera.h"

#include <cmath>

namespace sw {

namespace {
constexpr float kPi = 3.14159265358979323846f;

// ── TiXL interaction constants (CameraInteractionParameters.cs) ─────────────────────────────────
// cs:11 — public const float ZoomSpeed = 10f;
constexpr float kZoomSpeed = 10.0f;
// cs:5  — public const float RenderWindowHeight = 450; (pan divides pixel delta by this reference height)
constexpr float kRenderWindowHeight = 450.0f;
// CameraInteraction.cs:163 — `_orbitVelocity += delta * _deltaTime * -0.1f`, delta = (MouseDelta.X,
// -MouseDelta.Y). We fold TiXL's per-frame _deltaTime (~1/60) into this single sensitivity so one
// pixel-drag maps to a fixed orbit angle (v1 has no smoothing accumulator — a NAMED FORK below), i.e.
// -0.1f · (1/60). The sign matters: this reproduces TiXL's drag direction.
constexpr float kOrbitSensitivity = -0.1f / 60.0f;

// Shared 3-vector helpers (field_camera.cpp's normalize3/cross3/dot3 live in an anonymous namespace,
// so re-declare the same small ops here — pure, header-free).
void normalize3(float v[3]) {
  float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (len > 0.0f) { v[0] /= len; v[1] /= len; v[2] /= len; }
}
void cross3(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}
float len3(const float v[3]) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }

// Rotate a 3-vector by CreateFromAxisAngle(axis, angle) in System.Numerics ROW-VECTOR convention
// (v·M), matching CameraInteraction's Vector3.Transform(viewDir, rot). `axis` MUST be unit length
// (ViewAxis.Left/Up are normalized). Rodrigues form, which is exactly what Matrix4x4.CreateFromAxisAngle
// builds — v' = v·cosθ + (axis×v)·sinθ + axis·(axis·v)·(1-cosθ). (For a rotation matrix, v·M and M·v
// are transposes; using the cross-product Rodrigues form sidesteps the packing question and is
// convention-agnostic since axis-angle rotation is the same physical rotation either way — see the
// compound-rot note in applyOrbitDrag.)
void rotateAxisAngle(const float axis[3], float angle, const float v[3], float out[3]) {
  float c = std::cos(angle), s = std::sin(angle);
  float axv[3];
  cross3(axis, v, axv);                              // axis × v
  float d = axis[0] * v[0] + axis[1] * v[1] + axis[2] * v[2];  // axis · v
  for (int i = 0; i < 3; ++i)
    out[i] = v[i] * c + axv[i] * s + axis[i] * d * (1.0f - c);
}
}  // namespace

ViewCamera makeDefaultViewCamera() {
  ViewCamera cam;
  cam.eye[0] = 0.0f;
  cam.eye[1] = 0.0f;
  cam.eye[2] = defaultCameraDistance();  // TiXL DefaultCameraDistance = 1/tan(22.5°) = 2.4142135
  cam.target[0] = cam.target[1] = cam.target[2] = 0.0f;
  cam.roll = 0.0f;
  return cam;
}

ViewAxes computeViewAxes(const ViewCamera& cam) {
  // CameraInteraction.ViewAxis.ComputeForCamera (cs:401-410):
  //   ViewDistance = CameraTarget - CameraPosition;
  //   rolledUp = normalize(Transform(UnitY, CreateFromAxisAngle(ViewDistance, CameraRoll)));
  //   Left = normalize(cross(rolledUp, ViewDistance));
  //   Up   = normalize(cross(ViewDistance, Left));
  ViewAxes ax;
  ax.viewDistance[0] = cam.target[0] - cam.eye[0];
  ax.viewDistance[1] = cam.target[1] - cam.eye[1];
  ax.viewDistance[2] = cam.target[2] - cam.eye[2];

  // rolledUp: rotate world-up (0,1,0) about the (un-normalized) ViewDistance axis by roll, then
  // normalize. TiXL passes the un-normalized ViewDistance to CreateFromAxisAngle; Rodrigues needs a
  // unit axis, so normalize a copy first (rotation about an axis is direction-only — magnitude is
  // irrelevant). For roll==0 (the v1 default) this returns exactly (0,1,0), preserving default parity.
  float rollAxis[3] = {ax.viewDistance[0], ax.viewDistance[1], ax.viewDistance[2]};
  normalize3(rollAxis);
  float worldUp[3] = {0.0f, 1.0f, 0.0f};
  float rolledUp[3];
  rotateAxisAngle(rollAxis, cam.roll, worldUp, rolledUp);
  normalize3(rolledUp);

  cross3(rolledUp, ax.viewDistance, ax.left);
  normalize3(ax.left);
  cross3(ax.viewDistance, ax.left, ax.up);
  normalize3(ax.up);
  return ax;
}

void applyOrbitDrag(ViewCamera& cam, float dx, float dy) {
  // Pixel drag → orbit velocity (CameraInteraction.cs:158-163): delta=(dx, -dy); orbit = delta·sens.
  // orbit.x rotates around Up, orbit.y around Left.
  float orbitX = dx * kOrbitSensitivity;
  float orbitY = (-dy) * kOrbitSensitivity;

  ViewAxes ax = computeViewAxes(cam);

  // viewDir = Target - Position; length preserved (ApplyOrbitVelocity 3D branch, cs:282-290).
  float viewDir[3] = {ax.viewDistance[0], ax.viewDistance[1], ax.viewDistance[2]};
  float viewLen = len3(viewDir);
  if (viewLen <= 0.0f) return;  // degenerate (eye==target); nothing to orbit
  float unitView[3] = {viewDir[0] / viewLen, viewDir[1] / viewLen, viewDir[2] / viewLen};

  // rot = rotAroundX · rotAroundY, applied as Transform(viewDir, rot) (row-vector) — i.e. rotate by
  // rotAroundX(Left, orbitY) THEN rotAroundY(Up, orbitX). Compose by applying each rotation in that
  // order (v·A·B == rotate by A, then by B). This reproduces CameraInteraction.cs:265-267/286 exactly.
  float afterX[3], newViewDir[3];
  rotateAxisAngle(ax.left, orbitY, unitView, afterX);
  rotateAxisAngle(ax.up, orbitX, afterX, newViewDir);
  normalize3(newViewDir);

  // Position = Target - newViewDir · viewLen (distance held). eye is Position; roll untouched.
  cam.eye[0] = cam.target[0] - newViewDir[0] * viewLen;
  cam.eye[1] = cam.target[1] - newViewDir[1] * viewLen;
  cam.eye[2] = cam.target[2] - newViewDir[2] * viewLen;
}

void applyZoom(ViewCamera& cam, float wheel, float frameFactor) {
  // HandleMouseWheel non-CameraSpeed branch (cs:216-230). viewDistance = Position - Target.
  if (wheel == 0.0f) return;
  float viewDistance[3] = {cam.eye[0] - cam.target[0], cam.eye[1] - cam.target[1],
                           cam.eye[2] - cam.target[2]};
  float factor = 1.0f + (kZoomSpeed * frameFactor);  // cs:186 zoomFactorForCurrentFramerate
  if (wheel < 0.0f) {                                 // cs:218-219 further away
    for (int i = 0; i < 3; ++i) viewDistance[i] *= factor;
  } else {                                            // cs:224-225 closer
    for (int i = 0; i < 3; ++i) viewDistance[i] /= factor;
  }
  // Position = Target + viewDistance (cs:228). target/roll untouched.
  cam.eye[0] = cam.target[0] + viewDistance[0];
  cam.eye[1] = cam.target[1] + viewDistance[1];
  cam.eye[2] = cam.target[2] + viewDistance[2];
}

void applyPan(ViewCamera& cam, float dx, float dy) {
  // Pan (cs:308-327). factorX = dx/RenderWindowHeight, factorY = dy/RenderWindowHeight.
  // length = |Target - Position| (·CameraSpeed in TiXL — NAMED FORK: no per-user CameraSpeed yet → 1.0).
  // delta = Left·factorX·length + Up·factorY·length; Position += delta; Target += delta.
  ViewAxes ax = computeViewAxes(cam);
  float factorX = dx / kRenderWindowHeight;
  float factorY = dy / kRenderWindowHeight;
  float length = len3(ax.viewDistance);
  float delta[3];
  for (int i = 0; i < 3; ++i)
    delta[i] = ax.left[i] * factorX * length + ax.up[i] * factorY * length;
  for (int i = 0; i < 3; ++i) {
    cam.eye[i] += delta[i];
    cam.target[i] += delta[i];
  }
}

LayerCameraForward toForward(const ViewCamera& cam, float aspect) {
  // SAME return type + math as defaultLayerCameraForward — but this camera's eye/target/up. up is the
  // roll-rolled view Up (computeViewAxes); for the DEFAULT ViewCamera up==(0,1,0) exactly, and eye ==
  // (0,0,DefaultCameraDistance), target==0 → lookAtRH gets byte-identical inputs to
  // defaultLayerCameraForward, so the DEFAULT ViewCamera reproduces it bit-for-bit (the parity tooth).
  // fov/near/far are the SetDefaultCamera constants, shared with field_camera.
  ViewAxes ax = computeViewAxes(cam);
  LayerCameraForward f;
  f.worldToCamera = lookAtRH(cam.eye, cam.target, ax.up);
  f.cameraToClipSpace = perspectiveFovRH(kDefaultCamFovDegrees * kPi / 180.0f, aspect, 0.01f, 1000.0f);
  return f;
}

}  // namespace sw
