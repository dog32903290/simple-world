// DrawPoints: render a Point buffer (TiXL layout) as screen points. Orthographic
// map of XY / viewExtent -> NDC. Dead points (Scale == NaN, set by ParticleSystem
// lifetime) are pushed offscreen. Color comes from Point.Color.
#include <metal_stdlib>
#include "tixl_point.h"        // Point
#include "particle_params.h"   // DrawBinding
using namespace metal;

struct VSOut {
  float4 position   [[position]];
  float  point_size [[point_size]];
  float4 color;
};

vertex VSOut draw_points_vs(uint vid [[vertex_id]],
                            device const SwPoint* pts      [[buffer(DRAW_Points)]],
                            constant float&     viewExtent [[buffer(DRAW_ViewExtent)]]) {
  VSOut o;
  SwPoint pt = pts[vid];
  if (isnan(pt.Scale.x) || isnan(pt.Position.x)) {  // dead / un-emitted
    o.position = float4(2.0f, 2.0f, 2.0f, 1.0f);     // offscreen
    o.point_size = 0.0f;
    o.color = float4(0.0f);
    return o;
  }
  float2 p = float2(pt.Position.x, pt.Position.y) / viewExtent;
  o.position = float4(p, 0.0f, 1.0f);
  o.point_size = 4.0f;
  o.color = pt.Color;
  return o;
}

fragment float4 draw_points_fs(VSOut in [[stage_in]]) { return in.color; }
