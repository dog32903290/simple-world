// gridpoints — faithful port of external/tixl .../points/generate/GridPoints.hlsl
// (SHAPE: position + scale, Tiling=Cartesian). A generator op for the lane-A point-graph:
// writes a bag of SwPoints (no input bag).
//
// TiXL parity (GridPoints.hlsl lines 48-85, the Cartesian branch Tiling<0.5):
//   cell           = (i%cx, i/cx%cy, i/(cx*cy)%cz)
//   clampedCount   = (cx==1?1:cx-1, ...)             // avoid div-by-zero on plane axes
//   zeroAdjSize    = (cx==1?0:Size.x, ...)           // a 1-count axis collapses to 0
//   Cell mode  (SizeMode==0): pos = cell - clampedCount*(Pivot+0.5)
//   Bounds mode(SizeMode> 0): pos = cell/clampedCount - (Pivot+0.5)
//   pos *= zeroAdjSize;  pos += Center
//
// PARAM-COMPLETION GATE: Tiling still baked to Cartesian (the other three tilings are deferred —
// parityNotes), but Color(Vector4), F1/F2, and the quaternion orientation (OrientationAxis/Angle)
// are now READ from GridParams (filled by the cook from the NodeSpec) instead of baked white/0/
// identity. Math ported line-by-line from GridPoints.hlsl Cartesian branch (Tiling<0.5).
#include <metal_stdlib>
#include "tixl_point.h"       // SwPoint (64B)
#include "gridpoints_params.h" // GridParams, GridBinding
#include "shared/quat.metal.h" // qFromAngleAxis (shared attribute helper, also used by radial_points)
using namespace metal;

kernel void gridpoints(device SwPoint*       pts [[buffer(GRID_Points)]],
                       constant GridParams&  P   [[buffer(GRID_Params)]],
                       uint3                 tid [[thread_position_in_grid]]) {
  uint cx = max(P.CountX, 1u);
  uint cy = max(P.CountY, 1u);
  uint cz = max(P.CountZ, 1u);
  uint total = cx * cy * cz;
  uint index = tid.x;
  if (index >= total) return;

  // cell coordinate in the grid (TiXL: index % cx, index/cx % cy, index/(cx*cy) % cz)
  uint3 cell = uint3(index % cx,
                     (index / cx) % cy,
                     (index / (cx * cy)) % cz);
  float3 cellf = float3(cell);

  // clampedCount: a singleton axis maps to 1 (no division blow-up)
  float3 clampedCount = float3(cx == 1u ? 1.0f : float(cx - 1u),
                               cy == 1u ? 1.0f : float(cy - 1u),
                               cz == 1u ? 1.0f : float(cz - 1u));

  // zeroAdjustedSize: a singleton axis collapses its extent to 0 (the "plane of points" tip)
  float3 size = float3(P.SizeX, P.SizeY, P.SizeZ);
  float3 zeroAdjustedSize = float3(cx == 1u ? 0.0f : size.x,
                                   cy == 1u ? 0.0f : size.y,
                                   cz == 1u ? 0.0f : size.z);

  float3 pivot = float3(P.PivotX, P.PivotY, P.PivotZ);
  float3 center = float3(P.CenterX, P.CenterY, P.CenterZ);

  // SizeMode>0 (Bounds): normalize cell to 0..1 across the axis; Cell: integer steps
  float3 pos = (P.SizeMode > 0u) ? (cellf / clampedCount) - (pivot + 0.5f)
                                 : cellf - clampedCount * (pivot + 0.5f);
  pos *= zeroAdjustedSize;
  pos += center;  // Tiling=Cartesian branch

  SwPoint p;
  p.Position = pos;
  // GridPoints.hlsl:74-83 (Cartesian branch): attributes from the cbuffer, Rotation from axis-angle.
  p.Color = float4(P.ColorR, P.ColorG, P.ColorB, P.ColorA);
  p.FX1 = P.FX1;
  p.FX2 = P.FX2;
  p.Scale = float3(P.PointScale);
  p.Rotation = qFromAngleAxis(P.OrientAngle * (M_PI_F / 180.0f),
                              normalize(float3(P.OrientAxisX, P.OrientAxisY, P.OrientAxisZ)));
  pts[index] = p;
}
