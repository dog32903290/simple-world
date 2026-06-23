// RotateAroundAxis command op — TiXL Operators/Lib/render/transform/RotateAroundAxis.cs. A render-island
// transform-context op unlocked by the S2 spine: it is a thinner sibling of Group (S2b) — instead of a
// full S·R·T it pushes ONE axis-angle rotation onto context.ObjectToWorld around its (single) Command
// subtree. Same per-item group-stamp integration mechanism Group/Camera established (render_command.h
// hasGroup/groupObjectToWorld), so it needs NO new cook-core code, NO shader, NO seam.
//
// BACKWARD-TRACE (RotateAroundAxis.cs:18-28, Update):
//   var vector3 = Axis.GetValue(context);
//   var angle   = Angle.GetValue(context) / 180 * MathF.PI;            // degrees → radians
//   Matrix4x4 m = Matrix4x4.CreateFromAxisAngle(vector3, angle);       // the rotation
//   var prev = context.ObjectToWorld;
//   context.ObjectToWorld = Matrix4x4.Multiply(m, context.ObjectToWorld);   // PUSH (rot · parent)
//   Command.GetValue(context);                                          // cook the child with rot pushed
//   context.ObjectToWorld = prev;                                       // POP
// The child reads context.ObjectToWorld when it builds ObjectToClipSpace, so a child vertex sees
// v·childO2W·axisRot·parent…  (the rotation right-multiplied onto the child's own, identical to Group).
//
// ★INTEGRATION MECHANISM — per-item group stamp (the Group/Camera precedent, point_ops_group.cpp):
// SW is retained-mode with a per-item executor; there is no runtime ObjectToWorld scope stack. So cookRot
// STAMPS its axis-angle matrix onto every RenderDrawItem its subtree produced, ACCUMULATING into
// it.groupObjectToWorld exactly as Group does (it.group = it.group · thisRot). The executor right-multiplies
// it.groupObjectToWorld into the item's own ObjectToWorld (point_ops_rendertarget.cpp Layer2d + Mesh).
// push = stamp; pop = the accumulation IS the pop (an OUTER Group/RotateAroundAxis multiplies onto what an
// INNER one already stamped → innermost-first composition = TiXL's Multiply(outer, Multiply(inner, I))).
// This is the SAME slot Group writes — so RotateAroundAxis composes WITH Group (a Group above this op
// multiplies its SRT onto the rotation this op stamped, and vice-versa). No IsEnabled in TiXL's op (it has
// none) — the rotation is always applied.
//
//   ★MATRIX FORK (named, parity-load-bearing): Matrix4x4.CreateFromAxisAngle is transcribed
//   ELEMENT-FOR-ELEMENT from System.Numerics (ROW-MAJOR, row-vector — the field_camera convention), not
//   approximated. The axis is NOT renormalized (System.Numerics does not normalize either — a non-unit
//   axis scales the rotation, faithful to TiXL; the .t3 default axis is (0,0,1) unit = Z). deg→rad is the same
//   named fork Layer2d/Group already carry.
#include "runtime/point_ops.h"

#include <cmath>

#include "runtime/field_camera.h"    // Mat4 / mat4Mul (row-vector convention, same as Group)
#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp, cookVecN
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem

namespace sw {

// Matrix4x4.CreateFromAxisAngle(axis, angleRad) — System.Numerics reference impl, ROW-MAJOR / row-vector
// (same storage as field_camera's Mat4). axis = (x,y,z); for a UNIT axis this is the standard Rodrigues
// rotation. Transcribed verbatim from System.Numerics.Matrix4x4.CreateFromAxisAngle so the bytes match.
static Mat4 axisAngleRowMajor(float ax, float ay, float az, float angleRad) {
  // System.Numerics: x,y,z = axis; sa=sin, ca=cos; xx,yy,zz,xy,xz,yz products of the axis components.
  float sa = std::sin(angleRad), ca = std::cos(angleRad);
  float xx = ax * ax, yy = ay * ay, zz = az * az;
  float xy = ax * ay, xz = ax * az, yz = ay * az;
  Mat4 m = mat4Identity();
  m.m[0]  = xx + ca * (1.0f - xx);
  m.m[1]  = xy - ca * xy + sa * az;
  m.m[2]  = xz - ca * xz - sa * ay;
  m.m[4]  = xy - ca * xy - sa * az;
  m.m[5]  = yy + ca * (1.0f - yy);
  m.m[6]  = yz - ca * yz + sa * ax;
  m.m[8]  = xz - ca * xz + sa * ay;
  m.m[9]  = yz - ca * yz - sa * ax;
  m.m[10] = zz + ca * (1.0f - zz);
  return m;
}

// RotateAroundAxis: Command subtree in → Command out. Reads Axis (Vector3) + Angle (deg), builds the
// axis-angle matrix, and STAMPS it onto every collected item (accumulating, so nesting composes — same as
// Group). Unwired Command ⇒ empty chain (TiXL: eval an empty subtree). No IsEnabled gate (TiXL has none).
RenderCommand cookRotateAroundAxis(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.inputCommand) return rc;     // no subtree wired → empty (faithful)
  rc.items = c.inputCommand->items;   // COPY the subtree (we re-stamp)

  float axisDef[3] = {0.0f, 0.0f, 1.0f};  // .t3 default Axis (0,0,1) — Z (RotateAroundAxis.t3)
  float axis[3];
  cookVecN(c, "Axis", axisDef, 3, axis);
  float angleDeg = cookParam(c, "Angle", 0.0f);     // .t3 default 0 → identity rotation
  float angleRad = angleDeg / 180.0f * 3.14159265358979323846f;

  Mat4 rot = axisAngleRowMajor(axis[0], axis[1], axis[2], angleRad);

  for (RenderDrawItem& it : rc.items) {
    Mat4 existing{};
    for (int i = 0; i < 16; ++i) existing.m[i] = it.groupObjectToWorld[i];
    Mat4 composed = it.hasGroup ? mat4Mul(existing, rot) : rot;  // child·existing·thisRot (innermost first)
    for (int i = 0; i < 16; ++i) it.groupObjectToWorld[i] = composed.m[i];
    it.hasGroup = true;
  }
  return rc;
}

void registerRotateAroundAxisOp() { registerCmdOp("RotateAroundAxis", cookRotateAroundAxis); }

}  // namespace sw
