// runtime/point_ops_spreadintogrid — SpreadIntoGrid command op.
// TiXL authority: external/tixl/Operators/Lib/render/transform/SpreadIntoGrid.cs (+ .t3 defaults).
//
// TiXL SpreadIntoGrid iterates its MultiInput Command children and pushes a PER-CHILD pure-translation
// ObjectToWorld (SpreadIntoGrid.cs:33-66): child i sits at grid cell
//   xIdx = i % gridSize.X;  yIdx = i / gridSize.X;  zIdx = i / (gridSize.X * gridSize.Y)   (cs:37-39)
//   fX = gx<=1 ? 0 : xIdx/(gx-1) - 0.5;                                                    (cs:47)
//   fY = gy<=1 ? 0 : 0.5 - yIdx/(gy-1);                                                    (cs:48)
//   fZ = gz<=1 ? 0 : 0.5 - zIdx/(gz-1);                                                    (cs:49)
//   tSpreaded = (Spread * SpreadScale) * (fX, fY, fZ)                                      (cs:18, 51)
// with spread forced to ZERO when there is exactly ONE child (cs:20-21), gridSize clamped >=1 per
// axis (cs:26-28), and the transform = CreateTransformationMatrix(scale=1, rotation=Identity,
// translation=tSpreaded) = a pure TRANSLATE (cs:54-60), pushed as
// context.ObjectToWorld = Multiply(objectToParentObject, original) around child i's cook (cs:62-65).
// NOTE (faithful): yIdx/zIdx are NOT wrapped by gy/gz — an overflow child walks off the grid edge
// exactly like TiXL's integer division.
//
// ★MECHANISM — per-WIRE group stamp: sw is retained-mode (no ObjectToWorld scope stack); Group's
// per-item hasGroup/groupObjectToWorld stamp + executor right-multiply IS the push/pop analog
// (point_ops_group.cpp:23-34). The one new seam a spread needs over Group: each CHILD WIRE gets its
// OWN matrix, so the cook driver records the per-wire item counts of the MultiInput Command gather
// into CmdCookCtx::inputCmdWireItemCounts (filled by BOTH cook legs — the flat/resident mirror gate)
// and the op stamps wire i's items with translate(tSpreaded_i), accumulating exactly like Group.
// FORK (named, Group's fork class): PrepareAction/RestoreAction cross-sibling state N/A (retained
// mode); the executor consumes the group stamp on Layer2d + Mesh items only (same scope as Group).
#include "runtime/point_ops_spread.h"

#include <cstdint>
#include <vector>

#include "runtime/field_camera.h"    // Mat4 / mat4Mul / groupObjectToWorld
#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp, cookParam/cookVecN
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem

namespace sw {

bool& spreadCollapseIndexForTest() {
  static bool f = false;
  return f;
}

namespace {

// Accumulate-compose `srt` onto items [base, base+n) — the exact Group stamp (point_ops_group.cpp:76-86):
// composed = existingGroup · srt (innermost first); first stamp = srt. Shared by both spread ops.
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

// SpreadIntoGrid (TiXL render/transform/SpreadIntoGrid.cs): MultiInput Command in → Command out.
// The driver collected the child chains (wire order) into cc.inputCommand + the per-wire item counts
// into cc.inputCmdWireItemCounts; this op stamps each wire's items with its grid-cell translation.
RenderCommand cookSpreadIntoGrid(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.inputCommand) return rc;  // unwired ⇒ empty (TiXL: empty CollectedInputs)
  rc.items = c.inputCommand->items;
  std::vector<uint32_t> wires = c.inputCmdWireItemCounts;
  if (wires.empty() && !rc.items.empty()) wires.push_back((uint32_t)rc.items.size());  // legacy ctx: 1 child
  const size_t count = wires.size();  // TiXL commands.Count = number of WIRES (cs:23; empty chains still count)

  // spread = Spread * SpreadScale (cs:18); .t3 defaults Spread=(1,1,1), SpreadScale=1.
  float spreadDef[3] = {1.0f, 1.0f, 1.0f};
  float spread[3];
  cookVecN(c, "Spread", spreadDef, 3, spread);
  const float spreadScale = cookParam(c, "SpreadScale", 1.0f);
  for (float& v : spread) v *= spreadScale;
  if (count == 1) spread[0] = spread[1] = spread[2] = 0.0f;  // cs:20-21: single child sits at origin

  // gridSize clamped >=1 per axis (cs:26-28); .t3 default (3,3,1). Int3 → sw float params, truncate.
  float gridDef[3] = {3.0f, 3.0f, 1.0f};
  float grid[3];
  cookVecN(c, "GridSize", gridDef, 3, grid);
  int gx = (int)grid[0], gy = (int)grid[1], gz = (int)grid[2];
  if (gx < 1) gx = 1;
  if (gy < 1) gy = 1;
  if (gz < 1) gz = 1;

  size_t base = 0;
  for (size_t i = 0; i < count; ++i) {
    // -bug: collapse every child onto index 0 — the per-wire seam corrupted, the cook still runs.
    const size_t idx = spreadCollapseIndexForTest() ? 0 : i;
    const int xI = (int)(idx % (size_t)gx);                  // cs:37
    const int yI = (int)(idx / (size_t)gx);                  // cs:38 (no gy wrap — faithful)
    const int zI = (int)(idx / ((size_t)gx * (size_t)gy));   // cs:39 (no gz wrap — faithful)
    const float fX = gx <= 1 ? 0.0f : (float)xI / (float)(gx - 1) - 0.5f;  // cs:47
    const float fY = gy <= 1 ? 0.0f : 0.5f - (float)yI / (float)(gy - 1);  // cs:48
    const float fZ = gz <= 1 ? 0.0f : 0.5f - (float)zI / (float)(gz - 1);  // cs:49
    // cs:51-60: tSpreaded = spread*(fX,fY,fZ); transform = pure translate (scale=1, rotation=Identity).
    Mat4 srt = groupObjectToWorld(1.0f, 1.0f, 1.0f, /*yaw=*/0.0f, /*pitch=*/0.0f, /*roll=*/0.0f,
                                  spread[0] * fX, spread[1] * fY, spread[2] * fZ);
    stampWire(rc.items, base, wires[i], srt);  // push = per-wire stamp (cs:62 Multiply(M, original))
    base += wires[i];
  }
  return rc;
}

}  // namespace

// Defined here (single TU owns the family registration); cookSpreadLayout lives in its own leaf and
// registers itself through this fn via the extern below (one registrar, two leaves — rule-4 split).
RenderCommand cookSpreadLayout(CmdCookCtx& c);  // point_ops_spreadlayout.cpp

void registerSpreadOps() {
  registerCmdOp("SpreadIntoGrid", cookSpreadIntoGrid);
  registerCmdOp("SpreadLayout", cookSpreadLayout);
}

}  // namespace sw
