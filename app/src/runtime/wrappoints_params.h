// Shared host<->shader params for the TiXL-ported WrapPoints MODIFIER (lane P, batch 16).
// Mirrors external/tixl .../point/transform/WrapPoints.cs (.cs ports) +
// .../Assets/shaders/points/modify/WrapPoints.hlsl (.hlsl math).
// A count-preserving MODIFIER: each point's Position is wrapped (toroidally tiled) into a box
// of the given Size centered at Position, via floored modulo. Count is INHERITED from upstream.
//
// TiXL cbuffer (WrapPoints.hlsl register b1):
//   float3 Position; float __padding; float3 Size;
// (The b0 Transforms cbuffer of 10 camera matrices is unused by the kernel body — the math is
// pure point-space arithmetic, never multiplies by a matrix. We omit it entirely.)
//
// All Vector3 inputs become X/Y/Z scalars; kernel reassembles them (no packed_float3 trap, the
// particle_params.h discipline). 16-byte alignment maintained via static_assert.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct WrapPointsParams {
#ifdef __METAL_VERSION__
  uint  Count;   // inherited from the input bag (modifier: count comes from upstream Points)
  float _pad0;
  float _pad1;
  float _pad2;   // -> 16 bytes for the first row
#else
  uint32_t Count;
  float    _pad0;
  float    _pad1;
  float    _pad2;
#endif
  float PositionX, PositionY, PositionZ;  // .cs Position (Vector3), default (0,0,0) = box center
  float _pad3;                            // -> 16 bytes
  float SizeX, SizeY, SizeZ;              // .cs Size (Vector3), default (1,1,1) = box extents
  float _pad4;                            // -> 16 bytes
};

enum WrapPointsBinding {
  WRAPPOINTS_SourcePoints = 0,  // const device SwPoint* (t0)
  WRAPPOINTS_ResultPoints = 1,  // device SwPoint*       (u0)
  WRAPPOINTS_Params       = 2,  // constant WrapPointsParams& (b1 in TiXL; b0 here)
};

#ifndef __METAL_VERSION__
// Count(4)+3xpad(12)=16 | Position(12)+pad(4)=16 | Size(12)+pad(4)=16 = 48 bytes
static_assert(sizeof(WrapPointsParams) == 48, "WrapPointsParams must be 48 bytes (3x16)");
#endif
