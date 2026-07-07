// runtime/point_ops_getscreenpos — GetScreenPos command op + value-rail Position latch.
// TiXL authority: external/tixl/Operators/Lib/render/analyze/GetScreenPos.cs (+ .t3 defaults).
//
// TiXL GetScreenPos projects a WORLD position through the ambient camera into screen space
// (GetScreenPos.cs:17-53, Update):
//   1. view = (LocalPosition, 1) · (ObjectToWorld · WorldToCamera)                    (cs:19-23)
//   2. clip = view · CameraToClipSpace                                                (cs:26)
//   3. ndc  = clip.xyz / clip.w                                                       (cs:29-33)
//   4. aspectRatio = M22/M11;  screen = (ndc.x·aspect, ndc.y, SetDepthToZero?0:ndc.z) (cs:36-44)
// and publishes it on the Vector3 Position slot (cs:47-52). .t3 defaults: LocalPosition=(0,0,0),
// SetDepthToZero=true.
//
// ★ASPECT EXACTNESS (why sw builds the projection with aspect=1): with PerspectiveFovRH,
// M11 = yScale/aspect and M22 = yScale, so step 4's ndc.x·(M22/M11) = view.x·yScale/w for ANY
// aspect — the aspect cancels exactly. Building the matrix with aspect=1 bakes that product, so the
// output is byte-equal to TiXL's for every output aspect. ndc.y / ndc.z never see the aspect.
//
// ★CROSS-RAIL MECHANISM (Command cook → value rail): TiXL's UpdateCommand is evaluated inside the
// render graph (where the camera context is live) and writes Position.Value for value-rail
// consumers. sw mirrors that with a HOST LATCH: the cmd cook (inside the C1 LiveCameraScope, so it
// sees the wired Camera — cc.hasCamera/worldToCamera, camera_scope.h) computes the screen pos into
// a process-scoped latch; the value rail reads it through the stateful-value sink (StatefulOpReg →
// cookStatefulValueNodes writes extOut[1..3] = the Position.x/y/z spec port indices).
// NAMED FORKS:
//   fork-getscreenpos-frame-latch: cookStatefulValueNodes runs BEFORE the render cook (frame_cook
//     run order), so the value rail reads the PREVIOUS frame's projection (the KeepPreviousFrame
//     1-frame class; TiXL propagates within the eval via ForceInvalidate).
//   fork-getscreenpos-single-latch: ONE process latch (the logMessageCurrentText precedent) — the
//     resident cmd cook carries no per-instance key (cc.nodeId=0); multiple GetScreenPos instances
//     share the last-cooked value. Debug/analyze op, v1.
//   fork-getscreenpos-identity-o2w: ObjectToWorld = Identity (the camera_scope.h v1 fork — sw's cmd
//     rail has no ObjectToWorld scope; a Group-nested GetScreenPos ignores the group transform).
//   fork-getscreenpos-perspective-only: the live scope is written by the perspective Camera only
//     (isCameraScopeWriter); an OrthographicCamera-wrapped GetScreenPos sees the default camera.
//   The cs:45 "changed > 0.0001" gate is a dirty-flag optimization, value-identical → latch written
//   unconditionally (micro-fork).
#include "runtime/point_ops_getscreenpos.h"

#include <cmath>
#include <map>
#include <string>

#include "runtime/field_camera.h"           // Mat4 / mat4Mul / perspectiveFovRH / defaultLayerCameraForward
#include "runtime/point_graph.h"            // CmdCookCtx, registerCmdOp, cookParam/cookVecN
#include "runtime/point_ops_camera_scope.h" // liveActiveCamera (projection params of the wired Camera)
#include "runtime/view_camera_active.h"      // phase-B: activeViewCameraForward (default camera OR output override)
#include "runtime/render_command.h"         // RenderCommand
#include "runtime/stateful_value_op_registry.h"  // StatefulOpReg (value-rail step self-registration)
#include "runtime/stateful_value_ops.h"          // StatefulValueState/TransportSnapshot/ContextVarMap decls

namespace sw {

bool& getScreenPosDropDivideForTest() {
  static bool f = false;
  return f;
}

namespace {

// The process-scoped Position latch (see header fork notes).
float (&screenPosLatch())[3] {
  static float v[3] = {0.0f, 0.0f, 0.0f};
  return v;
}

constexpr float kPi = 3.14159265358979323846f;

// GetScreenPos (TiXL render/analyze/GetScreenPos.cs): Command SOURCE — emits NO draw items (the
// UpdateCommand slot exists so the projection runs inside the render eval, where the camera lives).
RenderCommand cookGetScreenPos(CmdCookCtx& c) {
  RenderCommand rc;
  float zero3[3] = {0.0f, 0.0f, 0.0f};
  float lp[3];
  cookVecN(c, "LocalPosition", zero3, 3, lp);                          // .t3 default (0,0,0)
  const bool depthZero = cookParam(c, "SetDepthToZero", 1.0f) > 0.5f;  // .t3 default true

  // Camera pair: the C1 live Camera (cc.hasCamera — the driver surfaced its WorldToCamera; fov/near/
  // far off the live scope) else the DEFAULT camera (EvaluationContext.SetDefaultCamera: eye=(0,0,
  // cot22.5°), fov=45°, near=0.01, far=1000 — defaultLayerCameraForward). aspect=1 is EXACT (header).
  Mat4 w2c;
  Mat4 proj;
  if (c.hasCamera) {
    for (int i = 0; i < 16; ++i) w2c.m[i] = c.worldToCamera[i];
    const ActiveCamera* lc = liveActiveCamera();
    const float fovDeg = (lc && lc->active) ? lc->fovDeg : 45.0f;
    const float zn = (lc && lc->active) ? lc->nearClip : 0.01f;
    const float zf = (lc && lc->active) ? lc->farClip : 1000.0f;
    proj = perspectiveFovRH(fovDeg * (kPi / 180.0f), 1.0f, zn, zf);
  } else {
    // phase-B: the DEFAULT-camera fallback reads the OUTPUT override when engaged so GetScreenPos tracks the
    // orbited output view (aspect=1 preserved — the aspect factor is baked into the aspect=1 projection, per
    // the note above). Override-absent → defaultLayerCameraForward(1.0f). The c.hasCamera branch wins first.
    LayerCameraForward d = activeViewCameraForward(1.0f);
    w2c = d.worldToCamera;
    proj = d.cameraToClipSpace;
  }

  // 1-2. world → view → clip (cs:19-26; ObjectToWorld = Identity, fork-getscreenpos-identity-o2w).
  Mat4 vp = mat4Mul(w2c, proj);  // row-vector: v·(WorldToCamera·CameraToClipSpace)
  const float cx = lp[0] * vp.m[0] + lp[1] * vp.m[4] + lp[2] * vp.m[8] + vp.m[12];
  const float cy = lp[0] * vp.m[1] + lp[1] * vp.m[5] + lp[2] * vp.m[9] + vp.m[13];
  const float cz = lp[0] * vp.m[2] + lp[1] * vp.m[6] + lp[2] * vp.m[10] + vp.m[14];
  const float cw = lp[0] * vp.m[3] + lp[1] * vp.m[7] + lp[2] * vp.m[11] + vp.m[15];
  // 3. perspective division (cs:29-33). -bug: division dropped (raw clip flows) — the golden tooth.
  const float invW = (getScreenPosDropDivideForTest() || cw == 0.0f) ? 1.0f : 1.0f / cw;
  // 4. screen (cs:36-44): aspect factor baked into the aspect=1 projection (header exactness note).
  float (&latch)[3] = screenPosLatch();
  latch[0] = cx * invW;
  latch[1] = cy * invW;
  latch[2] = depthZero ? 0.0f : cz * invW;
  return rc;  // no draw items (cs: Update computes, draws nothing)
}

// Value-rail step (the stateful-value sink): Position.x/y/z sit at spec port indices 1..3
// (UpdateCommand is port 0), and cookStatefulValueNodes copies out[i] → extOut[i] absolutely —
// so the latch lands on out[1..3]. Reads no inputs, no state (a pure latch read).
void stepGetScreenPos(const std::map<std::string, float>&, float, float, StatefulValueState&,
                      float out[8], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const float (&latch)[3] = screenPosLatch();
  out[1] = latch[0];
  out[2] = latch[1];
  out[3] = latch[2];
}

StatefulOpReg _reg_GetScreenPosStep{"GetScreenPos", stepGetScreenPos};

}  // namespace

void registerGetScreenPosOp() { registerCmdOp("GetScreenPos", cookGetScreenPos); }

}  // namespace sw
