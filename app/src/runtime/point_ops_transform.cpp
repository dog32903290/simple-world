// Transform command op — TiXL Operators/Lib/render/transform/Transform.cs. The render-island full-TRS
// transform with a PIVOT — the general sibling of Group (S2b): Group is Transform with pivot=0 plus a
// color/enable wrapper, so Transform reuses the SAME per-item group-stamp mechanism and adds only the
// scaling/rotation-center (pivot). NO new cook-core code, NO shader, NO seam.
//
// BACKWARD-TRACE (Transform.cs:24-50, Update):
//   var pivot = Pivot.GetValue(context);
//   var s = Scale.GetValue(context) * UniformScale.GetValue(context);   // 3D scale × uniform (like Group)
//   var r = Rotation.GetValue(context);  yaw=r.Y°, pitch=r.X°, roll=r.Z° → radians
//   var t = Translation.GetValue(context);
//   var objectToParentObject = GraphicsMath.CreateTransformationMatrix(
//       scalingCenter:  pivot, scalingRotation: Quaternion.Identity, scaling: s,
//       rotationCenter: pivot, rotation: CreateFromYawPitchRoll(yaw,pitch,roll), translation: t);
//   var prev = context.ObjectToWorld;
//   context.ObjectToWorld = Matrix4x4.Multiply(objectToParentObject, context.ObjectToWorld);  // PUSH
//   Command.GetValue(context);                                                                 // child
//   context.ObjectToWorld = prev;                                                              // POP
//
// ★PIVOT MATH (the ONLY thing beyond Group). GraphicsMath.CreateTransformationMatrix (GraphicsMath.cs:56-97)
// with scalingRotation = Identity (so its matrix and inverse are Identity and drop out) and
// scalingCenter == rotationCenter == pivot collapses to (row-vector, left-to-right apply order):
//   M = T(-pivot) · S · [T(+pivot) · T(-pivot)] · R · T(+pivot) · T(translation)
//     = T(-pivot) · S · R · T(+pivot) · T(translation)        ← the middle T(+p)·T(-p) cancels
// For pivot=0 this is S·R·T(translation) = groupObjectToWorld (verified against Group.cs). The pivot
// recenters scale+rotation about `pivot` before the final translate.
//   FORK (named): TransformCallback (Transform.cs:27 — an editor UI gizmo hook) is dropped (editor-only,
//   exactly like Group's ForceColorUpdate/profiling drops). deg→rad is the Layer2d/Group named fork.
//   Rotation is CreateFromYawPitchRoll(yaw=Y,pitch=X,roll=Z) transcribed element-for-element from
//   System.Numerics (ROW-MAJOR row-vector) — identical to groupObjectToWorld's R block (parity authority).
//
// ★INTEGRATION MECHANISM — per-item group stamp (the Group precedent): stamp M onto every RenderDrawItem
// the subtree produced, ACCUMULATING into it.groupObjectToWorld. The executor right-multiplies it into the
// item's own ObjectToWorld. push = stamp; pop = accumulation. Composes WITH Group / RotateAroundAxis /
// Shear (the SAME group slot). Unwired Command ⇒ empty chain. No IsEnabled (TiXL's op has none — unlike
// Group, which DOES gate; Transform always applies).
#include "runtime/point_ops.h"

#include <cmath>

#include "runtime/field_camera.h"    // Mat4 / mat4Identity / mat4Mul (row-vector, same as Group)
#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp, cookParam/cookVecN
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem

namespace sw {

namespace {
constexpr float kPi = 3.14159265358979323846f;

// R = CreateFromQuaternion(CreateFromYawPitchRoll(yaw,pitch,roll)) — transcribed element-for-element from
// System.Numerics, ROW-MAJOR row-vector. VERBATIM copy of groupObjectToWorld's R block (field_camera.cpp:
// 261-276) so the bytes match Group exactly. yaw=Y, pitch=X, roll=Z (degrees in → radians here).
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

Mat4 translateRowMajor(float tx, float ty, float tz) {
  Mat4 T = mat4Identity();
  T.m[12] = tx; T.m[13] = ty; T.m[14] = tz;
  return T;
}
}  // namespace

// Transform: Command subtree in → Command out. Builds M = T(-pivot)·S·R·T(+pivot)·T(translation) and
// STAMPS it onto every collected item (accumulating, so nesting composes). Unwired Command ⇒ empty chain.
RenderCommand cookTransform(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.inputCommand) return rc;     // no subtree wired → empty (faithful)
  rc.items = c.inputCommand->items;   // COPY the subtree (we re-stamp)

  // TiXL Transform.cs inputs. Scale default (1,1,1); UniformScale default 1; Rotation (0,0,0); Translation
  // (0,0,0); Pivot (0,0,0). scaling = Scale * UniformScale (cs:30).
  float scaleDef[3] = {1.0f, 1.0f, 1.0f};
  float scale[3];
  cookVecN(c, "Scale", scaleDef, 3, scale);
  float uniform = cookParam(c, "UniformScale", 1.0f);
  scale[0] *= uniform; scale[1] *= uniform; scale[2] *= uniform;
  float rotDef[3] = {0.0f, 0.0f, 0.0f};  // (X=pitch, Y=yaw, Z=roll), degrees
  float rot[3];
  cookVecN(c, "Rotation", rotDef, 3, rot);
  float transDef[3] = {0.0f, 0.0f, 0.0f};
  float trans[3];
  cookVecN(c, "Translation", transDef, 3, trans);
  float pivotDef[3] = {0.0f, 0.0f, 0.0f};
  float pivot[3];
  cookVecN(c, "Pivot", pivotDef, 3, pivot);

  Mat4 S = mat4Identity();
  S.m[0] = scale[0]; S.m[5] = scale[1]; S.m[10] = scale[2];
  Mat4 R = yawPitchRollRowMajor(/*yaw=*/rot[1], /*pitch=*/rot[0], /*roll=*/rot[2]);
  Mat4 Tneg = translateRowMajor(-pivot[0], -pivot[1], -pivot[2]);
  Mat4 Tpos = translateRowMajor(pivot[0], pivot[1], pivot[2]);
  Mat4 Tt = translateRowMajor(trans[0], trans[1], trans[2]);

  // M = T(-pivot) · S · R · T(+pivot) · T(translation)  (row-vector, left→right apply order = mat4Mul)
  Mat4 m = mat4Mul(mat4Mul(mat4Mul(mat4Mul(Tneg, S), R), Tpos), Tt);

  for (RenderDrawItem& it : rc.items) {
    Mat4 existing{};
    for (int i = 0; i < 16; ++i) existing.m[i] = it.groupObjectToWorld[i];
    Mat4 composed = it.hasGroup ? mat4Mul(existing, m) : m;  // child·existing·thisM (innermost first)
    for (int i = 0; i < 16; ++i) it.groupObjectToWorld[i] = composed.m[i];
    it.hasGroup = true;
  }
  return rc;
}

void registerTransformOp() { registerCmdOp("Transform", cookTransform); }

}  // namespace sw
