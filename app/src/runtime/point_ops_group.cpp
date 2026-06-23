// Group command op — TiXL Operators/Lib/render/transform/Group.cs. The FINAL S2 stage (S2a Execute
// collector + S2c layer-compose already landed): Execute PLUS an SRT transform-context push. Group
// collects N child Command chains (the SAME MultiInput Command collector the cook core grew for Execute —
// Group.Commands is a MultiInput Command, identical wire-order gather, so S2b adds NO cook-core gather
// code) AND wraps them with its scale/rotate/translate so every child layer composes WITH the group
// transform applied. This is the transform-context half of the render island (transform/ + camera/ ops).
// The render golden lives in point_ops_group_golden.cpp (rule-4 split).
//
// BACKWARD-TRACE (Group.cs:38-82, Update):
//   var s = Scale * UniformScale;  var r = Rotation;  var t = Translation;
//   var objectToParentObject = CreateTransformationMatrix(scalingCenter:0, scalingRotation:Identity,
//       scaling:(s.X,s.Y,s.Z), rotationCenter:0, rotation:CreateFromYawPitchRoll(r.Y,r.X,r.Z),
//       translation:(t.X,t.Y,t.Z));                                  // = S·R·T (GraphicsMath.cs:84-96)
//   var prev = context.ObjectToWorld;
//   context.ObjectToWorld = Matrix4x4.Multiply(objectToParentObject, prev);   // PUSH (group · parent)
//   if (IsEnabled && color.W > 0)
//     foreach (cmd in Commands.CollectedInputs) { cmd.Prepare?(); cmd.GetValue(ctx); cmd.Restore?(); }
//   context.ObjectToWorld = prev;                                              // POP (restore)
// The children read context.ObjectToWorld when they build their own ObjectToClipSpace (Layer2d via
// _ProcessLayer2d → ApplyTransformMatrix Multiply(M, context.ObjectToWorld)), so a child vertex sees
// v·childO2W·groupSRT·parentGroupSRT…  (group is the PARENT applied AFTER the child's own).
//
// ★INTEGRATION MECHANISM — per-item group stamp (the Camera-op precedent, point_ops_camera.cpp /
// render_command.h hasGroup/groupObjectToWorld): SW is retained-mode with a per-item executor; there is no
// runtime ObjectToWorld scope stack to push onto. So cookGroup STAMPS its accumulated S·R·T onto every
// RenderDrawItem its subtree produced, and the EXECUTOR right-multiplies it into the item's own
// ObjectToWorld (point_ops_rendertarget.cpp Layer2d + Mesh cases: finalO2W = childO2W · groupObjectToWorld).
// This reproduces TiXL's push/pop WITHOUT a scope stack — the SAME mechanism Camera uses for WorldToCamera.
//   push = stamp groupSRT onto every subtree item, ACCUMULATING: it.group = it.group · thisGroupSRT.
//   pop  = the accumulation IS the pop. An OUTER Group multiplies onto items an INNER Group already
//          stamped → child sees v·childO2W·innerSRT·outerSRT (innermost first) = exactly TiXL's
//          context.ObjectToWorld = Multiply(outerSRT, Multiply(innerSRT, Identity)). No "where !hasGroup"
//          guard (unlike Camera's innermost-wins): group transforms COMPOSE (every level contributes),
//          they do not OVERRIDE — so each Group multiplies onto whatever is already there.
//   FORK (named): cross-sibling PrepareAction state N/A (retained-mode, same as Execute).
//   ForceColorUpdate/Color/EnableProfiling inputs dropped (named fork — profiling is editor-only; Color
//   foreground-tint is an S3-shading-context concern, not the transform seam; the .t3 Color default
//   (1,1,1,1) makes it a no-op for parity).
#include "runtime/point_ops.h"

#include "runtime/field_camera.h"    // Mat4 / mat4Mul / groupObjectToWorld
#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp, cookParam/cookVecN
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem

namespace sw {

// Group (TiXL transform/Group.cs): MultiInput Command in → Command out. The driver has already collected +
// concatenated all wired Command subtrees in wire order into cc.inputCommand (the S2a keystone branch).
// This op (1) applies IsEnabled — disabled ⇒ EMPTY chain (TiXL skips the foreach), and (2) builds its
// S·R·T from Scale·UniformScale / Rotation(yaw,pitch,roll) / Translation and STAMPS it onto every collected
// item (accumulating, so nesting composes). Unwired Command ⇒ empty chain (TiXL: an empty CollectedInputs).
RenderCommand cookGroup(CmdCookCtx& c) {
  RenderCommand rc;
  const bool enabled = cookParam(c, "IsEnabled", 1.0f) > 0.5f;  // .t3 DefaultValue = true
  if (!enabled || !c.inputCommand) return rc;                   // disabled / unwired ⇒ empty (no draws)
  rc.items = c.inputCommand->items;                             // COPY the collected subtree (we re-stamp)

  // TiXL Group.cs inputs. Scale default (1,1,1); UniformScale default 1; Rotation default (0,0,0);
  // Translation default (0,0,0). scaling = Scale * UniformScale (cs:38).
  float scaleDef[3] = {1.0f, 1.0f, 1.0f};
  float scale[3];
  cookVecN(c, "Scale", scaleDef, 3, scale);
  float uniform = cookParam(c, "UniformScale", 1.0f);
  scale[0] *= uniform; scale[1] *= uniform; scale[2] *= uniform;
  float rotDef[3] = {0.0f, 0.0f, 0.0f};  // (X=pitch, Y=yaw, Z=roll) — TiXL Rotation Vector3, degrees
  float rot[3];
  cookVecN(c, "Rotation", rotDef, 3, rot);
  float transDef[3] = {0.0f, 0.0f, 0.0f};
  float trans[3];
  cookVecN(c, "Translation", transDef, 3, trans);

  // groupSRT = S·R·T (row-vector). yaw=rot.Y, pitch=rot.X, roll=rot.Z (Group.cs:40-42).
  Mat4 srt = groupObjectToWorld(scale[0], scale[1], scale[2], /*yaw=*/rot[1], /*pitch=*/rot[0],
                                /*roll=*/rot[2], trans[0], trans[1], trans[2]);

  for (RenderDrawItem& it : rc.items) {
    // Accumulate (compose), do NOT override: child sees v·childO2W·existingGroup·thisGroupSRT. For a first
    // (no inner group) stamp, existingGroup = identity → final = thisGroupSRT. For an OUTER group over an
    // already-stamped inner one, it.group · srt = innerSRT · outerSRT (innermost first) = TiXL
    // Multiply(outerSRT, innerSRT-on-context). hasGroup gates the executor's multiply.
    Mat4 existing{};
    for (int i = 0; i < 16; ++i) existing.m[i] = it.groupObjectToWorld[i];
    Mat4 composed = it.hasGroup ? mat4Mul(existing, srt) : srt;
    for (int i = 0; i < 16; ++i) it.groupObjectToWorld[i] = composed.m[i];
    it.hasGroup = true;
  }
  return rc;
}

void registerGroupOp() { registerCmdOp("Group", cookGroup); }

}  // namespace sw
