// DrawLines: connect a Point buffer into a polyline (TiXL Operators/Lib/.../draw/DrawLines.hlsl).
// TiXL connects Points[i]→Points[i+1] (sequential adjacency), expanding each segment into a
// 6-vertex screen-space-thickened quad whose width is the line thickness in NDC. We keep that
// adjacency + the 6-corner quad layout, but project orthographically (XY/viewExtent → NDC, the
// DrawPoints camera-less convention) instead of through TiXL's ObjectToClipSpace camera stack.
//
// FORK (named): TiXL's Transforms cbuffer (camera matrices), ShrinkWithDistance, FogParams,
// texture/UV, and miter-join math are dropped — we have no camera system (same fork class as
// DrawPoints' baked ortho). The quad is a flat screen-space band of constant half-width.
//
// SEPARATOR (NAMED FORK — forward parity, refuter-R-L 修帳): TiXL marks line breaks with
// Point.W = NaN (AppendPoints.hlsl writes ResultPoints[i].W = NAN at bag boundaries). Our
// CombineBuffers is a pure byte-blit and does NOT inject NaN separators today, so this break
// logic is dead-but-harmless in production — kept so the day a NaN-writing op lands (TiXL
// parity), disjoint polylines don't get bridged. A segment whose endpoint A or B has
// FX1 == NaN collapses offscreen. (The selftest exercises it by hand-writing the NaN.)
//
// CLOSED LOOP (DrawClosedLines, TiXL DrawLinesAlt.hlsl GetWrappedIndex): when P.closed, segment i
// connects pts[i]→pts[(i+1) % shapePts] so the polyline wraps last→first (a closed polygon).
// P.pointsPerShape>0 splits the bag into per-shape closed loops of that many points each (the
// executor draws `count` segments instead of `count-1`, so the wrap segment exists). closed=0 keeps
// the open Points[i]→Points[i+1] adjacency exactly (the wrap math is unreached → byte-identical).
#include <metal_stdlib>
#include "tixl_point.h"      // SwPoint
#include "draw_params.h"     // DrawLineBinding, DrawLineParams
using namespace metal;

// Per-segment 6-corner quad. x picks endpoint (A=0 / B=1); y is the across-line side (-1/+1).
constant float2 kLineCorners[6] = {
  float2(0, -1), float2(1, -1), float2(1, 1),
  float2(1, 1),  float2(0, 1),  float2(0, -1),
};

struct LineVSOut {
  float4 position [[position]];
  float4 color;
};

vertex LineVSOut draw_lines_vs(uint vid [[vertex_id]],
                               device const SwPoint* pts        [[buffer(DRAWLINE_Points)]],
                               constant DrawLineParams& P       [[buffer(DRAWLINE_Params)]]) {
  LineVSOut o;
  uint quadIndex = vid % 6;
  uint segId = vid / 6;                 // segment connects pts[idxA] → pts[idxB]
  float2 corner = kLineCorners[quadIndex];

  // Endpoint indices. OPEN (DrawLines): idxA=segId, idxB=segId+1 (sequential adjacency).
  // CLOSED (DrawClosedLines, TiXL GetWrappedIndex): the "next" index wraps modulo the shape so the
  // last segment of each shape returns to its first point — closing the polygon. The executor has
  // already resolved pointsPerShape to the concrete shape size (it passes the bag count when the
  // .t3 default 0 means "one shape over all points"), so shapePts here is always >0 when closed.
  uint idxA = segId;
  uint idxB = segId + 1;
  if (P.closed) {
    uint shapePts    = max(P.pointsPerShape, 1u);
    uint shapeStart  = (segId / shapePts) * shapePts;   // first point of this shape
    uint inShape     = segId - shapeStart;              // segment index within the shape
    idxA = shapeStart + (inShape % shapePts);
    idxB = shapeStart + ((inShape + 1) % shapePts);     // wrap last→first
  }

  SwPoint a = pts[idxA];
  SwPoint b = pts[idxB];

  // Break marker (TiXL W=NaN) or dead point (Scale.x NaN, set by ParticleSystem lifetime):
  // collapse this segment offscreen so disjoint polylines stay disjoint.
  bool broken = isnan(a.Position.x) || isnan(b.Position.x) ||
                isnan(a.FX1) || isnan(b.FX1) ||
                isnan(a.Scale.x) || isnan(b.Scale.x);
  if (broken) {
    o.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
    o.color = float4(0.0f);
    return o;
  }

  // Orthographic project both endpoints to NDC (DrawPoints convention).
  float2 aN = float2(a.Position.x, a.Position.y) / P.viewExtent;
  float2 bN = float2(b.Position.x, b.Position.y) / P.viewExtent;

  // Screen-space band: half-width across the segment direction. lineWidth is in world units,
  // mapped to NDC by the same viewExtent so thickness tracks the points.
  float2 dir = bN - aN;
  float dl = max(length(dir), 1e-6f);
  float2 t = dir / dl;
  float2 n = float2(-t.y, t.x);                 // across-line normal
  float halfW = 0.5f * P.lineWidth / P.viewExtent;

  float2 basePos = corner.x < 0.5f ? aN : bN;   // endpoint A or B
  float2 p = basePos + n * (corner.y * halfW);
  o.position = float4(p, 0.0f, 1.0f);

  // Color = param tint * per-Point color, lerped along the segment (TiXL output.color).
  float4 pc = (corner.x < 0.5f) ? a.Color : b.Color;
  o.color = P.color * pc;
  return o;
}

fragment float4 draw_lines_fs(LineVSOut in [[stage_in]]) { return in.color; }
