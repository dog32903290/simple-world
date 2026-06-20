// Shared host<->shader params for the TiXL-ported SimForceOffset MODIFIER (sim family).
// Mirrors external/tixl/Operators/Lib/Assets/shaders/points/sim/SimForceOffset.hlsl.
// A count-preserving modifier: applies (Gravity + radialForce) * effect to Position, where the
// radial force decays with distance from Center and `effect` is a radius/falloff window. Count INHERITED.
//
// TiXL cbuffer Params (register b0):
//   float3 Center; float Radius;
//   float RadiusFallOff; float RadialForce; float UseWForMass; float Variation;
//   float3 Gravity; float ForceDecayRate;
//
// Flattened struct (no packed_float3 / float3). 16-byte alignment held via static_assert.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SimForceOffsetParams {
#ifdef __METAL_VERSION__
  uint  Count;
  float CenterX;
  float CenterY;
  float CenterZ;   // -> 16 bytes
#else
  uint32_t Count;
  float    CenterX;
  float    CenterY;
  float    CenterZ;
#endif
  float Radius;         // .cs Radius, default 999.0
  float RadiusFallOff;  // .cs RadiusFallOff, default 0.0
  float RadialForce;    // .cs RadialForce, default 0.0
  float UseWForMass;    // .cs UseWForMass, default 0.0 (declared; unused by .hlsl math)
  // 16-byte row above
  float GravityX;       // .cs Gravity (Vector3), default (0,0,0)
  float GravityY;
  float GravityZ;
  float ForceDecayRate; // .cs ForceDecayRate, default 1.0
  // 16-byte row above
  float Variation;      // .cs Variation, default 0.0
  float _pad0;
  float _pad1;
  float _pad2;          // pad -> 16-byte multiple
};

enum SimForceOffsetBinding {
  SIMFORCEOFFSET_SourcePoints = 0,  // const device SwPoint* (t0; source+dest fork)
  SIMFORCEOFFSET_ResultPoints = 1,  // device SwPoint*       (u0)
  SIMFORCEOFFSET_Params       = 2,  // constant SimForceOffsetParams& (b0)
};

#ifndef __METAL_VERSION__
// Count(4)+Center(12) = 16
// + Radius(4)+RadiusFallOff(4)+RadialForce(4)+UseWForMass(4) = 16
// + Gravity(12)+ForceDecayRate(4) = 16
// + Variation(4)+3xpad(12) = 16
// Total = 64 bytes
static_assert(sizeof(SimForceOffsetParams) == 64, "SimForceOffsetParams must be 64 bytes (4x16)");
#endif
