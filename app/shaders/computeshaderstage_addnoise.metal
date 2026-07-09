// computeshaderstage_addnoise — ABI-repacked kernel driving AddNoise through the GENERIC
// ComputeShaderStage atom after the flat atom retires.
//
// Faithful MSL port of external/tixl/Operators/Lib/Assets/shaders/points/modify/AddNoise.hlsl with the
// generic const-buffer / SRV / UAV binding contract (NOT the fused scalar addnoise.metal's
// AddNoiseParams struct). Driven by the raw bytes FloatsToBuffer / IntsToBuffer assemble.
//
// ── HLSL cbuffer byte layout (FloatsToBuffer / IntsToBuffer, wire order == .t3 Params) ───────────
//   b0 (FloatsToBuffer, AddNoise.hlsl Params register b0), 11 floats tightly packed:
//        [0]=Amount(Strength) [1]=Frequency [2]=Phase [3]=Variation
//        [4]=AmountDistribution.x [5]=.y [6]=.z  [7]=RotationLookupDistance
//        [8]=NoiseOffset.x [9]=.y [10]=.z
//   b1 (IntsToBuffer, register b1): [0]=StrengthMode (FModes: None=0, F1=1, F2=2)
//   t0 (SRV) = SourcePoints (input Point bag) ; u0 (UAV) = ResultPoints (fresh StructuredBufferWithViews)
// Math is line-for-line the fused addnoise.metal (which is itself the faithful AddNoise.hlsl port);
// only the parameter SOURCE changes (raw cbuffer bytes vs a marshalled struct).
#include <metal_stdlib>
#include "tixl_point.h"                     // SwPoint (64B)
#include "computeshaderstage_params.h"      // CS_CB_BASE / CS_SRV_BASE / CS_UAV_BASE
#include "shared/hash.metal.h"              // hash41u
#include "shared/noise.metal.h"             // snoiseVec3
#include "shared/quat.metal.h"              // qRotateVec3, qFromMatrix3Precise
using namespace metal;

// GetNoise — direct port of TiXL AddNoise.hlsl GetNoise (same as addnoise.metal).
inline float3 csAddNoiseGetNoise(float3 pos, float3 variationOffset,
                                 float Frequency, float Phase, float Amount,
                                 float3 AmountDistribution) {
    float3 noiseLookup = (pos * 0.91f + variationOffset + Phase) * Frequency;
    return snoiseVec3(noiseLookup) * Amount / 10.0f * AmountDistribution;
}

// GetTranslationAndRotation — direct port (same as addnoise.metal).
inline void csAddNoiseGetTR(
    float weight, float3 pointPos, float4 rotation, float3 variationOffset,
    float Frequency, float Phase, float Amount, float3 AmountDistribution,
    float RotationLookupDistance, float3 NoiseOffset,
    thread float3& outOffset, thread float4& outRotation)
{
    float3 noise3 = csAddNoiseGetNoise(pointPos + NoiseOffset, variationOffset,
                                       Frequency, Phase, Amount, AmountDistribution);
    outOffset = noise3 * weight;

    float3 xDir = qRotateVec3(float3(RotationLookupDistance, 0.0f, 0.0f), rotation);
    float3 offsetAtPosX = csAddNoiseGetNoise(pointPos + xDir, variationOffset,
                                             Frequency, Phase, Amount, AmountDistribution) * weight;
    float3 rotatedXDir = (pointPos + xDir + offsetAtPosX) - (pointPos + outOffset);

    float3 yDir = qRotateVec3(float3(0.0f, RotationLookupDistance, 0.0f), rotation);
    float3 offsetAtPosY = csAddNoiseGetNoise(pointPos + yDir, variationOffset,
                                             Frequency, Phase, Amount, AmountDistribution) * weight;
    float3 rotatedYDir = (pointPos + yDir + offsetAtPosY) - (pointPos + outOffset);

    float3 ex = normalize(rotatedXDir);
    float3 ey = normalize(rotatedYDir);
    float3 ez = normalize(cross(ex, ey));
    if (all(abs(ez) < 1e-6f)) {
        ex = normalize(xDir);
        ey = normalize(yDir);
        ez = normalize(cross(ex, ey));
    }
    ey = normalize(cross(ez, ex));
    float3x3 orientationDest = float3x3(ex, ey, ez);
    outRotation = normalize(qFromMatrix3Precise(orientationDest));
}

kernel void computeshaderstage_addnoise(
    const device SwPoint* SourcePoints [[buffer(CS_SRV_BASE + 0)]],   // t0
    device SwPoint*       ResultPoints [[buffer(CS_UAV_BASE + 0)]],   // u0
    constant float*       cb0          [[buffer(CS_CB_BASE + 0)]],    // b0: 11 floats
    constant int*         cb1          [[buffer(CS_CB_BASE + 1)]],    // b1: StrengthMode
    constant uint&        numStructs   [[buffer(CS_CB_BASE + 3)]],    // dispatch bound (SRV element count)
    uint3 i [[thread_position_in_grid]])
{
    if (i.x >= numStructs) return;
    uint idx = i.x;

    float Amount    = cb0[0];
    float Frequency = cb0[1];
    float Phase     = cb0[2];
    float Variation = cb0[3];
    float3 AmountDistribution = float3(cb0[4], cb0[5], cb0[6]);
    float RotationLookupDistance = cb0[7];
    float3 NoiseOffset = float3(cb0[8], cb0[9], cb0[10]);
    int StrengthMode = cb1[0];

    float3 variationOffset = hash41u(idx).xyz * Variation;

    SwPoint p = SourcePoints[idx];
    float weight = (StrengthMode == 0) ? 1.0f
                 : (StrengthMode == 1) ? p.FX1
                 :                       p.FX2;

    float3 offset;
    float4 newRotation = p.Rotation;
    csAddNoiseGetTR(weight, p.Position + variationOffset, p.Rotation, variationOffset,
                    Frequency, Phase, Amount, AmountDistribution, RotationLookupDistance,
                    NoiseOffset, offset, newRotation);

    p.Position += offset;
    p.Rotation  = newRotation;
    ResultPoints[idx] = p;
}
