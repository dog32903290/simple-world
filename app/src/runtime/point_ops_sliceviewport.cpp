// runtime/point_ops_sliceviewport — SliceViewPort command op (cell viewport rect + RepeatView clip scale +
// RequestedResolution cell push). TiXL authority: external/tixl/Operators/Lib/render/transform/SliceViewPort.cs.
//
// TiXL SliceViewPort renders its SubGraph into ONE grid cell (SliceViewPort.cs:24-114):
//   cells   = CellCounts.Clamp(1,∞);  cellCount = cells.W·cells.H;  Count = min(cellCount,1000)   (cs:32-38)
//   cellSize = (RequestedResolution.W / cells.W, RequestedResolution.H / cells.H)                  (cs:40-41)
//   idx = CellIndex.Clamp(0,max) % cellCount;  column = idx % cells.W;  row = idx / cells.W        (cs:43-46)
//   viewport.X = (column + (1−Stretch.X)/2)·cellSize.X                                             (cs:49)
//   viewport.Y = (row    + (1−Stretch.Y)/2)·cellSize.Y                                             (cs:50)
//   viewport.W = cellSize.X·Stretch.X;  viewport.H = cellSize.Y·Stretch.Y                          (cs:51-52)
//   RepeatView (cs:67-74): M11 *= (cells.W/cells.H)/max(Stretch.X,eps);  M22 *= 1/max(Stretch.Y,eps)
//   context.RequestedResolution = cellSize.Clamp(1,16384)  around the subtree                       (cs:103)
//   rasterizer.SetViewport(viewport);  SubGraph.GetValue(context);  restore all                    (cs:105-113)
//
// ★INTEGRATION (retained-mode, the Camera/Group per-item STAMP precedent — sw has no runtime rasterizer/ctx
// scope stack): SliceViewPort STAMPS the viewport rect + the RepeatView M11/M22 clip scale onto every subtree
// item; the executor calls enc->setViewport per item (point_ops_renderstate.cpp applyItemViewport) and the
// item's clip builder applies clipScale. The RequestedResolution CELL push lives in the cook DRIVER
// (point_graph*_command_cook.cpp), which SETS requestedResolution = resolveSliceViewPortResolution BEFORE
// recursing into the subtree and RESTORES after — the exact resolveSetRequestedResolution mechanism.
// INNERMOST wins (a nested SliceViewPort already-stamped item is left alone).
//
// FORKS: fork-sliceviewport-repeatview-only (v1: RepeatView clip; SliceView/FitProjection M31/M32 crop offsets
//   deferred — clipScale carries only M11/M22); fork-sliceviewport-per-item-stamp (viewport is per-ITEM, not a
//   ctx scope — same bargain as Camera/Group). The Count output (cs:38) is a value the driver's cook-emit could
//   surface later; v1 omits it (no wire consumes it in the census graphs).
#include "runtime/point_ops_sliceviewport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp, RenderResolution, cookVecN/cookParam
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem

namespace sw {

bool& sliceViewPortDisableStampForTest() { static bool f = false; return f; }

namespace {
float paramOr(const std::map<std::string, float>& m, const char* id, float def) {
  auto it = m.find(id);
  return it != m.end() ? it->second : def;
}
uint32_t clampDim(float v) {  // TiXL Int2.Clamp(1,16384) — also Metal's max 2D texture dim on Apple Si.
  return (uint32_t)std::lround(std::min(std::max(v, 1.0f), 16384.0f));
}
// The CellCounts, clamped ≥1 (cs:32). Width = CellCounts.x, Height = CellCounts.y.
void cellCounts(const std::map<std::string, float>& p, int& w, int& h) {
  w = (int)paramOr(p, "CellCounts.x", 1.0f);
  h = (int)paramOr(p, "CellCounts.y", 1.0f);
  if (w < 1) w = 1;
  if (h < 1) h = 1;
}
}  // namespace

RenderResolution resolveSliceViewPortResolution(const std::map<std::string, float>& params,
                                                RenderResolution current) {
  int cw, ch;
  cellCounts(params, cw, ch);
  const float cellW = (float)current.w / (float)cw;  // cs:40
  const float cellH = (float)current.h / (float)ch;  // cs:41
  return RenderResolution{clampDim(cellW), clampDim(cellH)};  // cs:103 Clamp(1,16384)
}

void computeSliceViewPortCell(const std::map<std::string, float>& params, uint32_t resW, uint32_t resH,
                              float outViewport[4], float outClipScale[2]) {
  int cw, ch;
  cellCounts(params, cw, ch);
  const int cellCount = cw * ch;
  const float cellSizeX = (float)resW / (float)cw;  // cs:40
  const float cellSizeY = (float)resH / (float)ch;  // cs:41
  int idx = (int)paramOr(params, "CellIndex", 0.0f);
  if (idx < 0) idx = 0;
  const int mod = cellCount > 0 ? (idx % cellCount) : 0;  // cs:44
  const int column = cw > 0 ? (mod % cw) : 0;             // cs:45
  const int row = cw > 0 ? (mod / cw) : 0;                // cs:46
  const float stretchX = paramOr(params, "Stretch.x", 1.0f);
  const float stretchY = paramOr(params, "Stretch.y", 1.0f);
  outViewport[0] = ((float)column + (1.0f - stretchX) * 0.5f) * cellSizeX;  // cs:49
  outViewport[1] = ((float)row + (1.0f - stretchY) * 0.5f) * cellSizeY;     // cs:50
  outViewport[2] = cellSizeX * stretchX;                                     // cs:51
  outViewport[3] = cellSizeY * stretchY;                                     // cs:52
  // RepeatView clip scale (cs:67-74). eps guards a zero stretch (TiXL max(stretch, 0.0001)).
  const float eps = 0.0001f;
  outClipScale[0] = ((float)cw / (float)ch) / std::max(stretchX, eps);  // M11 *= aspect/stretchX (cs:69)
  outClipScale[1] = 1.0f / std::max(stretchY, eps);                     // M22 *= 1/stretchY (cs:70)
}

namespace {

// SliceViewPort cook: Command subtree in → Command out. The driver already PUSHED the cell RequestedResolution
// around the subtree cook (resolveSliceViewPortResolution). This op STAMPS the cell viewport rect + RepeatView
// clip scale onto every subtree item (innermost wins), then forwards them. Unwired Command → empty chain.
RenderCommand cookSliceViewPort(CmdCookCtx& c) {
  RenderCommand rc;
  if (!c.inputCommand) return rc;
  rc.items = c.inputCommand->items;
  if (sliceViewPortDisableStampForTest()) return rc;  // -bug: skip the stamp → subtree draws full-target

  // The ambient RequestedResolution the grid divides = the executor's output size at draw time. sw's cook
  // does not carry the pixel resolution to the op body (the driver owns it), so the viewport is computed in
  // NORMALIZED cell fractions here (resW=resH=1) and the executor scales by the output texture size — the
  // same "op stamps normalized, executor finalizes with output size" bargain as Layer2d's ScaleMode. The
  // clip scale is resolution-INDEPENDENT (pure cell/stretch ratios), so it is exact here.
  float vp[4], clip[2];
  computeSliceViewPortCell(*c.params, 1u, 1u, vp, clip);
  for (RenderDrawItem& it : rc.items) {
    if (it.hasViewport) continue;  // a NESTED SliceViewPort already stamped this item (innermost wins)
    it.hasViewport = true;
    it.viewport[0] = vp[0]; it.viewport[1] = vp[1]; it.viewport[2] = vp[2]; it.viewport[3] = vp[3];
    it.clipScale[0] = clip[0]; it.clipScale[1] = clip[1];
  }
  return rc;
}

}  // namespace

void registerSliceViewPortOp() { registerCmdOp("SliceViewPort", cookSliceViewPort); }

}  // namespace sw
