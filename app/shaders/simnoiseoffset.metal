// simnoiseoffset.metal — faithful Metal port of TiXL's SimNoiseOffset.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/sim/SimNoiseOffset.hlsl
// A count-preserving MODIFIER: per-point, samples a (simplex|curl)-noise field at the
// scaled position, displaces Position by it, and pre-multiplies Rotation by a quat that
// rotates the local +X probe toward (probe + noise). Writes ResultPoints (same count).
//
// TiXL parity (SimNoiseOffset.hlsl):
//   float3 variationOffset = hash31((float)(i.x%1234)/0.123) * Variation;
//   float3 pos = ResultPoints[i.x].Position*0.9;  // avoid simplex glitch at -1,0,0
//   float3 noiseLookup = (pos + variationOffset + Phase*float3(1,-1,0)) * Frequency;
//   noise = UseCurlNoise<0.5 ? snoiseVec3(noiseLookup) : curlNoise(noiseLookup);
//   noise *= Amount/100 * AmountDistribution;
//   float3 n = float3(1,0,0) * RotationLookupDistance;
//   ... noiseNormal (see fork note) ...
//   rotationFromDisplace = normalize(qFromVectors(normalize(n), normalize(n+noiseNormal)));
//   Position += noise; Rotation = qMul(rotationFromDisplace, Rotation);
//
// Forks from TiXL (named):
//   1. In-place RWStructuredBuffer<LegacyPoint> -> sw source+dest (const SourcePoints + writable
//      ResultPoints). Cleaner pattern; identical math.
//   2. UseCurlNoise<0.5 branch -> int UseCurlNoise param (0 snoise / 1 curl); same selection.
//   3. IsEnabled input omitted (sw has no per-op enable slot).
//   4. VERBATIM TiXL quirk preserved: the second noise sample ("noiseNormal") in the .hlsl
//      reuses `noiseLookup` (NOT `noiseLookupNormal`) inside snoiseVec3/curlNoise — i.e. the
//      normal-probe lookup vector is computed but never fed to the noise call. We port this
//      exactly (noiseNormal samples noiseLookup) to stay byte-faithful to the source; this is a
//      TiXL WIP artifact, not an sw improvement.
//   5. HLSL frac()->MSL fract() (none needed in this closure); float3 ctor identical.
#include <metal_stdlib>
#include "tixl_point.h"               // SwPoint (64B layout)
#include "simnoiseoffset_params.h"    // SimNoiseOffsetParams, SimNoiseOffsetBinding
#include "shared/hash.metal.h"        // hash31(float)
#include "shared/noise.metal.h"       // snoiseVec3, curlNoise
#include "shared/quat.metal.h"        // qFromVectors, qMul
using namespace metal;

kernel void simnoiseoffset(
    device const SwPoint* SourcePoints [[buffer(SIMNOISEOFFSET_SourcePoints)]],
    device       SwPoint* ResultPoints [[buffer(SIMNOISEOFFSET_ResultPoints)]],
    constant SimNoiseOffsetParams& P   [[buffer(SIMNOISEOFFSET_Params)]],
    uint3 i [[thread_position_in_grid]])
{
    uint idx = i.x;
    if (idx >= P.Count) return;

    SwPoint p = SourcePoints[idx];

    float3 AmountDistribution =
        float3(P.AmountDistributionX, P.AmountDistributionY, P.AmountDistributionZ);

    float3 variationOffset = hash31((float)(idx % 1234u) / 0.123f) * P.Variation;

    float3 pos = p.Position * 0.9f;  // avoid simplex noise glitch at -1,0,0
    float3 noiseLookup = (pos + variationOffset + P.Phase * float3(1.0f, -1.0f, 0.0f)) * P.Frequency;

    float3 noise = (P.UseCurlNoise < 1)
        ? snoiseVec3(noiseLookup) * P.Amount / 100.0f * AmountDistribution
        : curlNoise(noiseLookup)  * P.Amount / 100.0f * AmountDistribution;

    float3 n = float3(1.0f, 0.0f, 0.0f) * P.RotationLookupDistance;

    // Fork note #4: noiseLookupNormal is computed but TiXL feeds `noiseLookup` to the noise call.
    float3 posNormal = p.Position * 0.9f;  // avoid simplex noise glitch at -1,0,0
    float3 noiseLookupNormal =
        (posNormal + variationOffset + P.Phase * float3(0.0f, -1.0f, 0.0f)) * P.Frequency
        + n / P.Frequency;
    (void)noiseLookupNormal;  // intentionally unused — matches TiXL WIP source

    float3 noiseNormal = (P.UseCurlNoise < 1)
        ? snoiseVec3(noiseLookup) * P.Amount / 100.0f * AmountDistribution
        : curlNoise(noiseLookup)  * P.Amount / 100.0f * AmountDistribution;

    float4 rotationFromDisplace = normalize(qFromVectors(normalize(n), normalize(n + noiseNormal)));

    p.Position += noise;
    p.Rotation  = qMul(rotationFromDisplace, p.Rotation);

    ResultPoints[idx] = p;
}
