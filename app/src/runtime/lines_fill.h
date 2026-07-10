// runtime/lines_fill — encode one DrawKind::Lines RenderDrawItem (peeled out of the point_ops_rendertarget.cpp
// executor case for the line-count ratchet, mirror of mesh_pbr_fill.cpp). TiXL DrawLines / DrawClosedLines
// (Lib.point.draw): a screen-space quad per segment i→i+1 (open) or the wrapped closed-loop variant. The
// executor has already gated (it.count>=2) and picked/built the PSO. Pure CPU param-fill + segment-count math
// + Metal encoder calls, runtime leaf.
#pragma once

namespace MTL {
class RenderCommandEncoder;
class RenderPipelineState;
}  // namespace MTL

namespace sw {

struct RenderDrawItem;  // render_command.h

// Encode one DrawKind::Lines item: binds it.points, fills DrawLineParams (color/lineWidth/viewExtent/closed/
// pointsPerShape), computes the segment count (open: count-1; closed: count, or numShapes*pointsPerShape when
// PointsPerShape>0 — TiXL DrawClosedLines GetWrappedIndex, discarding a partial trailing shape), and draws
// segs*6 verts (screen-space quad per segment). No-op (returns without drawing) when segs==0.
void encodeLinesDraw(MTL::RenderCommandEncoder* enc, const RenderDrawItem& it, MTL::RenderPipelineState* pso);

}  // namespace sw
