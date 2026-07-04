// camera_value_golden — GOLDEN for the camera value-output family (resident_camera_value_cook.cpp):
// CamPosition (--selftest-camposition / -bug). CurrentCamMatrices joins in its own commit.
//
// TiXL authority: external/tixl/Operators/Lib/render/camera/CamPosition.cs (:29-38) +
// Core/Utils/Geometry/GraphicsMath.cs (LookAtRH :10-22, PerspectiveFovRH :27-56,
// DefaultCameraDistance :114). EVERY expected value below is HAND-DERIVED from those sources
// (independent-of-impl, GOLDEN_STANDARD feature 1) — no sw helper is called to produce a want.
//
// Model: resident_value_output_cook.cpp's runRequestedResolutionGolden (the no-evaluate extOut
// readback golden) — cook through the production emit pass, read extOut, cross-check
// evalResidentFloat agreement, assert hand-derived wants.
#include <cmath>
#include <cstdio>

#include "runtime/resident_camera_value_cook.h"  // cookCameraValueOutputNodes + the -bug seam
#include "runtime/resident_eval_graph.h"         // ResidentEvalGraph / evalResidentFloat
#include "runtime/value_op_registry.h"           // valueOpSelfTests() (self-registered sink)

namespace sw {
namespace {

// One Constant-driven Float input row (the resident projection of a .t3 param value).
ResidentInput constIn(const char* slotId, float v) {
  ResidentInput ri;
  ri.slotId = slotId;
  ri.driver = ResidentInput::Driver::Constant;
  ri.constant = v;
  return ri;
}
// One Connection-driven input row (an upstream wire).
ResidentInput connIn(const char* slotId, const char* srcPath, const char* srcSlot) {
  ResidentInput ri;
  ri.slotId = slotId;
  ri.driver = ResidentInput::Driver::Connection;
  ri.srcNodePath = srcPath;
  ri.srcSlotId = srcSlot;
  return ri;
}

// --- CamPosition GOLDEN --------------------------------------------------------------------------
// Probes sit OFF every identity (GOLDEN_STANDARD feature 2): eye=(3,0,4) ≠ default (0,0,2.414…),
// FOV=60° ≠ 45°, AspectRatio=1.5 ≠ 1, and the Command chain crosses an INTERMEDIATE node
// (RotateAroundAxis) so the transitive enclosing-camera walk is exercised, not just the direct wire.
//
// HAND-DERIVED WANTS (CamPosition.cs:29-38 + GraphicsMath.cs LookAtRH :10-22):
//   • Position = camToWorld·(0,0,0,1) = the camera EYE in world = (3,0,4).
//     (LookAtRH's inverse maps the camera-space origin back to the eye — geometric invariant.)
//   • Direction = Position − camToWorld·(0,0,1,1) = −zAxis, zAxis = normalize(eye−target)
//     = normalize(3,0,4) = (0.6,0,0.8)  [3-4-5 triangle] → Direction = (−0.6, 0, −0.8).
//     (NOT normalized by TiXL; here |eye−target| cancels only in the unit zAxis — the op emits −zAxis.)
//   • AspectRatio = CameraToClipSpace.M22/M11 (cs:37); PerspectiveFovRH has M11 = yScale/aspect,
//     M22 = yScale (GraphicsMath.cs:36-45) → M22/M11 = aspect = 1.5 exactly (fov/near/far cancel).
//
// DEFAULT leg (no enclosing camera — TiXL SetDefaultCamera): eye = (0,0,DefaultCameraDistance),
// DefaultCameraDistance = 1/tan(45°·π/360) = 2.41421356 (GraphicsMath.cs:114); target = origin →
// Direction = (0,0,−1); AspectRatio = requested 1280/720 = 1.77777….
//
// injectBug = SEVER the enclosing-camera walk (cameraValueBugSkipEnclosing, the true cook seam in
// resident_camera_value_cook.cpp findEnclosingCamera) → the enclosed CamPosition reads the DEFAULT
// camera → Position flips (3,0,4)→(0,0,2.414…), Direction (−0.6,0,−0.8)→(0,0,−1), Aspect 1.5→16/9
// → the SAME asserts go RED. Wants stay FIXED (no want-flip).
int runCamPositionGolden(bool injectBug) {
  const float eps = 1e-4f;
  bool allFaithful = true;

  ResidentEvalCtx ctx;
  ctx.requestedWidth = 1280u;
  ctx.requestedHeight = 720u;

  // ── Leg 1: ENCLOSED — CamPosition("1") ─Command→ RotateAroundAxis("2") ─out→ Camera("3") ──
  {
    ResidentEvalGraph g;
    ResidentNode cp;
    cp.path = "1";
    cp.opType = "CamPosition";
    ResidentNode rot;
    rot.path = "2";
    rot.opType = "RotateAroundAxis";
    rot.inputs.push_back(connIn("command", "1", "Command"));
    ResidentNode cam;
    cam.path = "3";
    cam.opType = "Camera";
    cam.inputs.push_back(connIn("command", "2", "out"));
    cam.inputs.push_back(constIn("Position.x", 3.0f));
    cam.inputs.push_back(constIn("Position.y", 0.0f));
    cam.inputs.push_back(constIn("Position.z", 4.0f));
    cam.inputs.push_back(constIn("FieldOfView", 60.0f));
    cam.inputs.push_back(constIn("AspectRatio", 1.5f));
    g.nodes.push_back(cp);
    g.nodes.push_back(rot);
    g.nodes.push_back(cam);
    g.byPath["1"] = 0;
    g.byPath["2"] = 1;
    g.byPath["3"] = 2;

    cameraValueBugSkipEnclosing() = injectBug;  // ★-bug: sever the walk (real cook-path corrosion)
    cookCameraValueOutputNodes(g, ctx);
    cameraValueBugSkipEnclosing() = false;  // reset for every later cook in this process

    const float* o = g.nodes[0].extOut;  // [1..3] Position [4..6] Direction [7] AspectRatio
    const float wantPos[3] = {3.0f, 0.0f, 4.0f};
    const float wantDir[3] = {-0.6f, 0.0f, -0.8f};
    const float wantAR = 1.5f;
    bool pos = std::fabs(o[1] - wantPos[0]) < eps && std::fabs(o[2] - wantPos[1]) < eps &&
               std::fabs(o[3] - wantPos[2]) < eps;
    bool dir = std::fabs(o[4] - wantDir[0]) < eps && std::fabs(o[5] - wantDir[1]) < eps &&
               std::fabs(o[6] - wantDir[2]) < eps;
    bool ar = std::fabs(o[7] - wantAR) < eps;

    // Readback leg: evalResidentFloat must AGREE with extOut on every Float output port (the
    // no-evaluate extOut channel a downstream Float wire actually resolves through).
    bool evalAgrees = std::fabs(evalResidentFloat(g, "1", "Position.x", ctx) - o[1]) < eps &&
                      std::fabs(evalResidentFloat(g, "1", "Direction.z", ctx) - o[6]) < eps &&
                      std::fabs(evalResidentFloat(g, "1", "AspectRatio", ctx) - o[7]) < eps;

    bool leg = pos && dir && ar && evalAgrees;
    allFaithful = allFaithful && leg;
    std::printf("[selftest-camposition] enclosed(eye=(3,0,4) fov=60 ar=1.5 via RotateAroundAxis): "
                "Pos=(%.4f,%.4f,%.4f) want(3,0,4) Dir=(%.4f,%.4f,%.4f) want(-0.6,0,-0.8) AR=%.4f "
                "want 1.5 evalAgrees=%d -> %s\n",
                o[1], o[2], o[3], o[4], o[5], o[6], o[7], evalAgrees ? 1 : 0,
                leg ? "ok" : "TRIPPED");
  }

  // ── Leg 2: DEFAULT camera — a bare CamPosition with NO Command consumer (top-level) ──
  // (Cooked WITHOUT the bug flag: this leg is the severed-walk endpoint itself, so the -bug run must
  //  leave it green — the tooth is leg 1's divergence, not a global scramble.)
  {
    ResidentEvalGraph g;
    ResidentNode cp;
    cp.path = "1";
    cp.opType = "CamPosition";
    g.nodes.push_back(cp);
    g.byPath["1"] = 0;
    cookCameraValueOutputNodes(g, ctx);
    const float* o = g.nodes[0].extOut;
    const float wantZ = 2.41421356f;  // 1/tan(45°·π/360), GraphicsMath.cs:114
    const float wantAR = 1280.0f / 720.0f;
    bool leg = std::fabs(o[1] - 0.0f) < eps && std::fabs(o[2] - 0.0f) < eps &&
               std::fabs(o[3] - wantZ) < eps && std::fabs(o[4] - 0.0f) < eps &&
               std::fabs(o[5] - 0.0f) < eps && std::fabs(o[6] - (-1.0f)) < eps &&
               std::fabs(o[7] - wantAR) < eps;
    allFaithful = allFaithful && leg;
    std::printf("[selftest-camposition] default(top-level, req 1280x720): Pos=(%.4f,%.4f,%.5f) "
                "want(0,0,2.41421) Dir=(%.4f,%.4f,%.4f) want(0,0,-1) AR=%.5f want %.5f -> %s\n",
                o[1], o[2], o[3], o[4], o[5], o[6], o[7], wantAR, leg ? "ok" : "TRIPPED");
  }

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-camposition] injectBug did not trip any tooth\n");
      return 0;  // dead tooth → NO-BITE list (GOLDEN_STANDARD polarity)
    }
    std::printf("[selftest-camposition] injectBug correctly RED (severed walk → default camera "
                "values under a wired Camera)\n");
    return 1;
  }
  std::printf("[selftest-camposition] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

// Golden registrar — DIRECT push into the live valueOpSelfTests() sink (no shared-file edit;
// mirror of RequestedResolutionGoldenRegistrar, resident_value_output_cook.cpp).
struct CameraValueGoldenRegistrar {
  CameraValueGoldenRegistrar() {
    valueOpSelfTests().push_back({"camposition", runCamPositionGolden});
  }
};
static const CameraValueGoldenRegistrar _reg_camera_value_golden;

}  // namespace
}  // namespace sw
