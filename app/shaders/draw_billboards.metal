// DrawBillboards: expand each Point into a camera-facing quad sprite (TiXL
// Operators/Lib/.../draw/DrawBillBoards.hlsl). TiXL emits 6 verts per Point (two triangles) and
// orients the quad toward the camera (OrientationMode=Billboard default) at size Scale * 0.010 *
// Stretch * (UseScale ? Point.Scale.xy : 1). We keep the 6-vert quad + size formula, but face the
// SCREEN (orthographic XY plane) instead of TiXL's camera — same camera-less fork as DrawPoints.
//
// FORK (named): TiXL's Transforms cbuffer (camera), texture atlas / sprite sampling, random
// scatter, rotation axis, color/scale curves (ColorOverW/SizeOverW), and OrientationMode variants
// are dropped — we render a flat untextured quad facing the screen. Color = Param.color *
// Point.Color (TiXL's base color path). Per-point Scale.xy stretch retained (UsePointScale default
// true). Hidden when Point.FX1 (W) is NaN — TiXL's hideUndefinedPoints (isnan(p.FX1) → 0).
#include <metal_stdlib>
#include "tixl_point.h"      // SwPoint
#include "draw_params.h"     // DrawBillboardBinding, DrawBillboardParams
using namespace metal;

// 6-corner quad in [-1,1]² (two triangles). xy is the quad-local offset; faces the screen.
constant float2 kBillboardCorners[6] = {
  float2(-1, -1), float2(1, -1), float2(1, 1),
  float2(1, 1),   float2(-1, 1), float2(-1, -1),
};

struct BbVSOut {
  float4 position [[position]];
  float4 color;
};

vertex BbVSOut draw_billboards_vs(uint vid [[vertex_id]],
                                  device const SwPoint* pts          [[buffer(DRAWBB_Points)]],
                                  constant DrawBillboardParams& P    [[buffer(DRAWBB_Params)]]) {
  BbVSOut o;
  uint quadIndex = vid % 6;
  uint pointId = vid / 6;
  float2 corner = kBillboardCorners[quadIndex];

  SwPoint pt = pts[pointId];

  // Dead point (Scale.x NaN) or undefined W (FX1 NaN, TiXL hideUndefinedPoints) → collapse.
  if (isnan(pt.Scale.x) || isnan(pt.Position.x) || isnan(pt.FX1)) {
    o.position = float4(2.0f, 2.0f, 2.0f, 1.0f);
    o.color = float4(0.0f);
    return o;
  }

  // Center in NDC (DrawPoints orthographic convention).
  float2 centerN = float2(pt.Position.x, pt.Position.y) / P.viewExtent;

  // TiXL size: Scale * 0.010 * (UsePointScale ? Point.Scale.xy : 1). The 0.010 is TiXL's quad
  // unit; we then map world→NDC via viewExtent so sprite size tracks the points like the lines do.
  float2 perPoint = float2(pt.Scale.x, pt.Scale.y);
  float2 quadOffset = corner * P.size * 0.010f * perPoint / P.viewExtent;
  float2 p = centerN + quadOffset;
  o.position = float4(p, 0.0f, 1.0f);

  o.color = P.color * pt.Color;
  return o;
}

fragment float4 draw_billboards_fs(BbVSOut in [[stage_in]]) { return in.color; }
