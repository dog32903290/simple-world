// Shared host<->shader params for the TiXL-ported AddNoise MODIFIER (lane A, batch 15).
// Mirrors external/tixl .../Assets/shaders/points/modify/AddNoise.hlsl.
// A count-preserving modifier: reads input bag, adds simplex-noise-driven position and
// rotation perturbation, writes the SAME count back. Count is INHERITED from upstream.
//
// TiXL cbuffer layout (two registers b0/b1):
//   b0 (floats): Amount, Frequency, Phase, Variation, AmountDistribution(v3),
//                RotationLookupDistance, NoiseOffset(v3), __padding
//   b1 (int):    StrengthMode (FModes: None=0, F1=1, F2=2)
//
// We flatten to one struct (no packed_float3 / matrix traps — the particle_params.h
// discipline). All Vector3 inputs become X/Y/Z scalars; kernel reassembles them.
// 16-byte alignment is maintained via static_assert.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct AddNoiseParams {
#ifdef __METAL_VERSION__
  uint   Count;           // inherited from upstream bag (modifier: count from input)
  int    StrengthMode;    // FModes: 0=None(weight=1), 1=F1(p.FX1), 2=F2(p.FX2)
  float  _pad0;           // align
  float  _pad1;           // align -> 16 bytes for first row
#else
  uint32_t Count;
  int32_t  StrengthMode;
  float    _pad0;
  float    _pad1;
#endif
  // b0 float params (TiXL cbuffer Params register b0)
  float Amount;                    // .cs Strength, default 1.0
  float Frequency;                 // .cs Frequency, default 1.0
  float Phase;                     // .cs Phase, default 0.0
  float Variation;                 // .cs Variation, default 0.0
  float AmountDistributionX;       // .cs AmountDistribution (Vector3), default (1,1,1)
  float AmountDistributionY;
  float AmountDistributionZ;
  float RotationLookupDistance;    // .cs RotationLookupDistance, default 0.25
  float NoiseOffsetX;              // .cs NoiseOffset (Vector3), default (0,0,0)
  float NoiseOffsetY;
  float NoiseOffsetZ;
  float _pad2;                     // pad to 16-byte multiple
};

enum AddNoiseBinding {
  ADDNOISE_SourcePoints = 0,  // const device SwPoint* (t0)
  ADDNOISE_ResultPoints = 1,  // device SwPoint*       (u0)
  ADDNOISE_Params       = 2,  // constant AddNoiseParams& (b0)
};

#ifndef __METAL_VERSION__
// Count(4) + StrengthMode(4) + 2xpad(8) = 16
// + Amount(4)+Freq(4)+Phase(4)+Variation(4) = 16
// + AmountDist(12)+RotLookup(4) = 16
// + NoiseOffset(12)+pad2(4) = 16
// Total = 64 bytes
static_assert(sizeof(AddNoiseParams) == 64, "AddNoiseParams must be 64 bytes (4x16)");
#endif
