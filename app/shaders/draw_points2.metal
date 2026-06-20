// DrawPoints2: render a Point buffer as Radius-sized screen-facing quad sprites (TiXL
// Operators/Lib/point/draw/DrawPoints2 → DrawPoints.hlsl). TiXL's DrawPoints2 is a NEW DrawPoints
// variant that drives the sprite size with a Radius parameter (Radius → Multiply ×10.8 → PointSize
// in the shader's cbuffer) instead of the v1 PointSize, draws camera-facing billboards ignoring
// point orientation, and (UseWForSize default true) scales each sprite by the point's W (FX1, the
// shader's ScaleFX==1 path: sizeFxFactor = pointDef.FX1). Color tints (Color * Point.Color).
//
// We keep the size formula + the W-scale + the Color tint, but project ORTHOGRAPHICALLY (XY /
// viewExtent → NDC), the camera-less convention shared by DrawPoints / DrawBillboards. The 6-vert
// quad is the DrawPoints.hlsl Corners layout (the sprite the texture would map onto).
//
// FORK (named): TiXL's Transforms cbuffer (camera matrices), Fog, the optional Texture_ sprite
// atlas (default Lib:images/basic/white-dot-256px.png → a round dot mask), FadeNearest (camera-Z
// fade), BlendMode, ZTest/ZWrite are dropped — sw has no camera system (same fork class as
// DrawPoints' baked ortho), so the sprite is a flat untextured SQUARE of constant Radius×W size.
// The Radius→×10.8→PointSize chain is honored: P.pointSize already = Radius * 10.8 (host side), and
// the 0.10 quad-unit + W-scale match DrawPoints.hlsl's `quadPos.xy * 0.10 * (PointSize * FX1)`.
#include <metal_stdlib>
#include "tixl_point.h"      // SwPoint
#include "draw_params.h"     // DrawPoint2Binding, DrawPoint2Params
using namespace metal;

// 6-corner quad in [-1,1]² (two triangles), DrawPoints.hlsl Corners (centered sprite).
constant float2 kPoint2Corners[6] = {
  float2(-1, -1), float2(1, -1), float2(1, 1),
  float2(1, 1),   float2(-1, 1), float2(-1, -1),
};

struct Point2VSOut {
  float4 position [[position]];
  float4 color;
};

vertex Point2VSOut draw_points2_vs(uint vid [[vertex_id]],
                                   device const SwPoint* pts      [[buffer(DRAWPOINT2_Points)]],
                                   constant DrawPoint2Params& P    [[buffer(DRAWPOINT2_Params)]]) {
  Point2VSOut o;
  uint quadIndex = vid % 6;
  uint pointId = vid / 6;
  float2 corner = kPoint2Corners[quadIndex];

  SwPoint pt = pts[pointId];
  // Dead point (Scale.x NaN, set by ParticleSystem lifetime) or NaN position → collapse offscreen.
  if (isnan(pt.Scale.x) || isnan(pt.Position.x)) {
    o.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
    o.color = float4(0.0f);
    return o;
  }

  // TiXL DrawPoints.hlsl: sizeFxFactor = ScaleFX==1 ? pointDef.FX1 : 1 (DrawPoints2 UseWForSize
  // default true → ScaleFX maps to W=FX1). s = PointSize * sizeFxFactor; quadPos += quadPos.xy*0.10*s.
  // P.pointSize already carries Radius*10.8 (host MultiplyInt chain). When UseWForSize=0 the W factor
  // is 1 (the host sets useWForSize accordingly). NaN W (undefined) → treat as 1 so the sprite shows.
  float wFactor = (P.useWForSize != 0u) ? pt.FX1 : 1.0f;
  if (isnan(wFactor)) wFactor = 1.0f;
  float s = P.pointSize * wFactor;

  float2 centerN = float2(pt.Position.x, pt.Position.y) / P.viewExtent;
  // 0.10 quad-unit (DrawPoints.hlsl), then world→NDC via viewExtent so sprite size tracks the points.
  float2 quadOffset = corner * 0.10f * s / P.viewExtent;
  o.position = float4(centerN + quadOffset, 0.0f, 1.0f);

  o.color = P.color * pt.Color;
  return o;
}

fragment float4 draw_points2_fs(Point2VSOut in [[stage_in]]) { return in.color; }
