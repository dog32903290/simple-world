// blendcameras_golden — GOLDEN for BlendCameras (--selftest-blendcameras / -bug).
//
// TiXL authority: external/tixl/Operators/Lib/render/camera/BlendCameras.cs (:24-106) +
// Core/Operator/Interfaces/ICamera.cs (Blend :38-73, BuildProjectionMatrices :110-130). EVERY want
// below is a HAND-DERIVED constant (GOLDEN_STANDARD feature 1) — same-orientation blends collapse the
// slerp to closed form; the different-orientation leg's midpoint is the exact RotY(45°) by symmetry.
//
// Probes sit OFF identities (feature 2): fractional Index 0.4/1.3 (the blend window + fractional
// weight), fov 60/90 ≠ default, non-unit-distance eyes, a 3-4-5 oblique single-camera round-trip, and
// a genuine 90°-apart orientation slerp midpoint. The cook-through drives the REGISTERED fn with a
// hand CmdCookCtx.cameraRefs (the camera-A gather seam's currency).
//
// injectBug = blendCamerasBugDropFraction(): the REAL cook drops the fractional blend (cs:74's frac →
// 0, camA only) → every fractional-blend want flips to the camA endpoint → RED. Wants stay FIXED.
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "runtime/point_ops_blendcameras.h"  // cookBlendCameras + the -bug seam
#include "runtime/render_command.h"          // RenderCommand / RenderDrawItem
#include "runtime/value_op_registry.h"       // valueOpSelfTests() (self-registered sink)

namespace sw {
namespace {

// A Camera op's resolved params (the map the drivers' CameraRef gather hands the cook).
std::map<std::string, float> cameraParams(float ex, float ey, float ez, float tx, float ty, float tz,
                                          float fovDeg, float aspect, float nearC, float farC) {
  std::map<std::string, float> m;
  m["Position.x"] = ex; m["Position.y"] = ey; m["Position.z"] = ez;
  m["Target.x"] = tx; m["Target.y"] = ty; m["Target.z"] = tz;
  m["FieldOfView"] = fovDeg;
  m["AspectRatio"] = aspect;
  m["ClipPlanes.x"] = nearC; m["ClipPlanes.y"] = farC;
  return m;
}

// Cook BlendCameras through the REGISTERED fn with the given refs + Index, one stampable item in.
RenderDrawItem cookOneItem(const std::vector<CmdCookCtx::CmdCameraRef>& refs, float index,
                           bool& gotItem) {
  std::map<std::string, float> params;
  params["Index"] = index;
  RenderCommand in;
  in.items.push_back(RenderDrawItem{});
  CmdCookCtx cc;
  cc.params = &params;
  cc.inputCommand = &in;
  cc.cameraRefs = refs;
  RenderCommand out = cookBlendCameras(cc);
  gotItem = out.items.size() == 1 && out.items[0].hasCamera;
  return gotItem ? out.items[0] : RenderDrawItem{};
}

bool v3near(const float got[3], float wx, float wy, float wz, float eps) {
  return std::fabs(got[0] - wx) < eps && std::fabs(got[1] - wy) < eps && std::fabs(got[2] - wz) < eps;
}

int runBlendCamerasGolden(bool injectBug) {
  const float eps = 1e-4f;
  bool allFaithful = true;

  blendCamerasBugDropFraction() = injectBug;  // ★-bug: fractional blend dropped (real cook seam)

  // ── Leg 1: SAME-orientation fractional blend, ALL wants hand constants ──────────────────────
  // A: eye=(0,0,5)→(0,0,0), fov 60, aspect 1.5, clip (1,101); B: eye=(4,0,5)→(4,0,0), fov 90,
  // aspect 2, clip (2,52). Both look down −Z with up Y → identical orientations → the slerp is the
  // identity and Blend reduces to pure lerps (ICamera.cs:42,64-68): Index 0.4 →
  //   eye = (1.6,0,5); target = eye + forward(−Z) = (1.6,0,4); up = (0,1,0);
  //   fov = lerp(60°,90°,0.4) = 72° (radian-lerp == degree-lerp, linear);
  //   near = 1.4; far = 101+(52−101)·0.4 = 81.4; aspect = lerp(1.5,2,0.4) = 1.7 (both explicit).
  {
    auto pa = cameraParams(0, 0, 5, 0, 0, 0, 60.0f, 1.5f, 1.0f, 101.0f);
    auto pb = cameraParams(4, 0, 5, 4, 0, 0, 90.0f, 2.0f, 2.0f, 52.0f);
    bool got = false;
    RenderDrawItem it = cookOneItem({{"Camera", &pa}, {"Camera", &pb}}, 0.4f, got);
    bool ok = got && v3near(it.camEye, 1.6f, 0.0f, 5.0f, eps) &&
              v3near(it.camTarget, 1.6f, 0.0f, 4.0f, eps) && v3near(it.camUp, 0.0f, 1.0f, 0.0f, eps) &&
              std::fabs(it.camFovDeg - 72.0f) < 1e-3f && std::fabs(it.camNear - 1.4f) < eps &&
              std::fabs(it.camFar - 81.4f) < 1e-3f && std::fabs(it.camAspect - 1.7f) < eps;
    allFaithful = allFaithful && ok;
    std::printf("[selftest-blendcameras] lerp f=0.4: eye=(%.3f,%.3f,%.3f) want(1.6,0,5) fov=%.2f "
                "want 72 near=%.2f far=%.2f aspect=%.2f want 1.4/81.4/1.7 -> %s\n",
                it.camEye[0], it.camEye[1], it.camEye[2], it.camFovDeg, it.camNear, it.camFar,
                it.camAspect, ok ? "ok" : "TRIPPED");
  }

  // ── Leg 2: the INDEX WINDOW (cs:31-32) — 3 refs, Index 1.3 → pair (1,2), blend 0.3 ──────────
  // Cameras at x = 0 / 10 / 20 (each looking down −Z): blended eye.x = 10 + 10·0.3 = 13.
  {
    auto p0 = cameraParams(0, 0, 5, 0, 0, 0, 45, 1, 0.01f, 1000);
    auto p1 = cameraParams(10, 0, 5, 10, 0, 0, 45, 1, 0.01f, 1000);
    auto p2 = cameraParams(20, 0, 5, 20, 0, 0, 45, 1, 0.01f, 1000);
    bool got = false;
    RenderDrawItem it = cookOneItem({{"Camera", &p0}, {"Camera", &p1}, {"Camera", &p2}}, 1.3f, got);
    bool ok = got && v3near(it.camEye, 13.0f, 0.0f, 5.0f, eps);
    allFaithful = allFaithful && ok;
    std::printf("[selftest-blendcameras] index=1.3 of 3: eye.x=%.3f want 13 -> %s\n", it.camEye[0],
                ok ? "ok" : "TRIPPED");
  }

  // ── Leg 3: SINGLE ref (cs:44-56 camA=camB) — 3-4-5 oblique orientation round-trip ───────────
  // One Camera eye=(3,0,4)→origin: the def round-trips through extract-quaternion → slerp(q,q) →
  // rebuild. zAxis = (0.6,0,0.8) [3-4-5] → stamp: eye=(3,0,4), target = eye − zAxis = (2.4,0,3.2),
  // up = (0,1,0). A corrupted quaternion extraction/rebuild breaks these constants.
  {
    auto pa = cameraParams(3, 0, 4, 0, 0, 0, 60.0f, 1.5f, 1.0f, 101.0f);
    bool got = false;
    RenderDrawItem it = cookOneItem({{"Camera", &pa}}, 0.0f, got);
    bool ok = got && v3near(it.camEye, 3.0f, 0.0f, 4.0f, eps) &&
              v3near(it.camTarget, 2.4f, 0.0f, 3.2f, eps) && v3near(it.camUp, 0.0f, 1.0f, 0.0f, eps) &&
              std::fabs(it.camFovDeg - 60.0f) < 1e-3f && std::fabs(it.camAspect - 1.5f) < eps;
    allFaithful = allFaithful && ok;
    std::printf("[selftest-blendcameras] single(3,0,4): tgt=(%.3f,%.3f,%.3f) want(2.4,0,3.2) -> %s\n",
                it.camTarget[0], it.camTarget[1], it.camTarget[2], ok ? "ok" : "TRIPPED");
  }

  // ── Leg 4: GENUINE orientation slerp — 90° apart, midpoint = RotY(45°) by symmetry ──────────
  // A: eye=(0,0,5)→origin (forward −Z, q=identity); B: eye=(5,0,0)→origin (forward −X, q=RotY 90°).
  // slerp(qa,qb,0.5) = RotY(45°) exactly → forward = (−√2/2, 0, −√2/2); pos = lerp = (2.5,0,2.5) →
  //   target = pos + forward = (1.7928932, 0, 1.7928932); up = (0,1,0).
  // Both refs' AspectRatio unauthored (Camera spec default 0 = output aspect) → stamp camAspect = 0
  // (fork-blendcameras-mixed-aspect-fallback, both-default == TiXL).
  {
    auto pa = cameraParams(0, 0, 5, 0, 0, 0, 45, 0, 0.01f, 1000);
    auto pb = cameraParams(5, 0, 0, 0, 0, 0, 45, 0, 0.01f, 1000);
    pa.erase("AspectRatio");  // unauthored → the resolver's spec default (0)
    pb.erase("AspectRatio");
    bool got = false;
    RenderDrawItem it = cookOneItem({{"Camera", &pa}, {"Camera", &pb}}, 0.5f, got);
    const float k = 0.70710678f;
    bool ok = got && v3near(it.camEye, 2.5f, 0.0f, 2.5f, eps) &&
              v3near(it.camTarget, 2.5f - k, 0.0f, 2.5f - k, eps) &&
              v3near(it.camUp, 0.0f, 1.0f, 0.0f, eps) && std::fabs(it.camAspect - 0.0f) < eps;
    allFaithful = allFaithful && ok;
    std::printf("[selftest-blendcameras] slerp 90-apart f=0.5: tgt=(%.5f,%.3f,%.5f) want(1.79289,0,"
                "1.79289) aspect=%.1f want 0 -> %s\n",
                it.camTarget[0], it.camTarget[1], it.camTarget[2], it.camAspect,
                ok ? "ok" : "TRIPPED");
  }

  // ── Leg 5: ERROR legs (cs:38-42, :51-55) — no refs / unsupported ref → subtree NOT evaluated ──
  {
    bool got = false;
    RenderDrawItem it = cookOneItem({}, 0.0f, got);
    (void)it;
    bool emptyOnNone = !got;
    std::map<std::string, float> junk;
    bool got2 = false;
    RenderDrawItem it2 = cookOneItem({{"DrawPoints", &junk}}, 0.0f, got2);
    (void)it2;
    bool emptyOnJunk = !got2;
    bool ok = emptyOnNone && emptyOnJunk;
    allFaithful = allFaithful && ok;
    std::printf("[selftest-blendcameras] error legs: none→empty=%d notacamera→empty=%d -> %s\n",
                emptyOnNone ? 1 : 0, emptyOnJunk ? 1 : 0, ok ? "ok" : "TRIPPED");
  }

  blendCamerasBugDropFraction() = false;  // reset for every later cook in this process

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-blendcameras] injectBug did not trip any tooth\n");
      return 0;  // dead tooth → NO-BITE list (GOLDEN_STANDARD polarity)
    }
    std::printf("[selftest-blendcameras] injectBug correctly RED (dropped fractional blend → "
                "camA-endpoint values)\n");
    return 1;
  }
  std::printf("[selftest-blendcameras] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

struct BlendCamerasGoldenRegistrar {
  BlendCamerasGoldenRegistrar() {
    valueOpSelfTests().push_back({"blendcameras", runBlendCamerasGolden});
  }
};
static const BlendCamerasGoldenRegistrar _reg_blendcameras_golden;

}  // namespace
}  // namespace sw
