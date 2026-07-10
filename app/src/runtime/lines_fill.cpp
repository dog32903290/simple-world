// runtime/lines_fill — see lines_fill.h for the peel rationale (ratchet split of point_ops_rendertarget.cpp,
// mirror of mesh_pbr_fill.cpp).
#include "runtime/lines_fill.h"

#include <Metal/Metal.hpp>

#include "runtime/draw_params.h"     // DrawLineParams + DRAWLINE_* bindings
#include "runtime/render_command.h"  // RenderDrawItem

namespace sw {

void encodeLinesDraw(MTL::RenderCommandEncoder* enc, const RenderDrawItem& it, MTL::RenderPipelineState* pso) {
  enc->setRenderPipelineState(pso);
  enc->setVertexBuffer(const_cast<MTL::Buffer*>(it.points), 0, DRAWLINE_Points);
  DrawLineParams lp{};
  lp.color[0] = it.color[0]; lp.color[1] = it.color[1];
  lp.color[2] = it.color[2]; lp.color[3] = it.color[3];
  lp.lineWidth = it.lineWidth;
  lp.viewExtent = it.viewExtent;
  lp.closed = it.lineClosed ? 1u : 0u;
  // DrawClosedLines: resolve TiXL's PointsPerShape default 0 ("one shape over all points") to the concrete
  // bag count so the shader's wrap modulo is always >0. Open DrawLines leaves both 0 → the wrap branch is
  // unreached (byte-identical).
  lp.pointsPerShape = it.lineClosed
      ? (it.pointsPerShape > 0 ? it.pointsPerShape : it.count)
      : 0u;
  enc->setVertexBytes(&lp, sizeof(lp), DRAWLINE_Params);
  // Segment count: OPEN draws (count-1) segments (sequential adjacency, TiXL DrawLines). CLOSED draws one
  // segment PER point — the extra wrap segment closes each shape (last→first), matching DrawClosedLines'
  // GetWrappedIndex. With pointsPerShape>0 a PARTIAL trailing shape is discarded (TiXL DrawLinesAlt
  // actualSegmentCount = numShapes*pointsPerShape) so we never emit a malformed wrap over a half-shape. Each
  // segment = 6 verts (screen-space quad).
  uint32_t segs;
  if (!it.lineClosed) {
    segs = it.count - 1;
  } else if (it.pointsPerShape > 0) {
    uint32_t numShapes = it.count / it.pointsPerShape;  // complete shapes only
    segs = numShapes * it.pointsPerShape;                // one segment per point in them
  } else {
    segs = it.count;                                     // one shape, all points wrap
  }
  if (segs == 0) return;
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger((size_t)segs * 6));
}

}  // namespace sw
