// addnoise.metal — faithful Metal port of TiXL's AddNoise.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/modify/AddNoise.hlsl
// A count-preserving MODIFIER: reads each SwPoint from SourcePoints, displaces its
// position by a simplex-noise field and updates its rotation to follow the displaced
// tangent frame. Writes the result to ResultPoints (same count, same stride).
//
// TiXL parity (AddNoise.hlsl):
//   - Noise function: snoiseVec3 (3 decorrelated simplex evaluations) from
//     shared/noise-functions.hlsl -> shared/noise.metal.h (already in project).
//   - Per-point variation: variationOffset = hash41u(i.x).xyz * Variation
//   - weight = StrengthMode==0 ? 1 : (StrengthMode==1 ? p.FX1 : p.FX2)
//   - GetTranslationAndRotation: computes position offset and new rotation quaternion
//     by looking up noise at the current position, then at two tangent-probed positions
//     (local X and Y directions offset by RotationLookupDistance), building an
//     orthonormal frame from the displaced tangent directions, then converting to quat.
//   - p.Position += offset; p.Rotation = newRotation
//
// Differences from HLSL (MSL port notes):
//   - snoiseVec3 is in shared/noise.metal.h (already present in project)
//   - hash41u is in shared/hash.metal.h (already present in project)
//   - qRotateVec3 / qFromMatrix3Precise are in shared/quat.metal.h
//   - HLSL frac() -> MSL fract(); no other differences in this closure
//   - cbuffers b0/b1 are flattened into one AddNoiseParams struct (no alignment trap)
//   - AmountDistribution is float3 (16-byte via pad in struct, not packed_float3)
//
// Fork from TiXL: the degenerate cross-product guard in GetTranslationAndRotation uses
// the refined version from the .hlsl file (the second, cleaner implementation):
//   if (all(abs(ez) < 1e-6)) { recompute from original xDir/yDir }
// This is already in the TiXL source; we copy it verbatim.
#include <metal_stdlib>
#include "tixl_point.h"              // SwPoint (64B layout)
#include "addnoise_params.h"         // AddNoiseParams, AddNoiseBinding
#include "shared/hash.metal.h"       // hash41u
#include "shared/noise.metal.h"      // snoiseVec3
#include "shared/quat.metal.h"       // qRotateVec3, qFromMatrix3Precise, qMul
using namespace metal;

// ---- noise sampling helper (direct port of TiXL AddNoise.hlsl GetNoise) ----
inline float3 GetNoise(float3 pos, float3 variationOffset,
                       float Frequency, float Phase, float Amount,
                       float3 AmountDistribution) {
    float3 noiseLookup = (pos * 0.91f + variationOffset + Phase) * Frequency;
    return snoiseVec3(noiseLookup) * Amount / 10.0f * AmountDistribution;
}

// ---- orientation update (direct port of TiXL AddNoise.hlsl GetTranslationAndRotation) ----
inline void GetTranslationAndRotation(
    float weight,
    float3 pointPos,
    float4 rotation,
    float3 variationOffset,
    float Frequency, float Phase, float Amount,
    float3 AmountDistribution,
    float  RotationLookupDistance,
    float3 NoiseOffset,
    thread float3& outOffset,
    thread float4& outRotation)
{
    // Base noise offset
    float3 noise3 = GetNoise(pointPos + NoiseOffset, variationOffset,
                             Frequency, Phase, Amount, AmountDistribution);
    outOffset = noise3 * weight;

    // Probe in local X direction
    float3 xDir = qRotateVec3(float3(RotationLookupDistance, 0.0f, 0.0f), rotation);
    float3 offsetAtPosX = GetNoise(pointPos + xDir, variationOffset,
                                   Frequency, Phase, Amount, AmountDistribution) * weight;
    float3 rotatedXDir = (pointPos + xDir + offsetAtPosX) - (pointPos + outOffset);

    // Probe in local Y direction
    float3 yDir = qRotateVec3(float3(0.0f, RotationLookupDistance, 0.0f), rotation);
    float3 offsetAtPosY = GetNoise(pointPos + yDir, variationOffset,
                                   Frequency, Phase, Amount, AmountDistribution) * weight;
    float3 rotatedYDir = (pointPos + yDir + offsetAtPosY) - (pointPos + outOffset);

    // Build right-handed orthonormal basis
    float3 ex = normalize(rotatedXDir);
    float3 ey = normalize(rotatedYDir);
    float3 ez = normalize(cross(ex, ey));

    // Degenerate guard (ex, ey nearly collinear)
    if (all(abs(ez) < 1e-6f)) {
        ex = normalize(xDir);
        ey = normalize(yDir);
        ez = normalize(cross(ex, ey));
    }

    // Recompute ey for orthogonality
    ey = normalize(cross(ez, ex));

    // MSL float3x3(ex,ey,ez) is column-major (col0=ex, col1=ey, col2=ez) which is the
    // "rotation matrix with basis vectors as columns" form that qFromMatrix3Precise expects.
    // TiXL HLSL: float3x3(ex,ey,ez) = row-major rows; transpose() flips to cols = basis.
    // MSL: float3x3(ex,ey,ez) = col-major cols already — no transpose needed.
    float3x3 orientationDest = float3x3(ex, ey, ez);
    outRotation = normalize(qFromMatrix3Precise(orientationDest));
}

kernel void addnoise(
    device const SwPoint* SourcePoints [[buffer(ADDNOISE_SourcePoints)]],
    device       SwPoint* ResultPoints [[buffer(ADDNOISE_ResultPoints)]],
    constant AddNoiseParams& P         [[buffer(ADDNOISE_Params)]],
    uint3 i [[thread_position_in_grid]])
{
    uint idx = i.x;
    if (idx >= P.Count) return;

    float3 variationOffset = hash41u(idx).xyz * P.Variation;

    SwPoint p = SourcePoints[idx];
    float weight = (P.StrengthMode == 0) ? 1.0f
                 : (P.StrengthMode == 1) ? p.FX1
                 :                         p.FX2;

    float3 AmountDistribution = float3(P.AmountDistributionX, P.AmountDistributionY, P.AmountDistributionZ);
    float3 NoiseOffset        = float3(P.NoiseOffsetX, P.NoiseOffsetY, P.NoiseOffsetZ);

    float3 offset;
    float4 newRotation = p.Rotation;

    GetTranslationAndRotation(
        weight,
        p.Position + variationOffset,
        p.Rotation,
        variationOffset,
        P.Frequency, P.Phase, P.Amount,
        AmountDistribution,
        P.RotationLookupDistance,
        NoiseOffset,
        offset,
        newRotation
    );

    p.Position += offset;
    p.Rotation  = newRotation;

    ResultPoints[idx] = p;
}
