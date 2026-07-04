// runtime/point_ops_camerawithrotation — CameraWithRotation cook + matrix builders (header has the
// story + forks). TiXL authority: Operators/Lib/render/camera/CameraWithRotation.cs.
#include "runtime/point_ops_camerawithrotation.h"

#include <cmath>

#include "runtime/render_command.h"  // RenderDrawItem (the stamp target)

namespace sw {

bool& cameraWithRotationBugYawPitchRollOrder() {
  static bool v = false;  // OFF in production; the golden flips it around one cook then resets
  return v;
}

namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

// System.Numerics Matrix4x4.CreateRotationX/Y/Z — row-major, row-vector (v·M), verbatim element layout.
Mat4 rotX(float rad) {
  Mat4 m = mat4Identity();
  const float c = std::cos(rad), s = std::sin(rad);
  m.m[5] = c;  m.m[6] = s;
  m.m[9] = -s; m.m[10] = c;
  return m;
}
Mat4 rotY(float rad) {
  Mat4 m = mat4Identity();
  const float c = std::cos(rad), s = std::sin(rad);
  m.m[0] = c; m.m[2] = -s;
  m.m[8] = s; m.m[10] = c;
  return m;
}
Mat4 rotZ(float rad) {
  Mat4 m = mat4Identity();
  const float c = std::cos(rad), s = std::sin(rad);
  m.m[0] = c;  m.m[1] = s;
  m.m[4] = -s; m.m[5] = c;
  return m;
}
// System.Numerics Matrix4x4.CreateFromQuaternion — verbatim (does NOT normalize; cs:88 passes the raw
// input quaternion straight through).
Mat4 quatToMat(const float q[4]) {
  const float x = q[0], y = q[1], z = q[2], w = q[3];
  const float xx = x * x, yy = y * y, zz = z * z;
  const float xy = x * y, wz = z * w, xz = z * x, wy = y * w, yz = y * z, wx = x * w;
  Mat4 m = mat4Identity();
  m.m[0] = 1.0f - 2.0f * (yy + zz); m.m[1] = 2.0f * (xy + wz);        m.m[2] = 2.0f * (xz - wy);
  m.m[4] = 2.0f * (xy - wz);        m.m[5] = 1.0f - 2.0f * (zz + xx); m.m[6] = 2.0f * (yz + wx);
  m.m[8] = 2.0f * (xz + wy);        m.m[9] = 2.0f * (yz - wx);        m.m[10] = 1.0f - 2.0f * (yy + xx);
  return m;
}

}  // namespace

Mat4 cameraWithRotationRotation(const CameraWithRotationParams& p) {
  if (p.rotationMode == 1) return quatToMat(p.quaternion);  // cs:87-88 (Quaternion mode)
  // Euler (cs:66 + :78-84): euler = Rotation·RotationFactor + RotationOffset2 (component-wise), then
  //   pitch   = CreateRotationY(euler.X°)   (cs:81)
  //   heading = CreateRotationX(euler.Y°)   (cs:82)
  //   roll    = CreateRotationZ(euler.Z°)   (cs:83)
  //   R = heading * pitch * roll            (cs:84, System.Numerics `*` = row-vector apply order)
  const float ex = (p.rotation[0] * p.rotationFactor[0] + p.rotationOffset2[0]) * kDegToRad;
  const float ey = (p.rotation[1] * p.rotationFactor[1] + p.rotationOffset2[1]) * kDegToRad;
  const float ez = (p.rotation[2] * p.rotationFactor[2] + p.rotationOffset2[2]) * kDegToRad;
  if (cameraWithRotationBugYawPitchRollOrder())
    // ★-bug: the CreateFromYawPitchRoll order TiXL's comment warns against (cs:73-76) — roll, then
    // pitch(X of euler.Y), then yaw(Y of euler.X) composed the WRONG way round.
    return mat4Mul(mat4Mul(rotZ(ez), rotX(ey)), rotY(ex));
  return mat4Mul(mat4Mul(rotX(ey), rotY(ex)), rotZ(ez));
}

void cameraWithRotationMatrices(const CameraWithRotationParams& p, float fallbackAspect, Mat4& outW2C,
                                Mat4& outC2C) {
  const float aspect = (p.aspectIn < 0.0001f) ? fallbackAspect : p.aspectIn;  // cs:53-57
  outC2C = perspectiveFovRH(p.fovDeg * kDegToRad, aspect, p.clipNear, p.clipFar);  // cs:110
  outC2C.m[8] = p.lensShift[0];  // M31 = LensShift.X (cs:111; row-major m[r*4+c], M31 → m[8])
  outC2C.m[9] = p.lensShift[1];  // M32 = LensShift.Y (cs:112)
  // worldToCamera = CreateTranslation(−Position) * rotationMatrix (cs:114-115, row-vector order).
  const Mat4 r = cameraWithRotationRotation(p);
  Mat4 t = mat4Identity();
  t.m[12] = -p.position[0];
  t.m[13] = -p.position[1];
  t.m[14] = -p.position[2];
  outW2C = mat4Mul(t, r);
}

void cameraWithRotationStamp(const CameraWithRotationParams& p, float outEye[3], float outTarget[3],
                             float outUp[3]) {
  const Mat4 r = cameraWithRotationRotation(p);
  // Columns of the row-major R: col2 (camera z-axis in world) = (m[2],m[6],m[10]); col1 (up) =
  // (m[1],m[5],m[9]). LookAtRH(pos, pos−col2, col1) == T(−pos)·R for orthonormal R (header proof).
  outEye[0] = p.position[0];
  outEye[1] = p.position[1];
  outEye[2] = p.position[2];
  outTarget[0] = p.position[0] - r.m[2];
  outTarget[1] = p.position[1] - r.m[6];
  outTarget[2] = p.position[2] - r.m[10];
  outUp[0] = r.m[1];
  outUp[1] = r.m[5];
  outUp[2] = r.m[9];
}

CameraWithRotationParams readCameraWithRotationParams(const CmdCookCtx& c) {
  CameraWithRotationParams p;
  const float d3z[3] = {0.0f, 0.0f, 0.0f};
  const float d3o[3] = {1.0f, 1.0f, 1.0f};
  const float dPos[3] = {0.0f, 0.0f, 2.4141f};        // .t3 Position default
  const float dQuat[4] = {1.0f, 1.0f, 1.0f, 1.0f};    // .t3 RotationQuaternion default
  const float dClip[2] = {0.01f, 1000.0f};            // .t3 ClipPlanes default
  const float dShift[2] = {0.0f, 0.0f};               // .t3 LensShift default
  cookVecN(c, "Position", dPos, 3, p.position);
  p.rotationMode = (int)cookParam(c, "RotationMode", 0.0f);
  cookVecN(c, "Rotation", d3z, 3, p.rotation);
  cookVecN(c, "RotationFactor", d3o, 3, p.rotationFactor);
  cookVecN(c, "RotationOffset2", d3z, 3, p.rotationOffset2);
  cookVecN(c, "RotationQuaternion", dQuat, 4, p.quaternion);
  p.fovDeg = cookParam(c, "FOV", 45.0f);
  float clip[2];
  cookVecN(c, "ClipPlanes", dClip, 2, clip);
  p.clipNear = clip[0];
  p.clipFar = clip[1];
  p.aspectIn = cookParam(c, "AspectRatio", -1.0f);  // .t3 default -1 → output aspect
  cookVecN(c, "LensShift", dShift, 2, p.lensShift);
  return p;
}

// CameraWithRotation: Command subtree in → Command out. Stamps its camera onto every subtree item
// that has no camera yet (push/pop innermost-wins, the cookCamera mechanism verbatim). Unwired
// Command → empty chain (TiXL: eval an empty subtree). The matrices are built by the EXECUTOR from
// the stamped eye/target/up (LookAtRH) — exact for T(−pos)·R via the decomposition (header proof).
RenderCommand cookCameraWithRotation(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.inputCommand) return rc;
  rc.items = c.inputCommand->items;
  const CameraWithRotationParams p = readCameraWithRotationParams(c);
  float eye[3], tgt[3], up[3];
  cameraWithRotationStamp(p, eye, tgt, up);
  // cs:53-57 fallback snapped at cook time: aspect<0.0001 → stamp 0 → the executor's output aspect
  // (render_command.h camAspect<=0 contract) — so 0<aspect<0.0001 falls back exactly like TiXL.
  const float aspectStamp = (p.aspectIn < 0.0001f) ? 0.0f : p.aspectIn;
  for (RenderDrawItem& it : rc.items) {
    if (it.hasCamera) continue;  // a NESTED camera already stamped this item (innermost wins = pop)
    it.hasCamera = true;
    it.camEye[0] = eye[0]; it.camEye[1] = eye[1]; it.camEye[2] = eye[2];
    it.camTarget[0] = tgt[0]; it.camTarget[1] = tgt[1]; it.camTarget[2] = tgt[2];
    it.camUp[0] = up[0]; it.camUp[1] = up[1]; it.camUp[2] = up[2];
    it.camFovDeg = p.fovDeg;
    it.camNear = p.clipNear;
    it.camFar = p.clipFar;
    it.camAspect = aspectStamp;
    // fork-camerawithrotation-lensshift-drawrail-drop: no lens shift on the stamp (header).
  }
  return rc;
}

void registerCameraWithRotationOp() {
  registerCmdOp("CameraWithRotation", cookCameraWithRotation);
}

}  // namespace sw
