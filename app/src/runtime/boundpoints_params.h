// Shared host<->shader params for the TiXL-ported BoundPoints MODIFIER (lane P, batch 16).
// Mirrors external/tixl .../point/transform/BoundPoints.cs (.cs ports) +
// .../Assets/shaders/points/modify/BoundPoints.hlsl (.hlsl math).
// A count-preserving MODIFIER: each point's Position is CLAMPED into an axis-aligned box of
// (Size * UniformScale) centered at Position. Count is INHERITED from upstream.
//
// TiXL cbuffer (BoundPoints.hlsl register b1):
//   float3 Position; float __padding; float3 Size; float UniformScale;
// (The b0 Transforms cbuffer of 10 camera matrices is unused by the kernel body. We omit it.)
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

struct BoundPointsParams {
#ifdef __METAL_VERSION__
  uint  Count;          // inherited from the input bag (modifier: count from upstream Points)
  float UniformScale;   // .cs UniformScale (Single), default 1.0 — multiplies Size
  float _pad0;
  float _pad1;          // -> 16 bytes for the first row
#else
  uint32_t Count;
  float    UniformScale;
  float    _pad0;
  float    _pad1;
#endif
  float PositionX, PositionY, PositionZ;  // .cs Position (Vector3), default (0,0,0) = box center
  float _pad2;                            // -> 16 bytes
  float SizeX, SizeY, SizeZ;              // .cs Size (Vector3), default (1,1,1) = box extents
  float _pad3;                            // -> 16 bytes
};

enum BoundPointsBinding {
  BOUNDPOINTS_SourcePoints = 0,  // const device SwPoint* (t0)
  BOUNDPOINTS_ResultPoints = 1,  // device SwPoint*       (u0)
  BOUNDPOINTS_Params       = 2,  // constant BoundPointsParams& (b1 in TiXL; b0 here)
};

#ifndef __METAL_VERSION__
// Count(4)+UniformScale(4)+2xpad(8)=16 | Position(12)+pad(4)=16 | Size(12)+pad(4)=16 = 48 bytes
static_assert(sizeof(BoundPointsParams) == 48, "BoundPointsParams must be 48 bytes (3x16)");
#endif
