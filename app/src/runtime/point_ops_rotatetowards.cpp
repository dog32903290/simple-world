// RotateTowards command op — TiXL Operators/Lib/render/transform/RotateTowards.cs. A render-island
// transform-context op unlocked by the S2 spine: a LookAt-style sibling of Group/RotateAroundAxis/Shear that
// pushes a ROTATION onto context.ObjectToWorld so the (single) Command subtree FACES a target point. Same
// per-item group-stamp integration mechanism (render_command.h hasGroup/groupObjectToWorld) — NO new cook-core
// code, NO shader, NO seam.
//
// BACKWARD-TRACE (RotateTowards.cs:17-51, Update):
//   var targetMode = LookTowards.GetEnumValue<Modes>(context);   // 0=TowardsCamera, 1=TowardsPosition
//   var targetPos  = AlternativeTarget.GetValue(context);        // Vector3 (.t3 default (0,0,1))
//   Vector3 targetPosDx;
//   if (targetMode == TowardsCamera) {                            // camera-coupled branch (see FORK below)
//       Matrix4x4.Invert(context.WorldToCamera, out var camToWorld);
//       targetPosDx = Vector4.Transform((0,0,0,1), camToWorld).ToVector3();   // camera WORLD position
//   } else { targetPosDx = targetPos; }
//   var sourcePos = Vector4.Transform((0,0,0,1), context.ObjectToWorld).ToVector3();   // op-origin in world
//   var lookAt = GraphicsMath.LookAtRH(Vector3.Zero, -targetPosDx + sourcePos, VectorT3.Up);  // Up=(0,1,0)
//   Matrix4x4.Invert(lookAt, out lookAt);                         // ← the PUSH is the INVERSE of the lookAt
//   var rotateOffset = Matrix4x4.CreateFromYawPitchRoll(RotationOffset.Y°, RotationOffset.X°, RotationOffset.Z°);
//   lookAt = Matrix4x4.Multiply(rotateOffset, lookAt);
//   var prev = context.ObjectToWorld;
//   context.ObjectToWorld = Matrix4x4.Multiply(lookAt, context.ObjectToWorld);   // PUSH (faceRot · parent)
//   Command.GetValue(context); context.ObjectToWorld = prev;     // cook child with the facing rotation, POP
//
// So the matrix this op contributes is:  M = rotateOffset · inverse( LookAtRH(0, sourcePos - targetPosDx, Up) ).
//
// ★INTEGRATION MECHANISM — per-item group stamp (the Group/RotateAroundAxis/Shear precedent): SW is
// retained-mode with a per-item executor; there is no runtime ObjectToWorld scope stack. cookRotateTowards
// builds ONE M from its params and STAMPS it onto every collected item, ACCUMULATING into it.groupObjectToWorld
// (it.group = it.group · M). The executor right-multiplies it into the item's own ObjectToWorld. push = stamp;
// pop = the accumulation (innermost first). Composes WITH Group / RotateAroundAxis / Shear (same group slot).
// Unwired Command ⇒ empty chain (TiXL: eval an empty subtree). No IsEnabled (TiXL's op has none).
//
// ★FORK #1 — sourcePos (named, parity-relevant): TiXL reads sourcePos = origin·context.ObjectToWorld, the
// op-scope's INCOMING world transform. SW's retained per-item model has NO live ObjectToWorld at cook time (the
// upstream context is already baked into the items; this op contributes only the parent push it stamps) — so at
// this scope context.ObjectToWorld is IDENTITY → sourcePos = (0,0,0). Identical assumption to Shear /
// RotateAroundAxis / Transform (they all build a pure relative matrix, ignoring the running ObjectToWorld). For
// a top-level RotateTowards (the common case, and what the golden drives) this is byte-faithful; a RotateTowards
// nested under a translating Group sees the upstream offset via the item's already-stamped groupObjectToWorld
// rather than via sourcePos. Faithful for the un-nested case; named for the nested one.
//
// ★FORK #2 — TowardsCamera mode (named, camera3d-coupled): TiXL's .t3 default LookTowards=0 = TowardsCamera,
// which inverts context.WorldToCamera to find the camera world position. CmdCookCtx threads NO WorldToCamera
// (the camera3d seam is not built), so we resolve the camera origin to TiXL's DEFAULT camera world position
// (0,0,defaultCameraDistance()) — exactly where the un-overridden context.WorldToCamera places the eye
// (field_camera.cpp default camera, GraphicsMath.cs:113-114). When a real camera3d push lands this becomes the
// threaded WorldToCamera. The TowardsPosition mode is fully closed-form and is what the golden pins.
#include "runtime/point_ops.h"

#include <cmath>

#include "runtime/field_camera.h"    // Mat4 / mat4Mul / mat4Identity / lookAtRH / mat4Inverse / defaultCameraDistance
#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp, cookVecN, cookParam
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem

namespace sw {

namespace {
constexpr float kPi = 3.14159265358979323846f;

// Matrix4x4.CreateFromYawPitchRoll(yaw,pitch,roll) — transcribed element-for-element from System.Numerics
// (Quaternion.CreateFromYawPitchRoll → CreateFromQuaternion), ROW-MAJOR / row-vector. VERBATIM copy of the R
// block in point_ops_transform.cpp / groupObjectToWorld so the bytes match Group/Transform exactly. Degrees in.
Mat4 yawPitchRollRowMajor(float yawDeg, float pitchDeg, float rollDeg) {
  float yaw = yawDeg * kPi / 180.0f, pitch = pitchDeg * kPi / 180.0f, roll = rollDeg * kPi / 180.0f;
  float sr = std::sin(roll * 0.5f), cr = std::cos(roll * 0.5f);
  float sp = std::sin(pitch * 0.5f), cp = std::cos(pitch * 0.5f);
  float sy = std::sin(yaw * 0.5f), cy = std::cos(yaw * 0.5f);
  float qx = cy * sp * cr + sy * cp * sr;
  float qy = sy * cp * cr - cy * sp * sr;
  float qz = cy * cp * sr - sy * sp * cr;
  float qw = cy * cp * cr + sy * sp * sr;
  float xx = qx * qx, yy = qy * qy, zz = qz * qz;
  float xy = qx * qy, wz = qz * qw, xz = qz * qx, wy = qy * qw, yz = qy * qz, wx = qx * qw;
  Mat4 R = mat4Identity();
  R.m[0] = 1.0f - 2.0f * (yy + zz); R.m[1] = 2.0f * (xy + wz);        R.m[2] = 2.0f * (xz - wy);
  R.m[4] = 2.0f * (xy - wz);        R.m[5] = 1.0f - 2.0f * (zz + xx); R.m[6] = 2.0f * (yz + wx);
  R.m[8] = 2.0f * (xz + wy);        R.m[9] = 2.0f * (yz - wx);        R.m[10] = 1.0f - 2.0f * (yy + xx);
  return R;
}
}  // namespace

// Build the facing-rotation matrix M = rotateOffset · inverse(LookAtRH(0, sourcePos - target, Up)).
// sourcePos = (0,0,0) (FORK #1). Exposed (not static) so the golden derives the SAME expected matrix from the
// SAME math (no re-derivation drift).
Mat4 rotateTowardsMatrix(const float target[3], const float rotationOffsetDeg[3]) {
  const float eye[3] = {0.0f, 0.0f, 0.0f};
  const float up[3] = {0.0f, 1.0f, 0.0f};                 // VectorT3.Up
  // LookAtRH target arg = -targetPosDx + sourcePos = (0,0,0) - target = -target (RotateTowards.cs:36).
  const float lookTarget[3] = {-target[0], -target[1], -target[2]};
  Mat4 lookAt = lookAtRH(eye, lookTarget, up);
  Mat4 lookAtInv;
  if (!mat4Inverse(lookAt, lookAtInv)) lookAtInv = mat4Identity();  // singular (degenerate target) → identity
  Mat4 rotateOffset =
      yawPitchRollRowMajor(/*yaw=*/rotationOffsetDeg[1], /*pitch=*/rotationOffsetDeg[0], /*roll=*/rotationOffsetDeg[2]);
  return mat4Mul(rotateOffset, lookAtInv);  // rotateOffset · inverse(lookAt) (RotateTowards.cs:45 Multiply order)
}

// RotateTowards: Command subtree in → Command out. Reads LookTowards (enum: 0=TowardsCamera default,
// 1=TowardsPosition), AlternativeTarget (Vector3, .t3 (0,0,1)), RotationOffset (Vector3, .t3 (0,0,0)), builds the
// facing-rotation matrix and STAMPS it onto every collected item (accumulating). Unwired Command ⇒ empty chain.
RenderCommand cookRotateTowards(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.inputCommand) return rc;     // no subtree wired → empty (faithful)
  rc.items = c.inputCommand->items;   // COPY the subtree (we re-stamp)

  float targetDef[3] = {0.0f, 0.0f, 1.0f};  // .t3 default AlternativeTarget (0,0,1)
  float target[3];
  cookVecN(c, "AlternativeTarget", targetDef, 3, target);

  float rotOffDef[3] = {0.0f, 0.0f, 0.0f};  // .t3 default RotationOffset (0,0,0)
  float rotOff[3];
  cookVecN(c, "RotationOffset", rotOffDef, 3, rotOff);

  float lookTowards = cookParam(c, "LookTowards", 0.0f);  // .t3 default 0 = TowardsCamera
  // TowardsCamera (FORK #2): target = default camera world position (0,0,defaultCameraDistance()). Otherwise
  // TowardsPosition: target = AlternativeTarget. (round() so an Enum slider value resolves to the integer mode.)
  if ((int)(lookTowards + 0.5f) == 0) {
    target[0] = 0.0f; target[1] = 0.0f; target[2] = defaultCameraDistance();
  }

  Mat4 m = rotateTowardsMatrix(target, rotOff);

  for (RenderDrawItem& it : rc.items) {
    Mat4 existing{};
    for (int i = 0; i < 16; ++i) existing.m[i] = it.groupObjectToWorld[i];
    Mat4 composed = it.hasGroup ? mat4Mul(existing, m) : m;  // child·existing·thisM (innermost first)
    for (int i = 0; i < 16; ++i) it.groupObjectToWorld[i] = composed.m[i];
    it.hasGroup = true;
  }
  return rc;
}

void registerRotateTowardsOp() { registerCmdOp("RotateTowards", cookRotateTowards); }

}  // namespace sw
