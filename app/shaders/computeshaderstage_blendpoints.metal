// computeshaderstage_blendpoints — ABI-repacked kernel driving BlendPoints through the GENERIC
// ComputeShaderStage atom after the flat atom retires.
//
// Faithful MSL port of external/tixl/Operators/Lib/Assets/shaders/points/combine/BlendPoints.hlsl with the
// generic const-buffer / DUAL-SRV / UAV binding contract (NOT the fused scalar blendpoints.metal's
// BlendPointsParams struct). Driven by the raw bytes FloatsToBuffer assembles + the per-SRV count aux array.
//
// BlendPoints is an INDEX-PAIRED 2-input blend: each output point (one per PointsA element = resultCount)
// lerps PointsA[i] toward PointsB[i] by a per-point factor f selected by BlendMode, with optional Scatter
// jitter, lerping every SwPoint channel (Rotation via qSlerp). resultCount = countA.
//
// ── HLSL cbuffer / SRV / UAV layout (wire order == .t3 FloatsToBuffer / ComputeShaderStage MultiInputs) ─
//   b0 (BlendPoints.hlsl Params register b0, FloatsToBuffer — tight float[], no pad):
//        cb0[0]=BlendFactor cb0[1]=BlendMode cb0[2]=PairingMode cb0[3]=Width cb0[4]=Scatter
//        (BlendPoints.t3 FloatsToBuffer.Params order: BlendFactor / IntToFloat(BlendMode) /
//         IntToFloat(Pairing) / RangeWidth / Scatter, Connections:178/184/190/196/202 == the cbuffer
//         order verbatim; the two IntToFloat nodes are value-preserving enum→float casts.)
//   t0 (SRV) = PointsA (PointsA_) ; t1 (SRV) = PointsB (PointsB_) ; u0 (UAV) = ResultPoints (fresh SBV, Stride 64)
//        (ShaderResources wire order PointsA-before-PointsB, BlendPoints.t3 Connections:238/244.)
//   numStructs (CS_CB_BASE+3) = resultCount = countA (the .t3 sizes the output SBV + dispatch from
//        GetSRVProperties(PointsA), Connections:136/256). srvCounts (CS_SRVCOUNT_BASE) = [countA, countB]:
//        the generic-seam AUX extension (computestage-per-srv-elementcount) so this DUAL-SRV kernel can read
//        countB for its Adjust thinning + B zero-fill — the seam otherwise passes only the front count.
// Math is line-for-line the fused blendpoints.metal (faithful, fixer-aligned BlendPoints.hlsl port); only
// the parameter/count SOURCE changes (raw cbuffer bytes + aux counts vs a marshalled struct).
//
// ── NAMED FORKS (preserved verbatim from blendpoints.metal) ───────────────────────────────────────────
//   fork[t-singular]: t = i.x/(resultCount-1) faithful float divide (resultCount==1 → div-by-0 as in HLSL).
//   fork[guard-off-by-one]: guard `i >= resultCount` (stricter than TiXL's `i.x > resultCount`, same
//     observable result — the extra HLSL write at i.x==resultCount is a discarded OOB write).
//   b-zero-fill (aligned to TiXL, no longer a fork): PointsB[i.x] OOB → all-zero element (D3D SRV contract),
//     reproduced as (bIndex < countB ? PointsB[bIndex] : SwPoint{}).
//   Point.W == SwPoint.FX1; final `ResultPoints[i].FX1 = f` overwrite kept verbatim.
#include <metal_stdlib>
#include "tixl_point.h"                     // SwPoint (64B), packed_float3
#include "computeshaderstage_params.h"      // CS_CB_BASE / CS_SRV_BASE / CS_UAV_BASE / CS_SRVCOUNT_BASE
#include "shared/quat.metal.h"              // qSlerp (faithful TiXL qSlerp port)
#include "shared/hash.metal.h"              // hash11 (verbatim TiXL Dave-Hoskins port)
using namespace metal;

// TiXL SmootherStep (BlendPoints.hlsl :18-22), verbatim.
inline float csbp_smootherStep(float x) {
  x = saturate(x);
  return x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
}

kernel void computeshaderstage_blendpoints(
    const device SwPoint* PointsA      [[buffer(CS_SRV_BASE + 0)]],   // t0 (PointsA_)
    const device SwPoint* PointsB      [[buffer(CS_SRV_BASE + 1)]],   // t1 (PointsB_)
    device SwPoint*       ResultPoints [[buffer(CS_UAV_BASE + 0)]],   // u0
    constant float*       cb0          [[buffer(CS_CB_BASE + 0)]],    // b0: 5 floats
    constant uint&        numStructs   [[buffer(CS_CB_BASE + 3)]],    // = resultCount = countA
    constant uint*        srvCounts    [[buffer(CS_SRVCOUNT_BASE)]],  // [countA, countB, ...]
    uint3 tid [[thread_position_in_grid]])
{
    uint i = tid.x;
    uint resultCount = numStructs;
    uint countA = srvCounts[0];    // == numStructs
    uint countB = srvCounts[1];

    // fork[guard-off-by-one]: keep Metal in-bounds (TiXL uses strict `i.x > resultCount`).
    if (i >= resultCount) return;

    float BlendFactor = cb0[0];
    float BlendMode   = cb0[1];
    float PairingMode = cb0[2];
    float Width       = cb0[3];
    float Scatter     = cb0[4];

    uint aIndex = i;
    uint bIndex = i;

    // fork[t-singular]: faithful float divide (resultCount==1 → div by 0, as in HLSL).
    float t = (float)i / ((float)resultCount - 1.0f);

    // Adjust pairing: count-mismatch thinning early-return (BlendPoints.hlsl :39-52).
    if (PairingMode > 0.5f && countA != countB) {
        uint firstIxA = (uint)(((float)aIndex * (float)resultCount) / (float)countA);
        uint firstIxB = (uint)(((float)bIndex * (float)resultCount) / (float)countB);
        uint firstIx  = max(firstIxA, firstIxB);
        if (i > firstIx) return;
    }

    SwPoint A = PointsA[aIndex];
    SwPoint B = (bIndex < countB) ? PointsB[bIndex] : SwPoint{};  // b-zero-fill (D3D SRV OOB contract)

    float f = 0.0f;
    if (BlendMode < 0.5f) {
        f = BlendFactor;
    } else if (BlendMode < 1.5f) {
        f = A.FX1;
    } else if (BlendMode < 2.5f) {
        f = (1.0f - B.FX1);
    } else if (BlendMode < 3.5f) {
        f = 1.0f - saturate((t - BlendFactor) / Width - BlendFactor + 1.0f);       // Ranged
    } else {
        float b = fmod(BlendFactor, 2.0f);                                          // RangedSmooth
        if (b > 1.0f) { b = 2.0f - b; t = 1.0f - t; }
        f = 1.0f - csbp_smootherStep(saturate((t - b) / Width - b + 1.0f));
    }

    float fallOffFromCenter = smoothstep(0.0f, 1.0f, 1.0f - fabs(f - 0.5f) * 2.0f);
    f += (hash11(t) - 0.5f) * Scatter * fallOffFromCenter;

    float3 aScale = float3(A.Scale.x, A.Scale.y, A.Scale.z);
    float3 bScale = float3(B.Scale.x, B.Scale.y, B.Scale.z);
    bool noBlend = isnan(aScale.x * bScale.x);

    f = noBlend ? (f < 0.5f ? 0.0f : 1.0f) : f;

    SwPoint out;
    float3 outScale = noBlend ? (f < 0.1f ? aScale : bScale) : mix(aScale, bScale, f);
    out.Scale    = packed_float3(outScale.x, outScale.y, outScale.z);
    out.Rotation = qSlerp(A.Rotation, B.Rotation, f);
    out.FX1      = mix(A.FX1, B.FX1, f);
    out.FX2      = mix(A.FX2, B.FX2, f);
    out.Color    = mix(A.Color, B.Color, f);
    float3 aPos  = float3(A.Position.x, A.Position.y, A.Position.z);
    float3 bPos  = float3(B.Position.x, B.Position.y, B.Position.z);
    float3 outPos = mix(aPos, bPos, f);
    out.Position = packed_float3(outPos.x, outPos.y, outPos.z);
    out.FX1      = f;  // TiXL final overwrite: ResultPoints[i].FX1 = f

    ResultPoints[i] = out;
}
