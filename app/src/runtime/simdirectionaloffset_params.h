// Shared host<->shader params for the TiXL-ported SimDirectionalOffset MODIFIER (sim family).
// Mirrors external/tixl/Operators/Lib/Assets/shaders/points/sim/SimDirectionalOffset.hlsl.
// A count-preserving modifier: pushes Position along Direction*Amount (Mode 0), or encodes the
// push into the velocity-in-Rotation.w channel (Mode 1). Count INHERITED.
//
// TiXL cbuffer Params (register b0):
//   float3 Direction; float Amount; float RandomAmount; float Mode;
//
// Flattened struct (no packed_float3 / float3). 16-byte alignment held via static_assert.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SimDirectionalOffsetParams {
#ifdef __METAL_VERSION__
  uint  Count;
  float DirectionX;
  float DirectionY;
  float DirectionZ;  // -> 16 bytes
#else
  uint32_t Count;
  float    DirectionX;
  float    DirectionY;
  float    DirectionZ;
#endif
  float Amount;        // .cs Amount, default 1.0
  float RandomAmount;  // .cs RandomAmount, default 0.0
  float Mode;          // .cs Mode (enum int cooked as float): 0=Legacy(position), 1=EncodeInRotation
  float _pad0;         // pad -> 16-byte multiple
};

enum SimDirectionalOffsetBinding {
  SIMDIRECTIONALOFFSET_SourcePoints = 0,  // const device SwPoint* (t0; source+dest fork)
  SIMDIRECTIONALOFFSET_ResultPoints = 1,  // device SwPoint*       (u0)
  SIMDIRECTIONALOFFSET_Params       = 2,  // constant SimDirectionalOffsetParams& (b0)
};

#ifndef __METAL_VERSION__
// Count(4)+Direction(12) = 16
// + Amount(4)+RandomAmount(4)+Mode(4)+pad0(4) = 16
// Total = 32 bytes
static_assert(sizeof(SimDirectionalOffsetParams) == 32,
              "SimDirectionalOffsetParams must be 32 bytes");
#endif
