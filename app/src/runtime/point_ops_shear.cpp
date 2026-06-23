// Shear command op — TiXL Operators/Lib/render/transform/Shear.cs. A render-island transform-context op
// unlocked by the S2 spine: a thin sibling of Group (S2b) that pushes a SHEAR matrix (not an S·R·T) onto
// context.ObjectToWorld around its single Command subtree. Same per-item group-stamp mechanism as
// Group/Camera/RotateAroundAxis — NO new cook-core code, NO shader, NO seam.
//
// BACKWARD-TRACE (Shear.cs:18-31, Update):
//   var shearing = Translation.GetValue(context);     // Vector3 (X,Y,Z) — named "Translation" in TiXL
//   Matrix4x4 m = Matrix4x4.Identity;
//   m.M12 = shearing.Y;                                // row0 col1
//   m.M21 = shearing.X;                                // row1 col0
//   m.M14 = shearing.Z;                                // row0 col3
//   var prev = context.ObjectToWorld;
//   context.ObjectToWorld = Matrix4x4.Multiply(m, context.ObjectToWorld);   // PUSH (shear · parent)
//   Command.GetValue(context);                          // cook the child with shear pushed
//   context.ObjectToWorld = prev;                       // POP
//
// ★MATRIX MAPPING (the load-bearing detail). System.Numerics Mij is 1-indexed row i, col j; field_camera's
// Mat4 is ROW-MAJOR m[r*4 + c] (0-indexed). So:
//   M12 → m[(1-1)*4 + (2-1)] = m[1]   ← shearing.Y
//   M21 → m[(2-1)*4 + (1-1)] = m[4]   ← shearing.X
//   M14 → m[(1-1)*4 + (4-1)] = m[3]   ← shearing.Z
// In row-vector convention v·M, m[1] couples input x → output y (a y-shear by x·Y), m[4] couples input
// y → output x (an x-shear by y·X), and m[3] writes into the w column (a projective/affine-w shear by
// x·Z — faithful to TiXL's literal M14 assignment, which is unusual but verbatim).
//
// ★INTEGRATION MECHANISM — per-item group stamp (the Group precedent): stamp the shear onto every
// RenderDrawItem the subtree produced, ACCUMULATING into it.groupObjectToWorld (it.group = it.group ·
// thisShear). The executor right-multiplies it into the item's ObjectToWorld. push = stamp; pop = the
// accumulation. Composes WITH Group / RotateAroundAxis (the SAME group slot). Unwired Command ⇒ empty
// chain (TiXL: eval an empty subtree). No IsEnabled (TiXL's op has none).
#include "runtime/point_ops.h"

#include "runtime/field_camera.h"    // Mat4 / mat4Identity / mat4Mul (row-vector, same as Group)
#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp, cookVecN
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem

namespace sw {

// Shear: Command subtree in → Command out. Reads Translation (Vector3 X,Y,Z) = the shear amounts, builds
// the identity-plus-shear matrix (M12=Y, M21=X, M14=Z), and STAMPS it onto every collected item
// (accumulating). Unwired Command ⇒ empty chain.
RenderCommand cookShear(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.inputCommand) return rc;     // no subtree wired → empty (faithful)
  rc.items = c.inputCommand->items;   // COPY the subtree (we re-stamp)

  float shearDef[3] = {0.0f, 0.0f, 0.0f};  // .t3 default (Shear.t3 Translation) → identity (no shear)
  float shear[3];
  cookVecN(c, "Translation", shearDef, 3, shear);

  Mat4 m = mat4Identity();
  m.m[1] = shear[1];  // M12 = shearing.Y
  m.m[4] = shear[0];  // M21 = shearing.X
  m.m[3] = shear[2];  // M14 = shearing.Z

  for (RenderDrawItem& it : rc.items) {
    Mat4 existing{};
    for (int i = 0; i < 16; ++i) existing.m[i] = it.groupObjectToWorld[i];
    Mat4 composed = it.hasGroup ? mat4Mul(existing, m) : m;  // child·existing·thisShear (innermost first)
    for (int i = 0; i < 16; ++i) it.groupObjectToWorld[i] = composed.m[i];
    it.hasGroup = true;
  }
  return rc;
}

void registerShearOp() { registerCmdOp("Shear", cookShear); }

}  // namespace sw
