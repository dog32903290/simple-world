// actioncamera_golden — GOLDEN for ActionCamera (--selftest-actioncamera / -bug).
//
// TiXL authority: external/tixl/Operators/Lib/render/camera/ActionCamera.cs (:19-85) +
// ActionCamera.t3 (defaults). EVERY want is a HAND-DERIVED constant on the 3-4-5 reference camera
// (eye=(3,0,4)→origin): unit viewDirection = (−0.6,0,−0.8) makes every integration step closed-form.
//
// The op is STATEFUL + TEMPORAL (a per-node cross-frame CameraDefinition integrated on the seconds
// clock) — this golden drives the PRODUCTION resolver (resolveActionCameraDefinition, the exact fn
// the BlendCameras cook dispatches to) through a deterministic frame sequence with hand-controlled
// ctx.time/frameIndex, per memory `sw-stateful-node-parity-gap`'s demand for closed-form stateful
// teeth (each step IS closed-form here, so this is a parity golden, not a smoke).
//
// Frames (dt = 0.5s each):
//   F0 t=0.0  uninitialized → blend forced 1 (cs:45-50) → def = reference: pos=(3,0,4),
//             target = pos + forward = (2.4,0,3.2) (Blend rebuilds target from the unit forward,
//             ICamera.cs:60), up=(0,1,0), fov = 60° rad, aspect = 1.5.
//   F1 t=0.5  Forward=2 Speed=1 → pos += viewDir·2·1·0.5 = (−0.6,0,−0.8) → pos=(2.4,0,3.2),
//             target = pos + viewDir = (1.8,0,2.4)   (cs:73,77).
//   F2 t=1.0  Yaw=π RotationSpeed=1 → rotY(−π·1·0.5) = −90° about Y: viewDir (−0.6,0,−0.8) →
//             (0.8,0,−0.6) (cs:66-67); Forward=0 → pos stays (2.4,0,3.2);
//             target = pos + newViewDir = (3.2,0,2.6).
//   F3 t=1.5  TriggerReset rising → blend forced 1 → def snaps to the reference:
//             pos=(3,0,4), target=(2.4,0,3.2)   (cs:43-50, edge-detected reset fork).
// Plus a BlendCameras COOK-THROUGH leg: an ActionCamera ref (fresh state) through the REGISTERED
// cookBlendCameras — the stamp carries the F0 definition (proves the stateful ref dispatch +
// nested-ref currency end to end).
//
// injectBug = actionCameraBugDropViewRotation(): the REAL integrator skips the yaw/pitch view
// rotation (cs:66-71) → F2's target flips (1.8,0,2.4)≠(3.2,0,2.6) → RED. Wants stay FIXED.
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "runtime/eval_context.h"            // EvaluationContext (the hand-controlled clock)
#include "runtime/point_ops_actioncamera.h"  // the production resolver + state reset + -bug seam
#include "runtime/point_ops_blendcameras.h"  // SwCameraDefinition / cookBlendCameras
#include "runtime/render_command.h"          // RenderCommand / RenderDrawItem
#include "runtime/value_op_registry.h"       // valueOpSelfTests() (self-registered sink)

namespace sw {
namespace {

bool v3near(const float got[3], float wx, float wy, float wz, float eps) {
  return std::fabs(got[0] - wx) < eps && std::fabs(got[1] - wy) < eps && std::fabs(got[2] - wz) < eps;
}

int runActionCameraGolden(bool injectBug) {
  const float eps = 1e-4f;
  const float kPi = 3.14159265358979323846f;
  bool allFaithful = true;

  resetActionCameraStateForTest();  // independent trajectory per run (one process runs both legs)
  actionCameraBugDropViewRotation() = injectBug;  // ★-bug: yaw/pitch rotation dropped (real seam)

  // The 3-4-5 reference camera (a plain Camera ref, resolved from params like the drivers hand it).
  std::map<std::string, float> camP;
  camP["Position.x"] = 3.0f; camP["Position.y"] = 0.0f; camP["Position.z"] = 4.0f;
  camP["Target.x"] = 0.0f; camP["Target.y"] = 0.0f; camP["Target.z"] = 0.0f;
  camP["FieldOfView"] = 60.0f;
  camP["AspectRatio"] = 1.5f;
  camP["ClipPlanes.x"] = 1.0f; camP["ClipPlanes.y"] = 101.0f;

  CmdCookCtx::CmdCameraRef camRef;
  camRef.opType = "Camera";
  camRef.params = &camP;
  camRef.nodePath = "cam1";

  std::map<std::string, float> acP;  // mutated per frame (= re-resolved params each frame)
  CmdCookCtx::CmdCameraRef acRef;
  acRef.opType = "ActionCamera";
  acRef.params = &acP;
  acRef.nodePath = "ac1";
  acRef.upstreamRefs.push_back(camRef);

  auto resolveAt = [&](uint32_t frame, float t) {
    EvaluationContext ectx{};
    ectx.frameIndex = frame;
    ectx.time = t;
    SwCameraDefinition def;
    resolveActionCameraDefinition(acRef, &ectx, def);
    return def;
  };

  // ── F0: init snap to the reference (blend forced 1, cs:45-50) ──
  {
    SwCameraDefinition d = resolveAt(0, 0.0f);
    bool ok = v3near(d.position, 3.0f, 0.0f, 4.0f, eps) && v3near(d.target, 2.4f, 0.0f, 3.2f, eps) &&
              v3near(d.up, 0.0f, 1.0f, 0.0f, eps) &&
              std::fabs(d.fovRad - 60.0f * kPi / 180.0f) < 1e-5f &&
              std::fabs(d.aspectRatio - 1.5f) < eps;
    allFaithful = allFaithful && ok;
    std::printf("[selftest-actioncamera] F0 init: pos=(%.3f,%.3f,%.3f) want(3,0,4) tgt=(%.3f,%.3f,"
                "%.3f) want(2.4,0,3.2) fov=%.4f aspect=%.2f -> %s\n",
                d.position[0], d.position[1], d.position[2], d.target[0], d.target[1], d.target[2],
                d.fovRad, d.aspectRatio, ok ? "ok" : "TRIPPED");
  }

  // ── F1: forward flight (cs:73,77) — dt=0.5, Forward=2, Speed=1 ──
  {
    acP["Forward"] = 2.0f;
    acP["Speed"] = 1.0f;
    SwCameraDefinition d = resolveAt(1, 0.5f);
    bool ok = v3near(d.position, 2.4f, 0.0f, 3.2f, eps) && v3near(d.target, 1.8f, 0.0f, 2.4f, eps);
    allFaithful = allFaithful && ok;
    std::printf("[selftest-actioncamera] F1 fly fwd: pos=(%.3f,%.3f,%.3f) want(2.4,0,3.2) tgt=(%.3f,"
                "%.3f,%.3f) want(1.8,0,2.4) -> %s\n",
                d.position[0], d.position[1], d.position[2], d.target[0], d.target[1], d.target[2],
                ok ? "ok" : "TRIPPED");
  }

  // ── F2: yaw −90° (cs:66-71) — dt=0.5, Yaw=π, RotationSpeed=1 → viewDir (−0.6,0,−0.8)→(0.8,0,−0.6) ──
  {
    acP["Forward"] = 0.0f;
    acP["Yaw"] = kPi;
    acP["RotationSpeed"] = 1.0f;
    SwCameraDefinition d = resolveAt(2, 1.0f);
    bool ok = v3near(d.position, 2.4f, 0.0f, 3.2f, eps) && v3near(d.target, 3.2f, 0.0f, 2.6f, eps);
    allFaithful = allFaithful && ok;
    std::printf("[selftest-actioncamera] F2 yaw -90: tgt=(%.3f,%.3f,%.3f) want(3.2,0,2.6) -> %s\n",
                d.target[0], d.target[1], d.target[2], ok ? "ok" : "TRIPPED");
  }

  // ── F3: TriggerReset rising edge (cs:43-50) → snap back to the reference ──
  {
    acP["Yaw"] = 0.0f;
    acP["TriggerReset"] = 1.0f;
    SwCameraDefinition d = resolveAt(3, 1.5f);
    bool ok = v3near(d.position, 3.0f, 0.0f, 4.0f, eps) && v3near(d.target, 2.4f, 0.0f, 3.2f, eps);
    allFaithful = allFaithful && ok;
    std::printf("[selftest-actioncamera] F3 reset: pos=(%.3f,%.3f,%.3f) want(3,0,4) -> %s\n",
                d.position[0], d.position[1], d.position[2], ok ? "ok" : "TRIPPED");
  }

  // ── COOK-THROUGH: BlendCameras consuming the ActionCamera ref (fresh state, F0 semantics) ──
  {
    resetActionCameraStateForTest();
    acP.clear();
    std::map<std::string, float> bp;
    bp["Index"] = 0.0f;
    RenderCommand in;
    in.items.push_back(RenderDrawItem{});
    EvaluationContext ectx{};
    ectx.frameIndex = 0;
    ectx.time = 0.0f;
    CmdCookCtx cc;
    cc.ctx = &ectx;
    cc.params = &bp;
    cc.inputCommand = &in;
    cc.cameraRefs = {acRef};
    RenderCommand out = cookBlendCameras(cc);
    bool got = out.items.size() == 1 && out.items[0].hasCamera;
    bool ok = got && v3near(out.items[0].camEye, 3.0f, 0.0f, 4.0f, eps) &&
              v3near(out.items[0].camTarget, 2.4f, 0.0f, 3.2f, eps) &&
              std::fabs(out.items[0].camFovDeg - 60.0f) < 1e-3f &&
              std::fabs(out.items[0].camAspect - 1.5f) < eps;
    allFaithful = allFaithful && ok;
    if (got)
      std::printf("[selftest-actioncamera] BlendCameras dispatch: eye=(%.3f,%.3f,%.3f) want(3,0,4) "
                  "fov=%.1f aspect=%.2f -> %s\n",
                  out.items[0].camEye[0], out.items[0].camEye[1], out.items[0].camEye[2],
                  out.items[0].camFovDeg, out.items[0].camAspect, ok ? "ok" : "TRIPPED");
    else
      std::printf("[selftest-actioncamera] BlendCameras dispatch emitted no stamped item -> TRIPPED\n");
  }

  actionCameraBugDropViewRotation() = false;  // reset for every later cook in this process
  resetActionCameraStateForTest();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-actioncamera] injectBug did not trip any tooth\n");
      return 0;  // dead tooth → NO-BITE list (GOLDEN_STANDARD polarity)
    }
    std::printf("[selftest-actioncamera] injectBug correctly RED (dropped yaw/pitch view rotation "
                "flips the F2 target)\n");
    return 1;
  }
  std::printf("[selftest-actioncamera] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

struct ActionCameraGoldenRegistrar {
  ActionCameraGoldenRegistrar() {
    valueOpSelfTests().push_back({"actioncamera", runActionCameraGolden});
  }
};
static const ActionCameraGoldenRegistrar _reg_actioncamera_golden;

}  // namespace
}  // namespace sw
