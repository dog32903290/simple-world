// DrawLinesBuildup: connect a Point buffer into a polyline like DrawLines, but with a progressive
// BUILDUP REVEAL driven by each point's W (FX1) (TiXL Operators/Lib/point/draw/DrawLinesBuildup →
// DrawLinesBuildup.hlsl). The line is the same screen-space-thickened quad as DrawLines (sequential
// adjacency Points[i]→Points[i+1]); the NEW behavior is the per-fragment reveal:
//
//   TiXL psMain: u-coord = wAtPoint - OffsetU,  OffsetU = TransitionProgress - 0.01;
//     f1 = saturate((u + VisibleRange) * 100);  f2 = 1 - saturate(u * 100);  alpha *= f1 * f2;
//   where wAtPoint = lerp(pointA.FX1, pointB.FX1, cornerFactor) is the point's parametric W.
//
// So a piece of the line is VISIBLE when (wAtPoint - OffsetU) ∈ [-VisibleRange, 0]: a moving window
// of width VisibleRange whose leading edge is at OffsetU. As TransitionProgress sweeps 0→1, the
// window slides along the polyline → the line "builds up". The *100 makes the window edges a hard
// (1-texel-W-wide) ramp.
//
// FORK (named): TiXL's Transforms cbuffer (camera matrices), ShrinkWithDistance, Fog, the
// Texture_ sample (default Lib:images/basic/white-pixel.png → a constant white tint = no-op), the
// neighbor-normal miter join, BlendMode, ZTest/ZWrite are dropped — sw has no camera system (same
// fork class as DrawLines' baked ortho). The line is a flat untextured constant-width band whose
// only DrawLinesBuildup-specific behavior is the W-reveal alpha. Color tints (Color * Point.Color).
#include <metal_stdlib>
#include "tixl_point.h"      // SwPoint
#include "draw_params.h"     // DrawLineBuildupBinding, DrawLineBuildupParams
using namespace metal;

// Per-segment 6-corner quad (same as draw_lines.metal). x picks endpoint (A=0/B=1); y is the
// across-line side (-1/+1).
constant float2 kBuildupCorners[6] = {
  float2(0, -1), float2(1, -1), float2(1, 1),
  float2(1, 1),  float2(0, 1),  float2(0, -1),
};

struct BuildupVSOut {
  float4 position [[position]];
  float4 color;
  float  wAtPoint;  // lerp(A.FX1, B.FX1, cornerFactor.x) → the reveal u-coord (TiXL texCoord.x)
};

vertex BuildupVSOut draw_lines_buildup_vs(uint vid [[vertex_id]],
                                          device const SwPoint* pts        [[buffer(DRAWLINEBU_Points)]],
                                          constant DrawLineBuildupParams& P [[buffer(DRAWLINEBU_Params)]]) {
  BuildupVSOut o;
  uint quadIndex = vid % 6;
  uint segId = vid / 6;                 // segment connects pts[segId] → pts[segId+1] (open adjacency)
  float2 corner = kBuildupCorners[quadIndex];

  SwPoint a = pts[segId];
  SwPoint b = pts[segId + 1];

  // Break marker (TiXL W=NaN) or dead point (Scale.x NaN) → collapse this segment offscreen.
  bool broken = isnan(a.Position.x) || isnan(b.Position.x) ||
                isnan(a.Scale.x) || isnan(b.Scale.x);
  if (broken) {
    o.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
    o.color = float4(0.0f);
    o.wAtPoint = 0.0f;
    return o;
  }

  // Orthographic project both endpoints to NDC (DrawLines convention).
  float2 aN = float2(a.Position.x, a.Position.y) / P.viewExtent;
  float2 bN = float2(b.Position.x, b.Position.y) / P.viewExtent;

  // Screen-space band: half-width across the segment direction (same as draw_lines.metal).
  float2 dir = bN - aN;
  float dl = max(length(dir), 1e-6f);
  float2 t = dir / dl;
  float2 n = float2(-t.y, t.x);
  float halfW = 0.5f * P.lineWidth / P.viewExtent;

  float2 basePos = corner.x < 0.5f ? aN : bN;
  o.position = float4(basePos + n * (corner.y * halfW), 0.0f, 1.0f);

  // Color along the segment (TiXL output.color = Color.rgb / Color.a per-vertex, no per-Point lerp
  // in DrawLinesBuildup — it uses the param Color only). Match TiXL: tint by param Color, multiply
  // by the endpoint's per-Point Color (sw DrawLines convention) so a colored bag still tints.
  float4 pc = (corner.x < 0.5f) ? a.Color : b.Color;
  o.color = P.color * pc;

  // wAtPoint = the reveal coord: lerp the two endpoints' W (FX1) by which endpoint this vert is.
  o.wAtPoint = (corner.x < 0.5f) ? a.FX1 : b.FX1;
  return o;
}

fragment float4 draw_lines_buildup_fs(BuildupVSOut in        [[stage_in]],
                                      constant DrawLineBuildupParams& P [[buffer(DRAWLINEBU_Params)]]) {
  // TiXL: u = texCoord.x + VisibleRange (already incl. -OffsetU baked into wAtPoint? No — see below).
  // wAtPoint here = raw W; OffsetU = TransitionProgress - 0.01. u = wAtPoint - OffsetU (TiXL texCoord.x).
  float offsetU = P.transitionProgress - 0.01f;
  float u = in.wAtPoint - offsetU;
  float f1 = saturate((u + P.visibleRange) * 100.0f);
  float f2 = 1.0f - saturate(u * 100.0f);
  float reveal = f1 * f2;
  float4 col = in.color;
  col.a *= reveal;
  return col;
}
