// radial_points — faithful port of external/tixl .../points/generate/RadialPoints.hlsl
// (SHAPE: position + scale). A generator op for the lane-A point-graph: writes a bag of
// SwPoints (no input bag).
//
// NOTE: replaces a pre-pivot orphan of the same name (32-byte Particle world, BI_* enum,
// never built). This version is on the live SwPoint (64B, tixl_point.h) contract.
//
// TiXL parity:
//  - Position is faithful: f = index/Count; angle = StartAngle°→rad + Cycles·2π·f;
//    dir = normalize(cross(Axis, up)); rotate dir·(Radius+RadiusOffset·f) around Axis by
//    angle; + Center. TiXL builds a 3x3 rotation matrix; we use the algebraically identical
//    Rodrigues form (clearer, no row/column transpose trap). --selftest-radialop pins it.
//  - Axis(+Z), Center(0), Color(white) and the quaternion ORIENTATION (TiXL's two modes)
//    are TiXL Vector/enum inputs. NodeSpec is Float-only today, so they are baked to
//    TiXL-equivalent defaults here (identity rotation) until vector params land. Flagged.
#include <metal_stdlib>
#include "tixl_point.h"        // SwPoint (64B)
#include "particle_params.h"   // RadialParams, RadialBinding
using namespace metal;

// Rodrigues rotation of v around (unit) axis by `angle` rad. == TiXL RotatePointAroundAxis.
static float3 rotateAroundAxis(float3 v, float3 axis, float angle) {
  axis = normalize(axis);
  float s = sin(angle), c = cos(angle);
  return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0f - c);
}

kernel void radial_points(device SwPoint*        pts [[buffer(RADIAL_Points)]],
                          constant RadialParams&  P   [[buffer(RADIAL_Params)]],
                          uint3                   tid [[thread_position_in_grid]]) {
  if (tid.x >= P.Count) return;
  uint i = tid.x;
  float f = P.Count > 1u ? float(i) / float(P.Count) : 0.0f;  // GainAndBias default = identity

  const float3 axis = float3(0.0f, 0.0f, 1.0f);  // TiXL Axis default (+Z); baked until vec params
  const float3 center = float3(P.CenterX, P.CenterY, P.CenterZ);  // TiXL Center (vector param, live)
  float3 up = axis.y > 0.7f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);

  float angle = P.StartAngle * (M_PI_F / 180.0f) + P.Cycles * 2.0f * M_PI_F * f;
  float l = P.Radius + P.RadiusOffset * f;
  float3 dir = normalize(cross(axis, up));
  float3 v = rotateAroundAxis(dir * l, axis, angle) + center;

  SwPoint p;
  p.Position = v;
  p.FX1 = 0.0f;
  p.Rotation = float4(0.0f, 0.0f, 0.0f, 1.0f);  // identity — orientation deferred (quat + Axis param)
  p.Color = float4(1.0f, 1.0f, 1.0f, 1.0f);
  p.Scale = float3(P.ScaleBase + P.ScaleByF * f);
  p.FX2 = 0.0f;
  pts[i] = p;
}
