// growstrains.metal — Metal port of TiXL GrowStrains.hlsl
// Reference: external/tixl/Operators/Lib/Assets/shaders/points/sim/GrowStrains.hlsl
//
// A STATELESS 2-input transform: PointsA (t0) × PointsB (t1) cartesian product (+1 NaN separator row per
// source loop), with a GrowthMap texture (t2) gating each strand's length. Output count = (CountA+1)*CountB.
// TiXL uses LegacyPoint (Position/W/Rotation/Color/Stretch/Selected) — byte-identical 64B layout to SwPoint
// (W↔FX1@12, Stretch↔Scale@48, Selected↔FX2@60), so we reuse SwPoint and read/write .FX1 as LegacyPoint.W.
//
// FAITHFUL: the kernel reproduces GrowStrains.hlsl line-for-line. It writes ONLY Position, W(=FX1) and
// Rotation for non-separator points (exactly as the HLSL); separator rows get W=NaN. Other attributes are
// left as the output buffer holds them (matches TiXL — the .hlsl never writes them).

#include <metal_stdlib>
using namespace metal;

#include "../src/runtime/tixl_point.h"
#include "../src/runtime/growstrains_params.h"
#include "shared/quat.metal.h"   // qMul, qRotateVec3
#include "shared/noise.metal.h"  // snoiseVec3
#include "shared/hash.metal.h"   // hash31

// Faithful port of TiXL quat-functions.hlsl qFromMatrix3 (non-precise). HLSL _mRC = row R, col C;
// MSL float3x3 is column-major (m[col][row]), so _mRC == m[C][R].
inline float4 gs_qFromMatrix3(float3x3 m) {
    float w = sqrt(max(0.0f, 1.0f + m[0][0] + m[1][1] + m[2][2])) / 2.0f;
    float w4 = 4.0f * w;
    float x = (m[1][2] - m[2][1]) / w4;  // _m21 - _m12
    float y = (m[2][0] - m[0][2]) / w4;  // _m02 - _m20
    float z = (m[0][1] - m[1][0]) / w4;  // _m10 - _m01
    return float4(x, y, z, w);
}

static float3 gs_GetNoise(float3 pos, constant GrowStrainsParams& P) {
    float3 noiseLookup = (pos + P.Phase) * P.Frequency;
    float3 dist = float3(P.NoiseDistributionX, P.NoiseDistributionY, P.NoiseDistributionZ);
    return snoiseVec3(noiseLookup) * P.NoiseAmount * dist;
}

static void gs_GetTranslationAndRotation(float weight, float3 pointPos, float4 rotation,
                                         constant GrowStrainsParams& P,
                                         thread float3& offset, thread float4& newRotation) {
    offset = gs_GetNoise(pointPos, P) * weight;

    float3 xDir = qRotateVec3(float3(P.RotationLookupDistance, 0, 0), rotation);
    float3 offsetAtPosXDir = gs_GetNoise(pointPos + xDir, P) * weight;
    float3 rotatedXDir = (pointPos + xDir + offsetAtPosXDir) - (pointPos + offset);

    float3 yDir = qRotateVec3(float3(0, P.RotationLookupDistance, 0), rotation);
    float3 offsetAtPosYDir = gs_GetNoise(pointPos + yDir, P) * weight;
    float3 rotatedYDir = (pointPos + yDir + offsetAtPosYDir) - (pointPos + offset);

    float3 rotatedXDirNormalized = normalize(rotatedXDir);
    float3 rotatedYDirNormalized = normalize(rotatedYDir);

    float3 crossXY = cross(rotatedXDirNormalized, rotatedYDirNormalized);
    float3x3 orientationDest = float3x3(
        rotatedXDirNormalized,
        cross(crossXY, rotatedXDirNormalized),
        crossXY);

    newRotation = normalize(gs_qFromMatrix3(transpose(orientationDest)));
}

[[kernel]]
void growstrains(
    const device SwPoint*        PointsA     [[buffer(GROWSTRAINS_PointsA)]],
    const device SwPoint*        PointsB     [[buffer(GROWSTRAINS_PointsB)]],
    device       SwPoint*        ResultPoints[[buffer(GROWSTRAINS_Result)]],
    constant     GrowStrainsParams& P        [[buffer(GROWSTRAINS_Params)]],
    constant     uint&           CountA       [[buffer(GROWSTRAINS_CountA)]],
    constant     uint&           CountB       [[buffer(GROWSTRAINS_CountB)]],
    constant     uint&           ResultCount  [[buffer(GROWSTRAINS_ResultCount)]],
    texture2d<float>             GrowthMap    [[texture(0)]],
    sampler                      texSampler   [[sampler(0)]],
    uint3                        tid          [[thread_position_in_grid]])
{
    uint i = tid.x;
    if (i >= ResultCount) return;

    uint sourceCount = CountA + 1u;  // +1 for NaN-Separator
    uint targetPosCount = CountB;
    if (sourceCount == 0u || targetPosCount == 0u) return;

    uint sourceIndex = i % sourceCount;

    if (sourceIndex == sourceCount - 1u) {
        ResultPoints[i].FX1 = sqrt(-1.0f);  // LegacyPoint.W = NaN separator
    } else {
        uint targetIndex = (i / sourceCount) % targetPosCount;
        SwPoint A = PointsA[sourceIndex];
        SwPoint B = PointsB[targetIndex];

        float3 pLocal = qRotateVec3(float3(A.Position), normalize(B.Rotation));

        float age = B.FX1;  // LegacyPoint.W
        float w = A.FX1;    // LegacyPoint.W

        float4 attributes = GrowthMap.sample(texSampler, float2(age, 1.0f - w), level(0));
        float d = saturate(attributes.r - 0.05f);
        if (d < 0.001f)
            d = sqrt(-1.0f);

        float4 rotation = qMul(normalize(A.Rotation), normalize(B.Rotation));

        float noiseWeight = attributes.g;
        float3 offset;
        float4 newRotation;
        float3 variationOffset = hash31((float)targetIndex) * P.Variation;

        gs_GetTranslationAndRotation(noiseWeight, pLocal * P.NoiseDensity + variationOffset, rotation,
                                     P, offset, newRotation);

        ResultPoints[i].Position = packed_float3(pLocal * P.Length + float3(B.Position) + offset);
        ResultPoints[i].FX1 = d * P.Width;   // LegacyPoint.W
        ResultPoints[i].Rotation = newRotation;
    }
}
