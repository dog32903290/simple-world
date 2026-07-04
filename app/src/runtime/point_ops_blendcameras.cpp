// runtime/point_ops_blendcameras — BlendCameras cook + the CameraDefinition blend math (header has
// the story + forks). TiXL authority: BlendCameras.cs + ICamera.cs (line refs inline).
#include "runtime/point_ops_blendcameras.h"

#include <cmath>

#include "runtime/point_ops_camera_scope.h"        // resolveActiveCamera (the Camera param semantics)
#include "runtime/point_ops_camerawithrotation.h"  // cameraWithRotationRotation (the CWR rotation)
#include "runtime/render_command.h"                // RenderDrawItem (the stamp target)

namespace sw {

bool& blendCamerasBugDropFraction() {
  static bool v = false;  // OFF in production; the golden flips it around one cook then resets
  return v;
}

namespace {

constexpr float kD2R = 3.14159265358979323846f / 180.0f;

float lerp1(float a, float b, float f) { return a + (b - a) * f; }  // MathUtils.Lerp (:305)
void lerp3(const float a[3], const float b[3], float f, float out[3]) {
  for (int i = 0; i < 3; ++i) out[i] = lerp1(a[i], b[i], f);
}
void cross3(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}
void norm3(float v[3]) {
  const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (l > 0.0f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

struct Quat {
  float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
};

// System.Numerics Quaternion.CreateFromRotationMatrix (Shepperd branch structure, verbatim). `m` is
// the row-major 3x3 rotation block (m[r*4+c] of a Mat4-shaped clean matrix).
Quat quatFromRotationRows(const float m[16]) {
  Quat q;
  const float trace = m[0] + m[5] + m[10];
  if (trace > 0.0f) {
    float s = std::sqrt(trace + 1.0f);
    q.w = s * 0.5f;
    s = 0.5f / s;
    q.x = (m[6] - m[9]) * s;   // M23 - M32
    q.y = (m[8] - m[2]) * s;   // M31 - M13
    q.z = (m[1] - m[4]) * s;   // M12 - M21
  } else if (m[0] >= m[5] && m[0] >= m[10]) {
    const float s = std::sqrt(1.0f + m[0] - m[5] - m[10]);
    const float invS = 0.5f / s;
    q.x = 0.5f * s;
    q.y = (m[1] + m[4]) * invS;   // M12 + M21
    q.z = (m[2] + m[8]) * invS;   // M13 + M31
    q.w = (m[6] - m[9]) * invS;   // M23 - M32
  } else if (m[5] > m[10]) {
    const float s = std::sqrt(1.0f + m[5] - m[0] - m[10]);
    const float invS = 0.5f / s;
    q.x = (m[4] + m[1]) * invS;   // M21 + M12
    q.y = 0.5f * s;
    q.z = (m[9] + m[6]) * invS;   // M32 + M23
    q.w = (m[8] - m[2]) * invS;   // M31 - M13
  } else {
    const float s = std::sqrt(1.0f + m[10] - m[0] - m[5]);
    const float invS = 0.5f / s;
    q.x = (m[8] + m[2]) * invS;   // M31 + M13
    q.y = (m[9] + m[6]) * invS;   // M32 + M23
    q.z = 0.5f * s;
    q.w = (m[1] - m[4]) * invS;   // M12 - M21
  }
  return q;
}

float quatDot(const Quat& a, const Quat& b) {
  return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

// System.Numerics Quaternion.Slerp (verbatim structure; the near-1 linear fallback included).
Quat quatSlerp(const Quat& a, const Quat& b, float t) {
  const float epsilon = 1e-6f;
  float cosOmega = quatDot(a, b);
  bool flip = false;
  if (cosOmega < 0.0f) { flip = true; cosOmega = -cosOmega; }
  float s1, s2;
  if (cosOmega > 1.0f - epsilon) {
    s1 = 1.0f - t;
    s2 = flip ? -t : t;
  } else {
    const float omega = std::acos(cosOmega);
    const float invSinOmega = 1.0f / std::sin(omega);
    s1 = std::sin((1.0f - t) * omega) * invSinOmega;
    s2 = flip ? -std::sin(t * omega) * invSinOmega : std::sin(t * omega) * invSinOmega;
  }
  Quat r;
  r.x = s1 * a.x + s2 * b.x;
  r.y = s1 * a.y + s2 * b.y;
  r.z = s1 * a.z + s2 * b.z;
  r.w = s1 * a.w + s2 * b.w;
  return r;
}

Quat quatNormalize(const Quat& q) {
  const float l = std::sqrt(quatDot(q, q));
  Quat r = q;
  if (l > 0.0f) { r.x /= l; r.y /= l; r.z /= l; r.w /= l; }
  return r;
}

// System.Numerics Vector3.Transform(v, Quaternion): v + 2·cross(q.xyz, cross(q.xyz, v) + w·v).
void quatRotate(const Quat& q, const float v[3], float out[3]) {
  const float qv[3] = {q.x, q.y, q.z};
  float c1[3], t[3];
  cross3(qv, v, c1);
  t[0] = c1[0] + q.w * v[0];
  t[1] = c1[1] + q.w * v[1];
  t[2] = c1[2] + q.w * v[2];
  float c2[3];
  cross3(qv, t, c2);
  out[0] = v[0] + 2.0f * c2[0];
  out[1] = v[1] + 2.0f * c2[1];
  out[2] = v[2] + 2.0f * c2[2];
}

float pget(const std::map<std::string, float>& m, const char* id, float def) {
  auto it = m.find(id);
  return it != m.end() ? it->second : def;
}

// = ExtractCameraQuaternion (ICamera.cs:76-107): build w2c, invert, orthonormalize the 3x3 rows of
// cameraToWorld (right/up/forward), CreateFromRotationMatrix on the clean row basis.
Quat extractCameraQuaternion(const SwCameraDefinition& d) {
  Mat4 c2c, w2c;
  buildProjectionMatrices(d, c2c, w2c);
  Mat4 c2w;
  mat4Inverse(w2c, c2w);
  float right[3] = {c2w.m[0], c2w.m[1], c2w.m[2]};    // row0 (cs:91)
  float up[3] = {c2w.m[4], c2w.m[5], c2w.m[6]};       // row1 (cs:92)
  float forward[3] = {c2w.m[8], c2w.m[9], c2w.m[10]}; // row2 (cs:93)
  norm3(right);
  norm3(up);
  norm3(forward);
  cross3(up, forward, right);  // right = normalize(up × forward) (cs:97)
  norm3(right);
  cross3(forward, right, up);  // up = forward × right (cs:98)
  Mat4 clean = mat4Identity();
  clean.m[0] = right[0];   clean.m[1] = right[1];   clean.m[2] = right[2];
  clean.m[4] = up[0];      clean.m[5] = up[1];      clean.m[6] = up[2];
  clean.m[8] = forward[0]; clean.m[9] = forward[1]; clean.m[10] = forward[2];
  return quatFromRotationRows(clean.m);  // (cs:100-106)
}

// System.Numerics CreateFromYawPitchRoll (row-vector): roll about Z first, then pitch about X, then
// yaw about Y → M = RotZ(roll)·RotX(pitch)·RotY(yaw).
Mat4 yawPitchRollMatrix(float yawRad, float pitchRad, float rollRad) {
  auto rx = [](float r) {
    Mat4 m = mat4Identity();
    m.m[5] = std::cos(r); m.m[6] = std::sin(r);
    m.m[9] = -std::sin(r); m.m[10] = std::cos(r);
    return m;
  };
  auto ry = [](float r) {
    Mat4 m = mat4Identity();
    m.m[0] = std::cos(r); m.m[2] = -std::sin(r);
    m.m[8] = std::sin(r); m.m[10] = std::cos(r);
    return m;
  };
  auto rz = [](float r) {
    Mat4 m = mat4Identity();
    m.m[0] = std::cos(r); m.m[1] = std::sin(r);
    m.m[4] = -std::sin(r); m.m[5] = std::cos(r);
    return m;
  };
  return mat4Mul(mat4Mul(rz(rollRad), rx(pitchRad)), ry(yawRad));
}

}  // namespace

bool cameraDefinitionFromParams(const std::string& opType,
                                const std::map<std::string, float>& params,
                                SwCameraDefinition& out) {
  if (opType == "Camera") {
    // Camera.cs:58-71 — the def carries the RAW aspect here (the <0.0001 → output-aspect resolution
    // is deferred to the stamp/value consumer, fork-blendcameras-mixed-aspect-fallback).
    const ActiveCamera ac = resolveActiveCamera(params);  // one param-semantics codepath
    for (int i = 0; i < 3; ++i) {
      out.position[i] = ac.eye[i];
      out.target[i] = ac.target[i];
      out.up[i] = ac.up[i];
    }
    out.nearFar[0] = ac.nearClip;
    out.nearFar[1] = ac.farClip;
    out.fovRad = ac.fovDeg * kD2R;  // FieldOfView.ToRadians (Camera.cs:68)
    out.aspectRatio = ac.aspect;
    // sw's Camera spec has no Roll/LensShift/PositionOffset/RotationOffset ports (named drop in
    // node_registry_draw_camera.cpp) → the ctor zeros stand (== TiXL defaults for those inputs).
    out.roll = 0.0f;
    out.lensShift[0] = out.lensShift[1] = 0.0f;
    out.positionOffset[0] = out.positionOffset[1] = out.positionOffset[2] = 0.0f;
    out.rotationOffset[0] = out.rotationOffset[1] = out.rotationOffset[2] = 0.0f;
    out.offsetAffectsTarget = false;
    return true;
  }
  if (opType == "CameraWithRotation") {
    // CameraWithRotation.cs:92-108 — Target = position + TransformNormal(UnitZ, R) (= row2 of R,
    // the +Z definition, DISTINCT from its pushed matrices); Roll = euler.Z radians (cs:105).
    CameraWithRotationParams cw;
    cw.position[0] = pget(params, "Position.x", 0.0f);
    cw.position[1] = pget(params, "Position.y", 0.0f);
    cw.position[2] = pget(params, "Position.z", 2.4141f);
    cw.rotationMode = (int)pget(params, "RotationMode", 0.0f);
    cw.rotation[0] = pget(params, "Rotation.x", 0.0f);
    cw.rotation[1] = pget(params, "Rotation.y", 0.0f);
    cw.rotation[2] = pget(params, "Rotation.z", 0.0f);
    cw.rotationFactor[0] = pget(params, "RotationFactor.x", 1.0f);
    cw.rotationFactor[1] = pget(params, "RotationFactor.y", 1.0f);
    cw.rotationFactor[2] = pget(params, "RotationFactor.z", 1.0f);
    cw.rotationOffset2[0] = pget(params, "RotationOffset2.x", 0.0f);
    cw.rotationOffset2[1] = pget(params, "RotationOffset2.y", 0.0f);
    cw.rotationOffset2[2] = pget(params, "RotationOffset2.z", 0.0f);
    cw.quaternion[0] = pget(params, "RotationQuaternion.x", 1.0f);
    cw.quaternion[1] = pget(params, "RotationQuaternion.y", 1.0f);
    cw.quaternion[2] = pget(params, "RotationQuaternion.z", 1.0f);
    cw.quaternion[3] = pget(params, "RotationQuaternion.w", 1.0f);
    const Mat4 r = cameraWithRotationRotation(cw);
    for (int i = 0; i < 3; ++i) out.position[i] = cw.position[i];
    out.target[0] = cw.position[0] + r.m[8];   // + TransformNormal(UnitZ, R) = row2 (cs:92-93)
    out.target[1] = cw.position[1] + r.m[9];
    out.target[2] = cw.position[2] + r.m[10];
    out.up[0] = pget(params, "Up.x", 0.0f);
    out.up[1] = pget(params, "Up.y", 1.0f);
    out.up[2] = pget(params, "Up.z", 0.0f);
    out.nearFar[0] = pget(params, "ClipPlanes.x", 0.01f);
    out.nearFar[1] = pget(params, "ClipPlanes.y", 1000.0f);
    out.lensShift[0] = pget(params, "LensShift.x", 0.0f);
    out.lensShift[1] = pget(params, "LensShift.y", 0.0f);
    out.positionOffset[0] = pget(params, "PositionOffset.x", 0.0f);
    out.positionOffset[1] = pget(params, "PositionOffset.y", 0.0f);
    out.positionOffset[2] = pget(params, "PositionOffset.z", 0.0f);
    out.aspectRatio = pget(params, "AspectRatio", -1.0f);
    out.fovRad = pget(params, "FOV", 45.0f) * kD2R;  // (cs:104)
    // Roll = euler.Z radians (cs:66 + :105): (Rotation.z·Factor.z + Offset2.z)·ToRad.
    out.roll = (cw.rotation[2] * cw.rotationFactor[2] + cw.rotationOffset2[2]) * kD2R;
    out.rotationOffset[0] = pget(params, "RotationOffset.x", 0.0f);
    out.rotationOffset[1] = pget(params, "RotationOffset.y", 0.0f);
    out.rotationOffset[2] = pget(params, "RotationOffset.z", 0.0f);
    out.offsetAffectsTarget = pget(params, "AlsoOffsetTarget", 1.0f) > 0.5f;  // .t3 default true
    return true;
  }
  return false;  // fork-blendcameras-ref-types-v1 (header)
}

void buildProjectionMatrices(const SwCameraDefinition& d, Mat4& outC2C, Mat4& outW2C) {
  // ICamera.cs:110-130 verbatim. PerspectiveFovRH clamps aspect/near/far internally (GraphicsMath).
  outC2C = perspectiveFovRH(d.fovRad, d.aspectRatio, d.nearFar[0], d.nearFar[1]);
  outC2C.m[8] = d.lensShift[0];  // M31 (cs:113)
  outC2C.m[9] = d.lensShift[1];  // M32 (cs:114)
  float eye[3] = {d.position[0], d.position[1], d.position[2]};
  if (!d.offsetAffectsTarget)
    for (int i = 0; i < 3; ++i) eye[i] += d.positionOffset[i];  // (cs:116-118)
  const Mat4 root = lookAtRH(eye, d.target, d.up);  // (cs:120)
  // rollRotation = CreateFromAxisAngle(UnitZ, −Roll·ToRad) (cs:121 — TiXL multiplies the stored roll
  // by ToRad again; transcribed literally, TiXL-faithful including its own unit quirk).
  Mat4 rollRot = mat4Identity();
  {
    const float r = -d.roll * kD2R;
    rollRot.m[0] = std::cos(r); rollRot.m[1] = std::sin(r);
    rollRot.m[4] = -std::sin(r); rollRot.m[5] = std::cos(r);
  }
  Mat4 addTrans = mat4Identity();  // (cs:122-123)
  if (d.offsetAffectsTarget) {
    addTrans.m[12] = d.positionOffset[0];
    addTrans.m[13] = d.positionOffset[1];
    addTrans.m[14] = d.positionOffset[2];
  }
  const Mat4 addRot = yawPitchRollMatrix(d.rotationOffset[1] * kD2R, d.rotationOffset[0] * kD2R,
                                         d.rotationOffset[2] * kD2R);  // (cs:125-127)
  outW2C = mat4Mul(mat4Mul(mat4Mul(root, rollRot), addRot), addTrans);  // (cs:129)
}

SwCameraDefinition blendCameraDefinitions(const SwCameraDefinition& a, const SwCameraDefinition& b,
                                          float f) {
  // ICamera.cs:38-73 verbatim.
  f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);  // (cs:40)
  SwCameraDefinition r;
  lerp3(a.position, b.position, f, r.position);  // (cs:42; the lerped target of :43 is overwritten
                                                 //  by :60 Target = position + forward — as in TiXL)
  Quat qa = extractCameraQuaternion(a);          // (cs:45-46)
  Quat qb = extractCameraQuaternion(b);
  if (quatDot(qa, qb) < 0.0f) { qb.x = -qb.x; qb.y = -qb.y; qb.z = -qb.z; qb.w = -qb.w; }  // (cs:49-50)
  const Quat q = quatNormalize(quatSlerp(qa, qb, f));  // (cs:52)
  const float negUnitZ[3] = {0.0f, 0.0f, -1.0f};
  const float unitY[3] = {0.0f, 1.0f, 0.0f};
  float forward[3], up[3];
  quatRotate(q, negUnitZ, forward);  // (cs:54)
  quatRotate(q, unitY, up);          // (cs:55)
  for (int i = 0; i < 3; ++i) r.target[i] = r.position[i] + forward[i];  // (cs:60, RH camera)
  norm3(up);
  for (int i = 0; i < 3; ++i) r.up[i] = up[i];  // (cs:61)
  r.roll = 0.0f;                                // (cs:62)
  for (int i = 0; i < 2; ++i) {
    r.nearFar[i] = lerp1(a.nearFar[i], b.nearFar[i], f);      // (cs:64)
    r.lensShift[i] = lerp1(a.lensShift[i], b.lensShift[i], f); // (cs:65)
  }
  lerp3(a.positionOffset, b.positionOffset, f, r.positionOffset);  // (cs:66)
  r.aspectRatio = lerp1(a.aspectRatio, b.aspectRatio, f);          // (cs:67)
  r.fovRad = lerp1(a.fovRad, b.fovRad, f);                         // (cs:68)
  lerp3(a.rotationOffset, b.rotationOffset, f, r.rotationOffset);  // (cs:69)
  r.offsetAffectsTarget = f < 0.5f ? a.offsetAffectsTarget : b.offsetAffectsTarget;  // (cs:71)
  return r;
}

void cameraStampFromW2C(const Mat4& w2c, float outEye[3], float outTarget[3], float outUp[3]) {
  Mat4 c2w;
  mat4Inverse(w2c, c2w);
  // Row-vector camToWorld: row3 = camera origin in world; row1 = camera +y (up); row2 = camera +z
  // (backward). LookAtRH(eye, eye−row2, row1) reproduces the rigid w2c exactly.
  outEye[0] = c2w.m[12]; outEye[1] = c2w.m[13]; outEye[2] = c2w.m[14];
  outUp[0] = c2w.m[4]; outUp[1] = c2w.m[5]; outUp[2] = c2w.m[6];
  outTarget[0] = c2w.m[12] - c2w.m[8];
  outTarget[1] = c2w.m[13] - c2w.m[9];
  outTarget[2] = c2w.m[14] - c2w.m[10];
}

// BlendCameras: Command subtree in → Command out (BlendCameras.cs:24-106). Resolves the wired camera
// references, picks the [index, index+1] pair by the float Index (cs:31-32), blends (cs:74-75), and
// stamps the subtree with the blended camera. TiXL's error legs (no cameras / not a camera, cs:38-55)
// RETURN WITHOUT evaluating the subtree → the sw mirror emits an EMPTY chain.
RenderCommand cookBlendCameras(CmdCookCtx& c) {
  RenderCommand rc;
  const int count = (int)c.cameraRefs.size();
  if (count == 0) return rc;  // "No cameras connected?" (cs:38-42) — subtree not evaluated
  float floatIndex = cookParam(c, "Index", 0.0f);
  const float hi = (float)count - 1.0001f;  // (cs:31 Clamp(0, count−1.0001))
  floatIndex = floatIndex < 0.0f ? 0.0f : (floatIndex > hi ? hi : floatIndex);
  int index = (int)floatIndex;  // (cs:32; count==1 → hi<0 → clamp yields ≤0 → index 0)
  if (index < 0) index = 0;
  float blend = floatIndex - (float)index;  // (cs:74)
  if (blendCamerasBugDropFraction()) blend = 0.0f;  // ★-bug: fractional blend dropped → camA only
  const int ia = count == 1 ? 0 : index;
  const int ib = count == 1 ? 0 : index + 1;  // (cs:44-56 single-camera leg: camA = camB)
  SwCameraDefinition camA, camB;
  if (!c.cameraRefs[ia].params || !c.cameraRefs[ib].params ||
      !cameraDefinitionFromParams(c.cameraRefs[ia].opType, *c.cameraRefs[ia].params, camA) ||
      !cameraDefinitionFromParams(c.cameraRefs[ib].opType, *c.cameraRefs[ib].params, camB))
    return rc;  // "That's not a camera" (cs:51-55) — subtree not evaluated

  const SwCameraDefinition blended = blendCameraDefinitions(camA, camB, blend);  // (cs:75)
  Mat4 c2c, w2c;
  buildProjectionMatrices(blended, c2c, w2c);  // (cs:77)
  float eye[3], tgt[3], up[3];
  cameraStampFromW2C(w2c, eye, tgt, up);
  // fork-blendcameras-mixed-aspect-fallback (header): either raw ref aspect < 0.0001 → stamp 0
  // (executor output aspect); both explicit → the TiXL lerp.
  const float aspectStamp =
      (camA.aspectRatio < 0.0001f || camB.aspectRatio < 0.0001f) ? 0.0f : blended.aspectRatio;

  if (!c.inputCommand) return rc;  // no subtree wired → empty (TiXL: eval an empty Command)
  rc.items = c.inputCommand->items;
  for (RenderDrawItem& it : rc.items) {
    if (it.hasCamera) continue;  // a NESTED camera already stamped this item (innermost wins)
    it.hasCamera = true;
    it.camEye[0] = eye[0]; it.camEye[1] = eye[1]; it.camEye[2] = eye[2];
    it.camTarget[0] = tgt[0]; it.camTarget[1] = tgt[1]; it.camTarget[2] = tgt[2];
    it.camUp[0] = up[0]; it.camUp[1] = up[1]; it.camUp[2] = up[2];
    it.camFovDeg = blended.fovRad / kD2R;
    it.camNear = blended.nearFar[0];
    it.camFar = blended.nearFar[1];
    it.camAspect = aspectStamp;
    // fork-blendcameras-lensshift-drawrail-drop: blended LensShift not carried by the stamp (header).
  }
  return rc;
}

void registerBlendCamerasOp() { registerCmdOp("BlendCameras", cookBlendCameras); }

}  // namespace sw
