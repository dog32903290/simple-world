// Shared host<->shader params for the TiXL-ported HexGridPoints GENERATOR (batch 19).
// Mirrors external/tixl/Operators/Lib/point/generate/HexGridPoints.cs (.cs ports) +
// .../Assets/shaders/points/generate/HexGridPoints.hlsl (math, Pattern=2 Hexa branch).
//
// HexGridPoints generates a hexagonal grid of points (no input bag).
// Math (HexGridPoints.hlsl Pattern=2 branch, lines 93-114):
//   total = CountX * CountY * CountZ
//   cell = (index % cx, index/cx % cy, index/(cx*cy) % cz)
//   clampedCount = (cx==1?1:cx-1, cy==1?1:cy-1, cz==1?1:cz-1)
//   zeroAdjustedSize = (cx==1?0:Size.x, ...)
//   SizeMode 0 (Cell):   pos = cell - clampedCount*(Pivot+0.5)
//   SizeMode 1 (Bounds): pos = cell/clampedCount - (Pivot+0.5)
//   pos *= zeroAdjustedSize
//   hexAttrIndex = cell.x % 2 + ((cell.y + 3) % 6) * 2
//   offsetAndAngles = HexOffsetsAndAngles[hexAttrIndex]
//   pos.x += offsetAndAngles.x * zeroAdjustedSize.x * 0.3333
//   pos.x *= HexScale * 3   (HexScale=0.578)
//   rotDelta = (180 + offsetAndAngles.y) * ToRad
//   pos += Center
//   Rotation = qFromAngleAxis(OrientationAngle*PI/180 + rotDelta, normalize(OrientationAxis))
//
// TiXL HexOffsetsAndAngles table (12 entries):
//   { (-1,90),(0,30), (0,150),(-1,-30), (-1,-150),(0,-90),
//     (0,30),(-1,90), (-1,-30),(0,150), (0,-90),(-1,-150) }
//
// Scale (TiXL Single [Input], default 1.0): per .t3 (HexGridPoints.t3:329-337) it routes through
//   ScaleVector3 to multiply the Size Vector3 (effective Size = Scale * Size). Applied host-side in
//   the cook (faithful to the ScaleVector3 routing); shader is unchanged.
// Fork: Rotation set via Y·X·Z order (CreateFromYawPitchRoll=Y·X·Z per batch16/17 rule).
//       OrientationAxis/OrientationAngle from params -> full quat. Color=white baked.
//       W from port (default 1.0). Triangular and default patterns baked to Pattern=2 (Hexa).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct HexGridParams {
#ifdef __METAL_VERSION__
  uint CountX;
  uint CountY;
  uint CountZ;
  uint SizeMode;   // 0 = Cell, 1 = Bounds
#else
  uint32_t CountX;
  uint32_t CountY;
  uint32_t CountZ;
  uint32_t SizeMode;
#endif
  // TiXL Size (Vector3)
  float SizeX, SizeY, SizeZ;
  // TiXL Center (Vector3)
  float CenterX;   // -> 16 bytes (4+4+4+4=16 above done, now SizeX..CenterX = 4*4=16)
  float CenterY, CenterZ;
  // TiXL Pivot (Vector3)
  float PivotX;    // -> 16 bytes
  float PivotY, PivotZ;
  float W;         // TiXL W (Single, default 1.0) -> point weight
  float OrientAxisX;   // TiXL OrientationAxis (Vector3, default 0,1,0)
  float OrientAxisY;
  float OrientAxisZ;
  float OrientAngle;   // TiXL OrientationAngle (Single, default 0) in degrees
  // TiXL Scale (Single, default 1.0). .t3 routes Scale -> ScaleVector3.Factor, scaling the Size
  // Vector3 (HexGridPoints.t3:329-337). We bake that multiply into Size on the host (cook), so the
  // shader sees the already-scaled Size; this field carries the raw factor for golden/inspection.
  float Scale;
  float _pad1;         // -> 16 bytes (5th row)
};

enum HexGridBinding {
  HEXGRID_Points = 0,  // device SwPoint* (u0)
  HEXGRID_Params = 1,  // constant HexGridParams& (b0)
};

#ifndef __METAL_VERSION__
// CountX+Y+Z+SizeMode=16 | SizeX+Y+Z+CenterX=16 | CenterY+CenterZ+PivotX+PivotY=16 |
// PivotZ+W+AxisX+AxisY=16 | AxisZ+Angle+pad0+pad1=16 = 80 bytes
static_assert(sizeof(HexGridParams) == 80, "HexGridParams must be 80 bytes (5x16)");
#endif
