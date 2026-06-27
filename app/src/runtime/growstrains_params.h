// growstrains_params.h — host/GPU shared cbuffer layout for GrowStrains.
// Reference: external/tixl/Operators/Lib/Assets/shaders/points/sim/GrowStrains.hlsl
//   cbuffer Params : register(b0) {
//     float Variation; float NoiseAmount; float Frequency; float Phase;
//     float3 NoiseDistribution; float RotationLookupDistance;
//     float Length; float Width; float NoiseDensity;
//   }
//
// GrowStrains is a STATELESS 2-input transform (PointsA t0 + PointsB t1 + GrowthMap t2 texture →
// ResultPoints). Count = (PointsA.N + 1) * PointsB.N (the cartesian product with a NaN separator row
// per source loop, the same count-product family as RepeatAtPoints). NoiseAmount/Frequency drive a
// simplex-noise displacement; the GrowthMap (sampled at float2(B.W, 1-A.W)) gates the strand length
// (d = saturate(r-0.05)) — an unwired/black GrowthMap → d≈0 → all NaN (degenerate, faithful).
//
// metal-cpp-discipline: this struct goes to a Metal constant buffer. The HLSL float3 NoiseDistribution
// is 16-byte aligned in the HLSL cbuffer (it starts a new 16-byte row after Phase). To match MSL's
// constant-buffer packing we lay the host struct out as four 16-byte rows; NoiseDistribution is a
// packed 3-float + RotationLookupDistance fills the 4th lane of its row (same as the HLSL row). The
// trailing Length/Width/NoiseDensity occupy the last row. static_assert pins the total size.
#pragma once
#ifndef __METAL_VERSION__
  #include <cstdint>
#endif

enum {
  GROWSTRAINS_PointsA      = 0,  // const device SwPoint* (t0)
  GROWSTRAINS_PointsB      = 1,  // const device SwPoint* (t1)
  GROWSTRAINS_Result       = 2,  // device SwPoint* (u0)
  GROWSTRAINS_Params       = 3,  // GrowStrainsParams (b0)
  GROWSTRAINS_CountA       = 4,  // uint (PointsA count, so the kernel can size the loop without GetDimensions)
  GROWSTRAINS_CountB       = 5,  // uint (PointsB count)
  GROWSTRAINS_ResultCount  = 6,  // uint (the product, for the i>=count guard)
  // GrowthMap texture is bound at texture index 0 + sampler index 0.
};

struct GrowStrainsParams {
  float Variation;              // row 0.x
  float NoiseAmount;            // row 0.y
  float Frequency;              // row 0.z
  float Phase;                  // row 0.w
  float NoiseDistributionX;     // row 1.x  (HLSL float3 NoiseDistribution starts row 1)
  float NoiseDistributionY;     // row 1.y
  float NoiseDistributionZ;     // row 1.z
  float RotationLookupDistance; // row 1.w
  float Length;                 // row 2.x
  float Width;                  // row 2.y
  float NoiseDensity;           // row 2.z
  float _pad;                   // row 2.w (pad to a full 16-byte row)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(GrowStrainsParams) == 48, "GrowStrainsParams: 3 x 16-byte rows (HLSL cbuffer layout)");
#endif
