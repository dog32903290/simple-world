// camerawithrotation_golden — GOLDEN for CameraWithRotation (--selftest-camerawithrotation / -bug).
//
// TiXL authority: external/tixl/Operators/Lib/render/camera/CameraWithRotation.cs (:61-90 rotation,
// :110-117 matrices) + CameraWithRotation.t3 (defaults). ORACLES:
//   • hand CONSTANTS for the single-axis Euler probe + one quaternion matrix row (independent of any
//     code, GOLDEN_STANDARD feature 1);
//   • an INDEPENDENT LOCAL TRANSCRIPTION of System.Numerics CreateRotationX/Y/Z /
//     CreateFromQuaternion / row-vector multiply for the multi-axis probes (the fractalsdf-style
//     "TiXL 轉錄 + 真 cook-through" calibration template) — the production path under test is
//     point_ops_camerawithrotation.cpp's builders + the REGISTERED cook fn (cook-through).
//
// Probes sit OFF every identity (feature 2): multi-axis Euler (30,45,40)° THROUGH the
// Rotation·Factor+Offset2 pipeline (cs:66), position (1,−2,3), fov 60°≠45, clip (1,101), lens shift
// (0.2,−0.1) ≠ 0, unit quaternion (1,2,3,4)/√30. A wrong axis order / transposed rotation / dropped
// factor/offset flips them.
//
// injectBug = cameraWithRotationBugYawPitchRollOrder(): the REAL cook path composes the Euler rotation
// in the CreateFromYawPitchRoll order TiXL's own comment warns against (cs:73-76) — the multi-axis
// Euler stamp AND matrices diverge → RED. Wants stay FIXED (no want-flip).
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "runtime/field_camera.h"                  // Mat4 / lookAtRH (the executor's stamp consumer)
#include "runtime/point_ops_camerawithrotation.h"  // the production builders + registered cook fn
#include "runtime/render_command.h"                // RenderCommand / RenderDrawItem
#include "runtime/value_op_registry.h"             // valueOpSelfTests() (self-registered sink)

namespace sw {
namespace {

constexpr float kD2R = 3.14159265358979323846f / 180.0f;

// ── independent local transcription (System.Numerics, row-major row-vector) — the ORACLE side ──
struct M4 {
  float m[16];
};
M4 oIdentity() {
  M4 r{};
  r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
  return r;
}
M4 oMul(const M4& a, const M4& b) {  // row-vector: apply a then b (System.Numerics Multiply(a,b))
  M4 r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) {
      float s = 0.0f;
      for (int k = 0; k < 4; ++k) s += a.m[i * 4 + k] * b.m[k * 4 + j];
      r.m[i * 4 + j] = s;
    }
  return r;
}
M4 oRotX(float rad) {  // System.Numerics CreateRotationX
  M4 r = oIdentity();
  r.m[5] = std::cos(rad); r.m[6] = std::sin(rad);
  r.m[9] = -std::sin(rad); r.m[10] = std::cos(rad);
  return r;
}
M4 oRotY(float rad) {  // System.Numerics CreateRotationY
  M4 r = oIdentity();
  r.m[0] = std::cos(rad); r.m[2] = -std::sin(rad);
  r.m[8] = std::sin(rad); r.m[10] = std::cos(rad);
  return r;
}
M4 oRotZ(float rad) {  // System.Numerics CreateRotationZ
  M4 r = oIdentity();
  r.m[0] = std::cos(rad); r.m[1] = std::sin(rad);
  r.m[4] = -std::sin(rad); r.m[5] = std::cos(rad);
  return r;
}
M4 oQuat(float x, float y, float z, float w) {  // System.Numerics CreateFromQuaternion (no normalize)
  M4 r = oIdentity();
  r.m[0] = 1 - 2 * (y * y + z * z); r.m[1] = 2 * (x * y + w * z); r.m[2] = 2 * (x * z - w * y);
  r.m[4] = 2 * (x * y - w * z); r.m[5] = 1 - 2 * (z * z + x * x); r.m[6] = 2 * (y * z + w * x);
  r.m[8] = 2 * (x * z + w * y); r.m[9] = 2 * (y * z - w * x); r.m[10] = 1 - 2 * (y * y + x * x);
  return r;
}
// expected worldToCamera = CreateTranslation(−pos) * R (cs:114-115)
M4 oW2C(const float pos[3], const M4& r) {
  M4 t = oIdentity();
  t.m[12] = -pos[0]; t.m[13] = -pos[1]; t.m[14] = -pos[2];
  return oMul(t, r);
}

bool near16(const Mat4& got, const M4& want, float eps) {
  for (int i = 0; i < 16; ++i)
    if (std::fabs(got.m[i] - want.m[i]) >= eps) return false;
  return true;
}

int runCameraWithRotationGolden(bool injectBug) {
  const float eps = 1e-4f;
  bool allFaithful = true;

  cameraWithRotationBugYawPitchRollOrder() = injectBug;  // ★-bug: wrong Euler composition (real cook)

  // ── Leg 1: COOK-THROUGH, single-axis Euler, HAND-CONSTANT stamp ─────────────────────────────
  // Rotation=(0,90,0) → euler.Y=90° → R = heading = RotX(90°) (cs:82,84 — pitch/roll identity).
  // RotX(90°) columns: col2 (camera z-axis) = (0, sin90, cos90) = (0,1,0); col1 (up) = (0, cos90,
  // −sin90) = (0,0,−1). Stamp (decomposition): eye=(1,2,3), target = eye − col2 = (1,1,3),
  // up = (0,0,−1) — the camera pitched 90° to look straight down −Y, its up now −Z. Also asserts the
  // .t3 defaults ride the cook: FOV 45, clip (0.01,1000), AspectRatio −1 → camAspect 0 (output aspect).
  {
    std::map<std::string, float> params;
    params["Position.x"] = 1.0f;
    params["Position.y"] = 2.0f;
    params["Position.z"] = 3.0f;
    params["Rotation.y"] = 90.0f;
    RenderCommand in;
    in.items.push_back(RenderDrawItem{});
    CmdCookCtx cc;
    cc.params = &params;
    cc.inputCommand = &in;
    RenderCommand out = cookCameraWithRotation(cc);  // the REGISTERED fn (true cook-through)
    bool ok = out.items.size() == 1;
    if (ok) {
      const RenderDrawItem& it = out.items[0];
      ok = it.hasCamera && std::fabs(it.camEye[0] - 1.0f) < eps &&
           std::fabs(it.camEye[1] - 2.0f) < eps && std::fabs(it.camEye[2] - 3.0f) < eps &&
           std::fabs(it.camTarget[0] - 1.0f) < eps && std::fabs(it.camTarget[1] - 1.0f) < eps &&
           std::fabs(it.camTarget[2] - 3.0f) < eps && std::fabs(it.camUp[0] - 0.0f) < eps &&
           std::fabs(it.camUp[1] - 0.0f) < eps && std::fabs(it.camUp[2] - (-1.0f)) < eps &&
           std::fabs(it.camFovDeg - 45.0f) < eps && std::fabs(it.camNear - 0.01f) < eps &&
           std::fabs(it.camFar - 1000.0f) < eps && std::fabs(it.camAspect - 0.0f) < eps;
      std::printf("[selftest-camerawithrotation] cook-through Rot=(0,90,0) pos=(1,2,3): "
                  "tgt=(%.3f,%.3f,%.3f) want(1,1,3) up=(%.3f,%.3f,%.3f) want(0,0,-1) "
                  "fov=%.1f aspect=%.1f -> %s\n",
                  it.camTarget[0], it.camTarget[1], it.camTarget[2], it.camUp[0], it.camUp[1],
                  it.camUp[2], it.camFovDeg, it.camAspect, ok ? "ok" : "TRIPPED");
    } else {
      std::printf("[selftest-camerawithrotation] cook-through emitted %zu items (want 1) -> TRIPPED\n",
                  out.items.size());
    }
    allFaithful = allFaithful && ok;
  }

  // ── Leg 2: MULTI-AXIS Euler through Rotation·Factor+Offset2 (cs:66) — transcription oracle ──
  // Rotation=(20,50,15)·Factor(1.5,0.5,2)+Offset2(0,20,10) → euler=(30,45,40)° (hand: 20·1.5=30,
  // 50·0.5+20=45, 15·2+10=40). want R = RotX(45°)·RotY(30°)·RotZ(40°) (cs:81-84); want w2c =
  // T(−(1,−2,3))·R. Asserts BOTH the production matrices AND lookAtRH(production stamp) — the
  // executor's exact stamp consumer — reproduce it (the decomposition proof under load).
  {
    CameraWithRotationParams p;
    p.position[0] = 1.0f; p.position[1] = -2.0f; p.position[2] = 3.0f;
    p.rotation[0] = 20.0f; p.rotation[1] = 50.0f; p.rotation[2] = 15.0f;
    p.rotationFactor[0] = 1.5f; p.rotationFactor[1] = 0.5f; p.rotationFactor[2] = 2.0f;
    p.rotationOffset2[1] = 20.0f; p.rotationOffset2[2] = 10.0f;
    p.fovDeg = 60.0f;
    p.clipNear = 1.0f; p.clipFar = 101.0f;
    p.lensShift[0] = 0.2f; p.lensShift[1] = -0.1f;
    const M4 wantR = oMul(oMul(oRotX(45.0f * kD2R), oRotY(30.0f * kD2R)), oRotZ(40.0f * kD2R));
    const M4 wantW2C = oW2C(p.position, wantR);

    Mat4 w2c, c2c;
    cameraWithRotationMatrices(p, /*fallbackAspect*/ 16.0f / 9.0f, w2c, c2c);
    bool mats = near16(w2c, wantW2C, eps);

    // c2c hand elements (GraphicsMath.cs:36-45 + cs:110-112): yScale=1/tan30°=1.7320508,
    // M11=yScale/(16/9)=0.97427857, M33=101/(1−101)=−1.01, M43=−1.01, M31/M32=LensShift=(0.2,−0.1).
    bool proj = std::fabs(c2c.m[0] - 0.97427857f) < eps && std::fabs(c2c.m[5] - 1.7320508f) < eps &&
                std::fabs(c2c.m[10] - (-1.01f)) < eps && std::fabs(c2c.m[14] - (-1.01f)) < eps &&
                std::fabs(c2c.m[8] - 0.2f) < eps && std::fabs(c2c.m[9] - (-0.1f)) < eps &&
                std::fabs(c2c.m[11] - (-1.0f)) < eps;

    float eye[3], tgt[3], up[3];
    cameraWithRotationStamp(p, eye, tgt, up);
    Mat4 viaStamp = lookAtRH(eye, tgt, up);  // what the EXECUTOR rebuilds from the stamp
    bool stamp = near16(viaStamp, wantW2C, eps);

    bool ok = mats && proj && stamp;
    allFaithful = allFaithful && ok;
    std::printf("[selftest-camerawithrotation] euler(30,45,40) pos=(1,-2,3): w2c=%d proj=%d "
                "stampLookAt=%d (w2c[0]=%.5f want %.5f; c2c M31=%.2f M43=%.3f) -> %s\n",
                mats ? 1 : 0, proj ? 1 : 0, stamp ? 1 : 0, w2c.m[0], wantW2C.m[0], c2c.m[8],
                c2c.m[14], ok ? "ok" : "TRIPPED");
  }

  // ── Leg 3: QUATERNION mode, unit q=(1,2,3,4)/√30 — transcription oracle + one HAND row ──
  // Hand row0 (CreateFromQuaternion): M11=1−2(y²+z²)=1−26/30=0.133333, M12=2(xy+wz)=28/30=0.933333,
  // M13=2(xz−wy)=−10/30=−0.333333 (x²,y²,z²,w²=(1,4,9,16)/30).
  {
    const float n = 1.0f / std::sqrt(30.0f);
    CameraWithRotationParams p;
    p.rotationMode = 1;
    p.quaternion[0] = 1.0f * n; p.quaternion[1] = 2.0f * n;
    p.quaternion[2] = 3.0f * n; p.quaternion[3] = 4.0f * n;
    p.position[0] = -2.0f; p.position[1] = 1.0f; p.position[2] = 5.0f;
    const M4 wantR = oQuat(p.quaternion[0], p.quaternion[1], p.quaternion[2], p.quaternion[3]);
    const M4 wantW2C = oW2C(p.position, wantR);
    bool handRow = std::fabs(wantR.m[0] - 0.133333f) < 1e-5f &&
                   std::fabs(wantR.m[1] - 0.933333f) < 1e-5f &&
                   std::fabs(wantR.m[2] - (-0.333333f)) < 1e-5f;  // oracle self-anchor vs hand math

    Mat4 w2c, c2c;
    cameraWithRotationMatrices(p, 1.0f, w2c, c2c);
    bool mats = near16(w2c, wantW2C, eps);
    float eye[3], tgt[3], up[3];
    cameraWithRotationStamp(p, eye, tgt, up);
    Mat4 viaStamp = lookAtRH(eye, tgt, up);
    bool stamp = near16(viaStamp, wantW2C, 1e-4f);  // unit q → orthonormal → decomposition exact

    bool ok = handRow && mats && stamp;
    allFaithful = allFaithful && ok;
    std::printf("[selftest-camerawithrotation] quat(1,2,3,4)/sqrt30 pos=(-2,1,5): handRow=%d w2c=%d "
                "stampLookAt=%d (R00=%.5f want 0.13333) -> %s\n",
                handRow ? 1 : 0, mats ? 1 : 0, stamp ? 1 : 0, wantR.m[0], ok ? "ok" : "TRIPPED");
  }

  cameraWithRotationBugYawPitchRollOrder() = false;  // reset for every later cook in this process

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-camerawithrotation] injectBug did not trip any tooth\n");
      return 0;  // dead tooth → NO-BITE list (GOLDEN_STANDARD polarity)
    }
    std::printf("[selftest-camerawithrotation] injectBug correctly RED (yaw-pitch-roll order bug "
                "flips the multi-axis Euler probe)\n");
    return 1;
  }
  std::printf("[selftest-camerawithrotation] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

struct CameraWithRotationGoldenRegistrar {
  CameraWithRotationGoldenRegistrar() {
    valueOpSelfTests().push_back({"camerawithrotation", runCameraWithRotationGolden});
  }
};
static const CameraWithRotationGoldenRegistrar _reg_camerawithrotation_golden;

}  // namespace
}  // namespace sw
