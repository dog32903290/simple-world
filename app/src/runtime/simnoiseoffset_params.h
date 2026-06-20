// Shared host<->shader params for the TiXL-ported SimNoiseOffset MODIFIER (sim family, sw-node-batch).
// Mirrors external/tixl/Operators/Lib/Assets/shaders/points/sim/SimNoiseOffset.hlsl.
// A count-preserving modifier: reads input bag, displaces Position by a (simplex|curl)-noise
// field and pre-multiplies Rotation by a quat that follows the displaced tangent. Count INHERITED.
//
// TiXL cbuffer Params (register b0):
//   float Amount; float Frequency; float Phase; float Variation;
//   float3 AmountDistribution; float RotationLookupDistance; float UseCurlNoise;
//
// We flatten to one struct (no packed_float3 / float3 — the metal-cpp-discipline). Vector3 inputs
// become X/Y/Z scalars; kernel reassembles. 16-byte alignment held via static_assert.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SimNoiseOffsetParams {
#ifdef __METAL_VERSION__
  uint  Count;          // inherited from upstream bag
  int   UseCurlNoise;   // .cs UseCurlNoise (bool>=0.5 -> 1=curlNoise, 0=snoiseVec3)
  float _pad0;
  float _pad1;          // -> 16 bytes for first row
#else
  uint32_t Count;
  int32_t  UseCurlNoise;
  float    _pad0;
  float    _pad1;
#endif
  // b0 float params
  float Amount;                 // .cs Amount, default 0.2
  float Frequency;              // .cs Frequency, default 1.0
  float Phase;                  // .cs Phase, default 0.0
  float Variation;              // .cs Variation, default 0.0
  float AmountDistributionX;    // .cs AmountDistribution (Vector3), default (1,1,1)
  float AmountDistributionY;
  float AmountDistributionZ;
  float RotationLookupDistance; // .cs RotLookupDistance -> shader RotationLookupDistance, default 2.0
};

enum SimNoiseOffsetBinding {
  SIMNOISEOFFSET_SourcePoints = 0,  // const device SwPoint* (t0; sw source+dest fork of in-place RW)
  SIMNOISEOFFSET_ResultPoints = 1,  // device SwPoint*       (u0)
  SIMNOISEOFFSET_Params       = 2,  // constant SimNoiseOffsetParams& (b0)
};

#ifndef __METAL_VERSION__
// Count(4)+UseCurlNoise(4)+2xpad(8) = 16
// + Amount(4)+Freq(4)+Phase(4)+Variation(4) = 16
// + AmountDist(12)+RotLookup(4) = 16
// Total = 48 bytes
static_assert(sizeof(SimNoiseOffsetParams) == 48, "SimNoiseOffsetParams must be 48 bytes (3x16)");
#endif
