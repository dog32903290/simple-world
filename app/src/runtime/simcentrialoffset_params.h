// Shared host<->shader params for the TiXL-ported SimCentricalOffset MODIFIER (sim family).
// Mirrors external/tixl/Operators/Lib/Assets/shaders/points/sim/SimCentricalOffset.hlsl.
// (TiXL spells the class "Centrical"; the centripetal-style force pulls/pushes points along the
// radial direction from Center, with magnitude clamped to +/-MaxAcceleration.) Count INHERITED.
//
// TiXL cbuffer Params (register b0):
//   float3 Center; float MaxAcceleration; float Acceleration; float DecayExponent;
// (.cs slot "Amount" feeds shader "Acceleration".)
//
// Flattened struct (no packed_float3 / float3). 16-byte alignment held via static_assert.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SimCentricalOffsetParams {
#ifdef __METAL_VERSION__
  uint  Count;
  float CenterX;
  float CenterY;
  float CenterZ;  // -> 16 bytes
#else
  uint32_t Count;
  float    CenterX;
  float    CenterY;
  float    CenterZ;
#endif
  float MaxAcceleration;  // .cs MaxAcceleration, default 1.0
  float Acceleration;     // .cs Amount -> shader Acceleration, default 0.04
  float DecayExponent;    // .cs DecayExponent, default 2.0
  float _pad0;            // pad -> 16-byte multiple
};

enum SimCentricalOffsetBinding {
  SIMCENTRICALOFFSET_SourcePoints = 0,  // const device SwPoint* (t0; source+dest fork)
  SIMCENTRICALOFFSET_ResultPoints = 1,  // device SwPoint*       (u0)
  SIMCENTRICALOFFSET_Params       = 2,  // constant SimCentricalOffsetParams& (b0)
};

#ifndef __METAL_VERSION__
// Count(4)+Center(12) = 16
// + MaxAcc(4)+Acc(4)+Decay(4)+pad0(4) = 16
// Total = 32 bytes
static_assert(sizeof(SimCentricalOffsetParams) == 32, "SimCentricalOffsetParams must be 32 bytes");
#endif
