// GridPoints (point-operator graph, lane A) — faithful port of TiXL
// .../points/generate/GridPoints.hlsl SHAPE math (a 3D grid of points).
// Source of truth: external/tixl/Operators/Lib/Assets/shaders/points/generate/GridPoints.hlsl
//
// All-scalar params (NO packed_float3) so there are zero cbuffer alignment traps; the
// shader reassembles float3(SizeX,Y,Z) / float3(CenterX,Y,Z) / float3(PivotX,Y,Z).
// Count is per-axis (CountX/Y/Z); total point count = CountX*CountY*CountZ (host-computed
// into ctx.count). SizeMode (0=Cell padding / 1=Bounds volume) ports faithfully.
//
// PARAM-COMPLETION GATE: TiXL's Tiling (Cartesian/Triangular/HoneyComb/Diagonal) is still baked
// to Cartesian (the other three branches are deferred — parityNotes), but the per-point ATTRIBUTE
// inputs (Scale multiplier, Color(Vector4), F1/F2, quaternion orientation OrientationAxis/Angle)
// are now real struct members the cook fills from the NodeSpec and the kernel reads (was baked to
// TiXL white/identity/0). All-scalar layout (NO packed_float3) so there are zero cbuffer alignment
// traps; the shader reassembles float3(SizeX,Y,Z) etc on read.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct GridParams {
#ifdef __METAL_VERSION__
  uint CountX;
  uint CountY;
  uint CountZ;
  uint SizeMode;  // 0 = Cell (padding between pts), 1 = Bounds (size of volume)
#else
  uint32_t CountX;
  uint32_t CountY;
  uint32_t CountZ;
  uint32_t SizeMode;
#endif
  float SizeX;    // TiXL Size (Vector3) × Scale — per-axis extent (Cell: spacing, Bounds: total)
  float SizeY;
  float SizeZ;
  float CenterX;  // TiXL Center (Vector3) — translation added to every point
  float CenterY;
  float CenterZ;
  float PivotX;   // TiXL Pivot (Vector3) — grid anchor offset (default 0 -> centered)
  float PivotY;
  float PivotZ;
  float PointScale;  // TiXL PointScale -> SwPoint.Scale
  // ---- param-completion fan-out: per-point attribute inputs (were baked) -----------------
  float FX1;      // TiXL F1 (Single) -> SwPoint.FX1. .t3 default 1.0
  float FX2;      // TiXL F2 (Single) -> SwPoint.FX2. .t3 default 1.0
  float ColorR;   // TiXL Color (Vector4) -> SwPoint.Color. .t3 default white
  float ColorG;
  float ColorB;
  float ColorA;
  float OrientAxisX;  // TiXL OrientationAxis (Vector3) -> qFromAngleAxis. .t3 default (1,0,0)
  float OrientAxisY;
  float OrientAxisZ;
  float OrientAngle;  // TiXL OrientationAngle (degrees). .t3 default 0.0 -> 96 bytes (16-byte multiple)
};

enum GridBinding {
  GRID_Points = 0,  // device SwPoint* (u0)
  GRID_Params = 1,  // constant GridParams& (b0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(GridParams) == 96, "GridParams must be a 16-byte multiple (96 bytes)");
#endif
