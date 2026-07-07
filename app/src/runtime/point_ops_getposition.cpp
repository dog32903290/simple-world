// runtime/point_ops_getposition — GetPosition command op + value-rail Position latch.
// TiXL authority: external/tixl/Operators/Lib/flow/context/GetPosition.cs (+ .t3 defaults).
//
// TiXL GetPosition transforms a PositionOffset through the eval-context transform scope, selected by the
// Space enum (GetPosition.cs:39-51):
//   matrix = Space switch:                                                            (cs:39-45)
//     WorldSpace  → context.ObjectToWorld
//     CameraSpace → context.WorldToCamera * context.ObjectToWorld
//     ClipSpace   → context.CameraToClipSpace * context.WorldToCamera * context.ObjectToWorld
//   p = (Offset.X, Offset.Y, Offset.Z, 1);  pInSpace = Vector4.Transform(p, matrix);  (cs:47-49)
//   newPosition = (pInSpace.X, pInSpace.Y, pInSpace.Z);   // NO perspective divide by W (cs:51)
// and publishes it on the Vector3 Position slot (cs:29-34 UpdateReturnResults echoes _lastPosition). .t3
// defaults: Space=WorldSpace(0), PositionOffset=(0,0,0).
//
// ★ROW-VECTOR CONVENTION (HLSL mul(m,v) → MSL v·M rule): TiXL uses row-vector algebra — `Vector4.Transform
// (p, M)` = p·M, and left-to-right `A * B * C` composition applies A then B then C. sw's field_camera math
// is ALSO row-major row-vector, so:
//   CameraSpace: p · (ObjectToWorld · WorldToCamera)  = p · WorldToCamera            (O2W = Identity, fork)
//   ClipSpace:   p · (ObjectToWorld · WorldToCamera · CameraToClipSpace)             (= mat4Mul(w2c, c2c))
// This is the SAME mat4Mul(w2c, proj) chain GetScreenPos builds — GetPosition just SKIPS the perspective
// division GetScreenPos does (cs:51 keeps raw clip xyz).
//
// ★CROSS-RAIL MECHANISM (Command cook → value rail): identical to GetScreenPos (point_ops_getscreenpos.cpp).
// TiXL's UpdateCommand is evaluated inside the render graph (where the transform scope is live) and writes
// Position.Value for value-rail consumers. sw mirrors that with a HOST LATCH: the cmd cook (inside the C1
// LiveCameraScope, so it sees the wired Camera — cc.hasCamera/worldToCamera) computes the transformed
// position into a process-scoped latch; the value rail reads it through the stateful-value sink (StatefulOpReg
// → cookStatefulValueNodes writes extOut[1..3] = the Position.x/y/z spec port indices).
// NAMED FORKS (shared with GetScreenPos):
//   fork-getposition-frame-latch: cookStatefulValueNodes runs BEFORE the render cook, so the value rail
//     reads the PREVIOUS frame's transform (the KeepPreviousFrame 1-frame class). This IS the TiXL
//     `_lastPosition` cross-frame store (cs:70-72) — the latch persists between frames.
//   fork-getposition-single-latch: ONE process latch (cc.nodeId=0 in the resident cmd cook) — multiple
//     GetPosition instances share the last-cooked value (the GetScreenPos/logMessageCurrentText precedent).
//   fork-getposition-identity-o2w: ObjectToWorld = Identity (sw's cmd rail has no ObjectToWorld scope) — a
//     Group-nested GetPosition ignores the group transform. In WorldSpace this makes Position = the raw
//     PositionOffset (faithful to the common top-level case where O2W is Identity anyway).
//   The cs:52-56 "Distance > 0.00001 → ForceInvalidate" gate is a dirty-flag optimization (value-identical)
//     → the latch is written unconditionally (micro-fork, same as GetScreenPos's cs:45 gate).
#include "runtime/point_ops_getposition.h"

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

bool& getPositionForceIdentityForTest() {
  static bool f = false;
  return f;
}

namespace {

// The process-scoped Position latch (= TiXL _lastPosition, the cross-frame store; see header fork notes).
float (&positionLatch())[3] {
  static float v[3] = {0.0f, 0.0f, 0.0f};
  return v;
}

constexpr float kPi = 3.14159265358979323846f;

// GetPosition (TiXL flow/context/GetPosition.cs): Command SOURCE — emits NO draw items (the UpdateCommand
// slot exists so the transform read runs inside the render eval, where the scope lives).
RenderCommand cookGetPosition(CmdCookCtx& c) {
  RenderCommand rc;
  float zero3[3] = {0.0f, 0.0f, 0.0f};
  float off[3];
  cookVecN(c, "PositionOffset", zero3, 3, off);        // .t3 default (0,0,0)
  const int space = (int)(cookParam(c, "Space", 0.0f) + 0.5f);  // 0=World 1=Camera 2=Clip (.t3 default 0)

  // Build the Space matrix (row-vector p·M). WorldSpace = Identity (ObjectToWorld = Identity, the fork);
  // CameraSpace = WorldToCamera; ClipSpace = WorldToCamera · CameraToClipSpace. The camera pair matches
  // GetScreenPos: the C1 live Camera (cc.hasCamera) else the DEFAULT camera. aspect=1 (the projection's
  // aspect never affects a position that is NOT perspective-divided; the x-scale it introduces is the
  // faithful TiXL clip-space x).
  Mat4 m;
  for (int i = 0; i < 16; ++i) m.m[i] = (i % 5 == 0) ? 1.0f : 0.0f;  // Identity (WorldSpace)
  if ((space == 1 || space == 2) && !getPositionForceIdentityForTest()) {
    Mat4 w2c;
    if (c.hasCamera) {
      for (int i = 0; i < 16; ++i) w2c.m[i] = c.worldToCamera[i];
    } else {
      // phase-B: the DEFAULT-camera fallback now reads the OUTPUT override when engaged (aspect=1 preserved —
      // a position is NOT perspective-divided, so aspect never matters here). Override-absent → identical to
      // defaultLayerCameraForward(1.0f). The c.hasCamera branch (a wired Camera op) still wins first.
      LayerCameraForward d = activeViewCameraForward(1.0f);
      w2c = d.worldToCamera;
    }
    if (space == 1) {
      m = w2c;  // CameraSpace: p · WorldToCamera
    } else {
      Mat4 proj;
      if (c.hasCamera) {
        const ActiveCamera* lc = liveActiveCamera();
        const float fovDeg = (lc && lc->active) ? lc->fovDeg : 45.0f;
        const float zn = (lc && lc->active) ? lc->nearClip : 0.01f;
        const float zf = (lc && lc->active) ? lc->farClip : 1000.0f;
        proj = perspectiveFovRH(fovDeg * (kPi / 180.0f), 1.0f, zn, zf);
      } else {
        // phase-B: ClipSpace projection fallback pulls the OUTPUT override's CameraToClipSpace when engaged
        // (aspect=1 preserved, per the header exactness note). Override-absent → defaultLayerCameraForward(1).
        LayerCameraForward d = activeViewCameraForward(1.0f);
        proj = d.cameraToClipSpace;
      }
      m = mat4Mul(w2c, proj);  // ClipSpace: p · (WorldToCamera · CameraToClipSpace)
    }
  }

  // p = (offset, 1) · M — take xyz, NO perspective divide (cs:51 keeps raw clip xyz).
  float (&latch)[3] = positionLatch();
  latch[0] = off[0] * m.m[0] + off[1] * m.m[4] + off[2] * m.m[8] + m.m[12];
  latch[1] = off[0] * m.m[1] + off[1] * m.m[5] + off[2] * m.m[9] + m.m[13];
  latch[2] = off[0] * m.m[2] + off[1] * m.m[6] + off[2] * m.m[10] + m.m[14];
  return rc;  // no draw items (cs: Update computes, draws nothing)
}

// Value-rail step (the stateful-value sink): Position.x/y/z sit at spec port indices 1..3 (UpdateCommand is
// port 0), and cookStatefulValueNodes copies out[i] → extOut[i] absolutely — so the latch lands on
// out[1..3]. Reads no inputs, no state (a pure latch read, the GetScreenPos precedent).
void stepGetPosition(const std::map<std::string, float>&, float, float, StatefulValueState&,
                     float out[8], const TransportSnapshot&, ContextVarMap*, const std::string&) {
  const float (&latch)[3] = positionLatch();
  out[1] = latch[0];
  out[2] = latch[1];
  out[3] = latch[2];
}

StatefulOpReg _reg_GetPositionStep{"GetPosition", stepGetPosition};

}  // namespace

void registerGetPositionOp() { registerCmdOp("GetPosition", cookGetPosition); }

}  // namespace sw
