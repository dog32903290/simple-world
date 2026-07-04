// OrbitCamera command op — see point_ops_orbitcamera.h (backward-trace + forks). Golden lives in
// point_ops_orbitcamera_golden.cpp.
// TiXL authority: external/tixl/Operators/Lib/render/camera/OrbitCamera.cs (GUID 6415ed0e) +
// Core/Utils/MathUtils.cs:16-41 (PerlinNoise, via the anim_math.h verbatim port).
#include "runtime/point_ops_orbitcamera.h"

#include "runtime/anim_math.h"       // anim_math::perlinNoise — VERBATIM MathUtils.cs:16-41 port
#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem

#include <cmath>
#include <map>
#include <string>

#include "runtime/tixl_point.h"  // EvaluationContext (localFxTime, the :56 LocalFxTime clock)

namespace sw {
namespace {
constexpr float kOrbitToRad = 3.14159265358979323846f / 180.0f;  // MathUtils.ToRad = (float)(PI/180.0)

float orbitParam(const std::map<std::string, float>& m, const char* id, float def) {
  auto it = m.find(id);
  return it != m.end() ? it->second : def;
}

// Vector3.Transform/TransformNormal by Matrix4x4.CreateFromYawPitchRoll(yaw, pitch, roll=0):
// System.Numerics applies roll FIRST ("about axis the object is facing"), then pitch, then yaw —
// with roll=0 the active rotation is Ry(yaw)·Rx(pitch)·v (right-handed).
void orbitYawPitchRotate(const float v[3], float yaw, float pitch, float out[3]) {
  const float cp = std::cos(pitch), sp = std::sin(pitch);
  const float x1 = v[0];
  const float y1 = v[1] * cp - v[2] * sp;   // Rx(pitch) active
  const float z1 = v[1] * sp + v[2] * cp;
  const float cy = std::cos(yaw), sy = std::sin(yaw);
  out[0] = x1 * cy + z1 * sy;               // Ry(yaw) active
  out[1] = y1;
  out[2] = -x1 * sy + z1 * cy;
}

// Vector3.TransformNormal by Matrix4x4.CreateFromAxisAngle(axis, angle) — Rodrigues, right-handed;
// OrbitCamera.cs:117 feeds the NORMALIZED adjustedViewDirection as the axis.
void orbitAxisAngleRotate(const float v[3], const float k[3], float ang, float out[3]) {
  const float c = std::cos(ang), s = std::sin(ang);
  const float kv = k[0] * v[0] + k[1] * v[1] + k[2] * v[2];
  const float cr[3] = {k[1] * v[2] - k[2] * v[1], k[2] * v[0] - k[0] * v[2],
                       k[0] * v[1] - k[1] * v[0]};
  for (int i = 0; i < 3; ++i) out[i] = v[i] * c + cr[i] * s + k[i] * kv * (1.0f - c);
}

void orbitNormalize(float v[3]) {
  const float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (len > 0.0f) { v[0] /= len; v[1] /= len; v[2] /= len; }
}
}  // namespace

// OrbitCamera.cs:50-155 UpdateCameraDefinition at Damping=0 (fork-orbitcamera-damping-dropped).
ActiveCamera resolveOrbitCamera(const std::map<std::string, float>& p, float localFxTime) {
  // :55-56 time (fork-orbitcamera-overridetime-nonzero: non-zero constant overrides the bars clock)
  const float overrideTime = orbitParam(p, "OverrideTime", 0.0f);
  const float time = overrideTime != 0.0f ? overrideTime : localFxTime;
  const float fovDeg = orbitParam(p, "FOV", 45.0f);           // :58 (stamped in DEGREES; executor ToRad)
  const float aspect = orbitParam(p, "AspectRatio", 0.0f);    // :59-63 (<=0 → executor output aspect)
  const float clipN = orbitParam(p, "NearFarClip.x", 0.01f);  // :67
  const float clipF = orbitParam(p, "NearFarClip.y", 1000.0f);
  const float dist = orbitParam(p, "DistanceToTarget", 3.0f);
  const float radius = (dist == 0.0f) ? 0.0001f : dist;       // :71 (avoid 0 radius)
  const int seed = (int)orbitParam(p, "Seed", 0.0f);          // :74 (int-on-float fork)
  const float wobbleSpeed = orbitParam(p, "WobbleSpeed", 0.2f);  // :75
  int wobbleComplexity = (int)orbitParam(p, "WobbleComplexity", 2.0f);  // :76 Clamp(1,8)
  wobbleComplexity = wobbleComplexity < 1 ? 1 : (wobbleComplexity > 8 ? 8 : wobbleComplexity);
  const float rotOff[3] = {orbitParam(p, "RotationOffset.x", 0.0f),   // :78
                           orbitParam(p, "RotationOffset.y", 0.0f),
                           orbitParam(p, "RotationOffset.z", 0.0f)};
  const float center[3] = {orbitParam(p, "CameraTargetPosition.x", 0.0f),  // :81-82
                           orbitParam(p, "CameraTargetPosition.y", 0.0f),
                           orbitParam(p, "CameraTargetPosition.z", 0.0f)};
  // :143-154 ComputeAngle(angleAndWobble, seedIndex): wobble off below the 0.001 amplitude epsilon.
  auto computeAngle = [&](const char* xId, const char* yId, float xDef, float yDef, int seedIndex) {
    const float ax = orbitParam(p, xId, xDef);
    const float ay = orbitParam(p, yId, yDef);
    const float wobble =
        std::fabs(ay) < 0.001f
            ? 0.0f
            : (anim_math::perlinNoise(time * wobbleSpeed, 1.0f, wobbleComplexity,
                                      seed + 123 * seedIndex) - 0.5f) * 2.0f * ay;
    return kOrbitToRad * (ax + wobble);
  };
  // :84-88 orbit yaw: spin angle+wobble, the SpinRate·time rotation, SpinOffset, and the CONSTANT
  // per-seed Perlin yaw term PerlinNoise(0,1,6,seed)·360 (present even at all-zero inputs).
  const float spinOffset = orbitParam(p, "SpinOffset", 0.0f);
  const float orbitYaw =
      computeAngle("SpinAngleAndWobble.x", "SpinAngleAndWobble.y", 0.0f, 0.0f, 1) +
      kOrbitToRad * ((orbitParam(p, "SpinRate", 0.05f) * time) * 360.0f + spinOffset +
                     anim_math::perlinNoise(0.0f, 1.0f, 6, seed) * 360.0f);
  const float orbitPitch =
      -computeAngle("OrbitAngleAndWobble.x", "OrbitAngleAndWobble.y", 30.0f, 5.0f, 2);  // :89
  const float p0[3] = {0.0f, 0.0f, radius};  // :72
  float eye[3];
  orbitYawPitchRotate(p0, orbitYaw, orbitPitch, eye);  // :90-95 (orbits the ORIGIN, not the center)
  float viewDir[3] = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};  // :98
  const float viewYaw =
      computeAngle("AimYawAngleAndWobble.x", "AimYawAngleAndWobble.y", 0.0f, 0.0f, 3) +
      rotOff[0] * kOrbitToRad;  // :100
  const float viewPitch =
      computeAngle("AimPitchAngleAndWobble.x", "AimPitchAngleAndWobble.y", 0.0f, 0.0f, 4) +
      rotOff[1] * kOrbitToRad;  // :101
  float adjDir[3];
  orbitYawPitchRotate(viewDir, viewYaw, viewPitch, adjDir);  // :102-107 (TransformNormal)
  orbitNormalize(adjDir);                                    // :108
  float up[3] = {orbitParam(p, "Up.x", 0.0f), orbitParam(p, "Up.y", 1.0f),
                 orbitParam(p, "Up.z", 0.0f)};  // :114
  const float roll =
      computeAngle("AimRollAngleAndWobble.x", "AimRollAngleAndWobble.y", 0.0f, 5.0f, 5) +
      rotOff[2] * kOrbitToRad;  // :115
  float upR[3];
  orbitAxisAngleRotate(up, adjDir, roll, upR);  // :117-118 (axis = adjusted view direction)
  orbitNormalize(upR);                          // :119
  const float posOff[3] = {orbitParam(p, "PositionOffset.x", 0.0f),  // :65
                           orbitParam(p, "PositionOffset.y", 0.0f),
                           orbitParam(p, "PositionOffset.z", 0.0f)};
  float posOffR[3];
  orbitYawPitchRotate(posOff, orbitYaw, orbitPitch, posOffR);  // :121 TransformNormal(offset, ORBIT rot)
  ActiveCamera cam;
  cam.active = true;
  for (int i = 0; i < 3; ++i) {
    cam.eye[i] = eye[i] + posOffR[i];             // :121
    cam.target[i] = cam.eye[i] + adjDir[i];       // :123 (Damping=0 → :125-126 lerp is identity)
    cam.up[i] = upR[i];
  }
  cam.fovDeg = fovDeg;
  cam.nearClip = clipN;
  cam.farClip = clipF;
  cam.aspect = aspect;
  return cam;
}

// cookOrbitCamera: Command subtree in → Command out; resolve the orbit camera once, stamp every
// !hasCamera subtree item (the cookCamera innermost-wins push — the executor rebuilds LookAtRH +
// PerspectiveFovRH from these raw params, :69/:138).
RenderCommand cookOrbitCamera(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.inputCommand) return rc;  // no subtree wired → empty (TiXL: eval an empty Command)
  rc.items = c.inputCommand->items;
  static const std::map<std::string, float> kEmptyOrbitParams;
  const ActiveCamera cam = resolveOrbitCamera(c.params ? *c.params : kEmptyOrbitParams,
                                              c.ctx ? c.ctx->localFxTime : 0.0f);
  for (RenderDrawItem& it : rc.items) {
    if (it.hasCamera) continue;  // a NESTED camera already stamped this item (innermost wins = pop)
    it.hasCamera = true;
    it.camEye[0] = cam.eye[0]; it.camEye[1] = cam.eye[1]; it.camEye[2] = cam.eye[2];
    it.camTarget[0] = cam.target[0]; it.camTarget[1] = cam.target[1]; it.camTarget[2] = cam.target[2];
    it.camUp[0] = cam.up[0]; it.camUp[1] = cam.up[1]; it.camUp[2] = cam.up[2];
    it.camFovDeg = cam.fovDeg;
    it.camNear = cam.nearClip;
    it.camFar = cam.farClip;
    it.camAspect = cam.aspect;  // <=0 → executor output aspect (Camera.cs:53-55 fallback shape)
  }
  return rc;
}

void registerOrbitCameraOp() { registerCmdOp("OrbitCamera", cookOrbitCamera); }

}  // namespace sw
