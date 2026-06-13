// Shared host<->shader params for the TiXL-ported WrapPointPosition MODIFIER (batch 19).
// Mirrors external/tixl .../point/transform/WrapPointPosition.cs (.cs ports) +
// .../Assets/shaders/points/modify/WrapPointPosition.hlsl (.hlsl math).
//
// WrapPointPosition is a CUBE FOLD — distinct from WrapPoints (floored-mod torus):
//   p_local = p - Center
//   for each axis: if |p_local[a]| > padded[a], offsetFactor[a] = +/-1 else 0
//   wrapped = p_local + Size * offsetFactor + Center
//   (padded = halfSize + Padding, Padding = Size.x * 0.1)
//   W channel: NaN on wrapped (WriteLineBreaks), else edge-fade = product of saturate(distToEdge*10).
//
// TiXL cbuffer (WrapPointPosition.hlsl register b0):
//   float3 Center; float UseCamera; float3 Size; float WriteLineBreaks;
//   (Transforms cbuffer b1 is only needed for UseCamera path; we bake UseCamera=0.)
//
// Fork: UseCamera (needs camera matrix) baked to 0 (no camera in cook ctx).
//       WriteLineBreaks baked to 0 (W edge-fade path only).
//
// All Vector3 inputs X/Y/Z scalars; no packed_float3 trap. 16-byte aligned.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct WrapPointPositionParams {
#ifdef __METAL_VERSION__
  uint  Count;   // inherited from input bag (modifier: count from upstream Points)
  float _pad0;
  float _pad1;
  float _pad2;   // -> 16 bytes
#else
  uint32_t Count;
  float    _pad0;
  float    _pad1;
  float    _pad2;
#endif
  float CenterX, CenterY, CenterZ;  // .cs Position port (renamed Center in .hlsl cbuffer)
  float _pad3;                       // -> 16 bytes
  float SizeX, SizeY, SizeZ;        // .cs Size (Vector3), default (2,2,2)
  float _pad4;                       // -> 16 bytes
};

enum WrapPointPositionBinding {
  WRAPPOINTPOS_SourcePoints = 0,  // const device SwPoint* (t0)
  WRAPPOINTPOS_ResultPoints = 1,  // device SwPoint*       (u0)
  WRAPPOINTPOS_Params       = 2,  // constant WrapPointPositionParams& (b0)
};

#ifndef __METAL_VERSION__
// Count(4)+3xpad(12)=16 | Center(12)+pad(4)=16 | Size(12)+pad(4)=16 = 48 bytes
static_assert(sizeof(WrapPointPositionParams) == 48, "WrapPointPositionParams must be 48 bytes");
#endif
