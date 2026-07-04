// runtime/resident_camera_value_cook — the camera value-output cook-emit pass (header has the story).
//
// TiXL authority: external/tixl/Operators/Lib/render/camera/CamPosition.cs (+ CurrentCamMatrices.cs,
// commit 2). Matrix construction authority: Core/Utils/Geometry/GraphicsMath.cs (LookAtRH /
// PerspectiveFovRH) via field_camera's verbatim ports — the SAME builders the draw-rail executor uses
// for stamped cameras (stampedCameraForward), so the value rail and the draw rail can never disagree
// about what a given Camera node's matrices are.
#include "runtime/resident_camera_value_cook.h"

#include <cmath>
#include <map>
#include <string>

#include "runtime/field_camera.h"            // Mat4 / mat4Inverse / mat4Mul / *CameraForward builders
#include "runtime/graph.h"                   // NodeSpec / PortSpec / findSpec
#include "runtime/point_graph.h"             // registerCmdOp (CmdCookCtx via point_graph_cook_ctx.h)
#include "runtime/point_ops_camera_scope.h"  // ActiveCamera / resolveActiveCamera (one resolve codepath)
#include "runtime/resident_eval_graph.h"     // ResidentEvalGraph / resolveResidentFloatInputs

namespace sw {

bool& cameraValueBugSkipEnclosing() {
  static bool v = false;  // OFF in production; the camera-value golden flips it around a cook then resets
  return v;
}

namespace {

// The camera-family op types whose Command-input subtree runs under THEIR pushed matrices (the
// enclosing-camera writers this walk can terminate on). Camera (isCameraScopeWriter) +
// OrthographicCamera today; CameraWithRotation / BlendCameras join as their lanes land.
bool isEnclosingCameraType(const std::string& opType) {
  return isCameraScopeWriter(opType) || opType == "OrthographicCamera";
}

// True iff `slotId` is a Command-typed OUTPUT port of `spec` (the walk only follows the Command rail —
// a Float wire out of CamPosition.Position.x into some consumer does NOT put the op under that consumer).
bool isCommandOutputSlot(const NodeSpec* spec, const std::string& slotId) {
  if (!spec) return false;
  for (const PortSpec& p : spec->ports)
    if (!p.isInput && p.id == slotId) return p.dataType == "Command";
  return false;
}

// True iff `slotId` is a Command-typed INPUT port of `spec` (the enclosing relation holds only when the
// camera CONSUMES the chain through its Command input — a Float wire into Camera.Position.x does not).
bool isCommandInputSlot(const NodeSpec* spec, const std::string& slotId) {
  if (!spec) return false;
  for (const PortSpec& p : spec->ports)
    if (p.isInput && p.id == slotId) return p.dataType == "Command";
  return false;
}

// STRUCTURAL enclosing-camera walk (fork-camera-value-structural-enclosing-walk, header): from
// `startPath`, follow the first consumer of each node's Command-typed output downstream; the first
// camera-family node consuming through its Command input = the innermost enclosing camera. nullptr =
// top-level (no enclosing camera → default camera), or the walk was severed by the -bug seam.
// Multiple consumers of one Command output: the FIRST found wins (named micro-fork
// fork-camera-value-first-consumer — a fanned-out Command output is evaluated under EACH consumer in
// TiXL; one frame-level value cannot represent both, so the first wire is the one honoured).
const ResidentNode* findEnclosingCamera(const ResidentEvalGraph& g, const std::string& startPath) {
  if (cameraValueBugSkipEnclosing()) return nullptr;  // ★-bug: sever the walk → default camera
  std::string cur = startPath;
  for (int hop = 0; hop < 64; ++hop) {  // depth guard (mirror of evalResidentFloat's cycle break)
    const ResidentNode* curN = g.node(cur);
    if (!curN) return nullptr;
    const NodeSpec* curSpec = findSpec(curN->opType);
    const ResidentNode* consumer = nullptr;
    std::string viaSlotId;
    for (const ResidentNode& x : g.nodes) {
      for (const ResidentInput& ri : x.inputs) {
        if (ri.driver == ResidentInput::Driver::Connection && ri.srcNodePath == cur &&
            isCommandOutputSlot(curSpec, ri.srcSlotId)) {
          consumer = &x;
          viaSlotId = ri.slotId;
          break;
        }
        // MultiInput extra wires (a Group/Execute collecting several Commands) count the same.
        for (const auto& ec : ri.extraConns)
          if (ec.first == cur && isCommandOutputSlot(curSpec, ec.second)) {
            consumer = &x;
            viaSlotId = ri.slotId;
            break;
          }
        if (consumer) break;
      }
      if (consumer) break;
    }
    if (!consumer) return nullptr;  // reached the root → no enclosing camera
    if (isEnclosingCameraType(consumer->opType) &&
        isCommandInputSlot(findSpec(consumer->opType), viaSlotId))
      return consumer;
    cur = consumer->path;  // an intermediate Command node (Group/RotateAroundAxis/…) — keep walking
  }
  return nullptr;
}

// Build the enclosing camera's FORWARD pair (WorldToCamera + CameraToClipSpace) — the matrices TiXL's
// context would carry inside that camera's subtree. cam==nullptr → the DEFAULT camera
// (EvaluationContext.SetDefaultCamera) at the frame's requested aspect. Reuses the SAME builders the
// draw-rail executor uses (stampedCameraForward / defaultLayerCameraForward) — one construction codepath.
void enclosingCameraForward(const ResidentEvalGraph& g, const ResidentNode* cam,
                            const ResidentEvalCtx& ctx, Mat4& outW2C, Mat4& outC2C) {
  const float reqAspect = (ctx.requestedHeight > 0)
                              ? (float)ctx.requestedWidth / (float)ctx.requestedHeight
                              : 1.0f;  // unseeded frame → square (the executor's own fallback shape)
  if (!cam) {
    LayerCameraForward f = defaultLayerCameraForward(reqAspect);
    outW2C = f.worldToCamera;
    outC2C = f.cameraToClipSpace;
    return;
  }
  const std::map<std::string, float> params = resolveResidentFloatInputs(g, *cam, ctx);
  auto p = [&](const char* id, float def) {
    auto it = params.find(id);
    return it != params.end() ? it->second : def;
  };
  if (cam->opType == "OrthographicCamera") {
    // Same param ids + defaults as the OrthographicCamera spec row (node_registry_draw_camera.cpp).
    const float eye[3] = {p("Position.x", -0.0015059264f), p("Position.y", 0.0014562709f),
                          p("Position.z", 10.0f)};
    const float tgt[3] = {p("Target.x", 0.0f), p("Target.y", 0.0f), p("Target.z", 0.0f)};
    const float up[3] = {p("Up.x", 0.0f), p("Up.y", 1.0f), p("Up.z", 0.0f)};
    const float stretch[2] = {p("Stretch.x", 1.0f), p("Stretch.y", 1.0f)};
    const float aspectIn = p("AspectRatio", 0.0f);
    const float aspect = aspectIn < 0.0001f ? reqAspect : aspectIn;  // OrthographicCamera.cs:25-28
    LayerCameraForward f =
        stampedCameraForward(eye, tgt, up, /*ortho*/ true, /*fov dead*/ 45.0f, p("Scale", 1.0f),
                             stretch, aspect, p("NearFarClip.x", 0.1f), p("NearFarClip.y", 1000.0f));
    outW2C = f.worldToCamera;
    outC2C = f.cameraToClipSpace;
    return;
  }
  // Camera — the SAME resolve codepath the C1 scope + cookCamera use (one param semantics).
  const ActiveCamera ac = resolveActiveCamera(params);
  const float aspect = ac.aspect < 0.0001f ? reqAspect : ac.aspect;  // Camera.cs:53-55 fallback
  const float stretch11[2] = {1.0f, 1.0f};
  LayerCameraForward f = stampedCameraForward(ac.eye, ac.target, ac.up, /*ortho*/ false, ac.fovDeg,
                                              1.0f, stretch11, aspect, ac.nearClip, ac.farClip);
  outW2C = f.worldToCamera;
  outC2C = f.cameraToClipSpace;
}

// The camera value ops draw nothing — TiXL's Command slot exists purely as the execution-path hook
// (CamPosition.cs:23-24 "only want to call update for the execution path"); the VALUES ride the
// frame-level pass below. An empty RenderCommand keeps the Command chain walkable through them.
RenderCommand cookCameraValueNoop(CmdCookCtx&) { return RenderCommand{}; }

}  // namespace

void registerCameraValueOps() { registerCmdOp("CamPosition", cookCameraValueNoop); }

void cookCameraValueOutputNodes(ResidentEvalGraph& g, const ResidentEvalCtx& ctx) {
  for (ResidentNode& rn : g.nodes) {
    if (rn.opType != "CamPosition") continue;
    // CamPosition (TiXL Lib.render.camera.CamPosition) — emit the ambient camera's world-space
    // position/direction + the projection aspect. CamPosition.cs:29-38:
    //   Matrix4x4.Invert(context.WorldToCamera, out var camToWorld);            // :29
    //   pos = Vector4.Transform((0,0,0,1), camToWorld); Position = pos.XYZ;     // :31-32
    //   dir = pos - Vector4.Transform((0,0,1,1), camToWorld); Direction = dir.XYZ;  // :34-35 (NOT normalized)
    //   AspectRatio = CameraToClipSpace.M22 / M11;                              // :37
    // Row-vector algebra: (0,0,0,1)·M = row 3 (m[12..14]); (0,0,1,1)·M = row2+row3 → dir = -row2.
    Mat4 w2c, c2c;
    enclosingCameraForward(g, findEnclosingCamera(g, rn.path), ctx, w2c, c2c);
    Mat4 camToWorld;
    mat4Inverse(w2c, camToWorld);
    // Output-port order in the NodeSpec (= extOut index; [0] is the Command output, no float slot):
    //   [1..3] Position.xyz  [4..6] Direction.xyz  [7] AspectRatio.
    rn.extOut[1] = camToWorld.m[12];
    rn.extOut[2] = camToWorld.m[13];
    rn.extOut[3] = camToWorld.m[14];
    rn.extOut[4] = -camToWorld.m[8];
    rn.extOut[5] = -camToWorld.m[9];
    rn.extOut[6] = -camToWorld.m[10];
    rn.extOut[7] = c2c.m[5] / c2c.m[0];  // M22/M11 (row-major m[r*4+c]: M22=m[5], M11=m[0])
  }
}

}  // namespace sw
