// runtime/point_ops_spreadlayout — SpreadLayout command op.
// TiXL authority: external/tixl/Operators/Lib/render/transform/SpreadLayout.cs (+ .t3 defaults).
//
// TiXL SpreadLayout = Group's SRT push, but the TRANSLATION is spread PER CHILD along a line
// (SpreadLayout.cs:55-93 for-loop over Commands.CollectedInputs):
//   s = Scale * UniformScale                       (cs:34)
//   yaw = Rotation.Y, pitch = Rotation.X, roll = Rotation.Z (degrees → radians)   (cs:36-38)
//   f_i = count<=1 ? 0 : (0.5 - (i/(count-1) - 0.5)) - Pivot                      (cs:59)
//   tSpreaded_i = Translation - Spread * f_i                                      (cs:60)
//   objectToParentObject_i = CreateTransformationMatrix(scaling=s,
//       rotation=CreateFromYawPitchRoll(yaw,pitch,roll), translation=tSpreaded_i) (cs:63-69)
//   context.ObjectToWorld = Multiply(objectToParentObject_i, original)            (cs:77)
// gated by IsEnabled && commands.Count>0 (cs:41; disabled ⇒ children not executed ⇒ no draws),
// with Spread forced ZERO when there is exactly ONE child (cs:43-44). .t3 defaults: Translation/
// Rotation/Spread=(0,0,0), Scale=(1,1,1), UniformScale=1, Pivot=0.5, IsEnabled=true.
//
// ★MECHANISM: the same per-WIRE group stamp as SpreadIntoGrid (see point_ops_spreadintogrid.cpp
// header) — CmdCookCtx::inputCmdWireItemCounts gives the child-chain boundaries; wire i's items get
// groupObjectToWorld(s, yaw,pitch,roll, tSpreaded_i) accumulated Group-style (hasGroup/
// groupObjectToWorld, executor right-multiply).
// FORKS (named, the Group fork class — point_ops_group.cpp:35-38): Color foreground-tint +
// ForceColorUpdate (context.ForegroundColor *= Color + InvalidateGraph, cs:71-86) are an S3
// shading-context concern, dropped (the .t3 Color default (1,1,1,1) makes it a parity no-op);
// ITransformable/TransformCallback (cs:13-16, 25) is the editor gizmo hook — editor-only, dropped;
// PrepareAction/RestoreAction cross-sibling state N/A (retained mode).
#include "runtime/point_ops_spread.h"

#include <cstdint>
#include <vector>

#include "runtime/field_camera.h"    // Mat4 / mat4Mul / groupObjectToWorld
#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp, cookParam/cookVecN
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem

namespace sw {

namespace {

// The Group accumulate-stamp over one wire's items (verbatim twin of point_ops_spreadintogrid.cpp's
// stampWire — 10 lines, kept file-local in each leaf rather than growing a shared header).
void stampWire(std::vector<RenderDrawItem>& items, size_t base, uint32_t n, const Mat4& srt) {
  for (uint32_t k = 0; k < n && base + k < items.size(); ++k) {
    RenderDrawItem& it = items[base + k];
    Mat4 existing{};
    for (int j = 0; j < 16; ++j) existing.m[j] = it.groupObjectToWorld[j];
    Mat4 composed = it.hasGroup ? mat4Mul(existing, srt) : srt;
    for (int j = 0; j < 16; ++j) it.groupObjectToWorld[j] = composed.m[j];
    it.hasGroup = true;
  }
}

}  // namespace

// SpreadLayout (TiXL render/transform/SpreadLayout.cs): MultiInput Command in → Command out. Declared
// in point_ops_spreadintogrid.cpp (the family registrar); non-static so it can bind there.
RenderCommand cookSpreadLayout(CmdCookCtx& c) {
  RenderCommand rc;
  const bool enabled = cookParam(c, "IsEnabled", 1.0f) > 0.5f;  // .t3 default true
  if (!enabled || !c.inputCommand) return rc;  // cs:41 gate: disabled/unwired ⇒ no draws
  rc.items = c.inputCommand->items;
  std::vector<uint32_t> wires = c.inputCmdWireItemCounts;
  if (wires.empty() && !rc.items.empty()) wires.push_back((uint32_t)rc.items.size());  // legacy ctx: 1 child
  const size_t count = wires.size();  // TiXL commands.Count = number of WIRES (empty chains still count)
  if (count == 0) return rc;

  const float pivot = cookParam(c, "Pivot", 0.5f);  // cs:27; .t3 default 0.5
  float zeroDef[3] = {0.0f, 0.0f, 0.0f};
  float spread[3];
  cookVecN(c, "Spread", zeroDef, 3, spread);  // cs:29; .t3 default (0,0,0)
  float scaleDef[3] = {1.0f, 1.0f, 1.0f};
  float s[3];
  cookVecN(c, "Scale", scaleDef, 3, s);
  const float uniform = cookParam(c, "UniformScale", 1.0f);
  s[0] *= uniform; s[1] *= uniform; s[2] *= uniform;  // cs:34: s = Scale * UniformScale
  float rot[3];
  cookVecN(c, "Rotation", zeroDef, 3, rot);  // (X=pitch, Y=yaw, Z=roll) degrees, cs:36-38
  float t[3];
  cookVecN(c, "Translation", zeroDef, 3, t);
  if (count == 1) spread[0] = spread[1] = spread[2] = 0.0f;  // cs:43-44

  size_t base = 0;
  for (size_t i = 0; i < count; ++i) {
    // -bug: collapse every child onto index 0 — per-wire seam corrupted, cook still runs.
    const size_t idx = spreadCollapseIndexForTest() ? 0 : i;
    const float fi = count <= 1 ? 0.0f
                                : (0.5f - ((float)idx / (float)(count - 1) - 0.5f)) - pivot;  // cs:59
    const float ts[3] = {t[0] - spread[0] * fi, t[1] - spread[1] * fi, t[2] - spread[2] * fi};  // cs:60
    // cs:63-69: S·R·T with yaw=rot.Y, pitch=rot.X, roll=rot.Z (the Group.cs:40-42 mapping).
    Mat4 srt = groupObjectToWorld(s[0], s[1], s[2], /*yaw=*/rot[1], /*pitch=*/rot[0],
                                  /*roll=*/rot[2], ts[0], ts[1], ts[2]);
    stampWire(rc.items, base, wires[i], srt);  // push = per-wire stamp (cs:77 Multiply(M, original))
    base += wires[i];
  }
  return rc;
}

}  // namespace sw
