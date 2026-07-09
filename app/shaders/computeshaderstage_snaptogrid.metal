// computeshaderstage_snaptogrid — ABI-repacked kernel driving SnapPointsToGrid through the GENERIC
// ComputeShaderStage atom after the flat atom retires.
//
// Faithful MSL port of external/tixl/Operators/Lib/Assets/shaders/points/_internal/SnapPointsToGrid.hlsl
// with the generic const-buffer / SRV / UAV binding contract (NOT the fused scalar snaptogrid.metal's
// SnapToGridParams struct). Driven by the raw bytes FloatsToBuffer assembles.
//
// ── HLSL cbuffer byte layout (FloatsToBuffer, wire order == .t3 FloatsToBuffer.Params) ───────────
//   b0 (SnapPointsToGrid.hlsl Params register b0), 12 floats tightly packed:
//        [0]=GridStretch.x [1]=.y [2]=.z  [3]=Amount
//        [4]=GridOffset.x  [5]=.y [6]=.z  [7]=GridScale
//        [8]=Scatter       [9]=Mode       [10]=GainAndBias.x [11]=GainAndBias.y
//   b1 (IntsToBuffer, register b1): [0]=StrengthFactor — NOT read here (baked None → strength=Amount,
//        same NAMED FORK as the flat kernel).
//   t0 (SRV) = SourcePoints ; u0 (UAV) = ResultPoints (fresh StructuredBufferWithViews)
// Math is line-for-line the fused snaptogrid.metal (faithful SnapPointsToGrid.hlsl port); only the
// parameter SOURCE changes (raw cbuffer bytes vs a marshalled struct).
//
// ── NAMED FORKS (preserved from snaptogrid.metal) ────────────────────────────────────────────────
//   • Scatter baked to 0 (hash-jitter deferred) — cb0[8] IS read but the .t3 default is 0.
//   • StrengthFactor=None baked (strength=Amount); b1 not read.
//   • zero-gridSize guard: substitute gridSize=1 where any axis is 0 (TiXL divides unguarded → Inf/NaN;
//     observable only at scale=0).
//   • ApplyGainAndBias: the SCALAR-overload clamped early-out (author's documented intent), not the
//     float4 overload's dropped-clamp hand-slip.
#include <metal_stdlib>
#include "tixl_point.h"                     // SwPoint (64B)
#include "computeshaderstage_params.h"      // CS_CB_BASE / CS_SRV_BASE / CS_UAV_BASE
using namespace metal;

// GLSL floored modulo (== TiXL `mod` macro). Guards y==0 → return 0. (same as snaptogrid.metal)
inline float3 csSnapFloorMod3(float3 x, float3 y) {
    float3 q = select(float3(0.0f), floor(x / y), y != float3(0.0f));
    return x - y * q;
}

// ApplyGainAndBias — VERBATIM port of bias-functions.hlsl scalar form (same as snaptogrid.metal).
inline float csSnapGetBias(float bias, float x) {
    return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}
inline float csSnapGetSchlickBias(float g, float x) {
    if (x < 0.5f) { x *= 2.0f; x = 0.5f * csSnapGetBias(g, x); }
    else          { x = 2.0f * x - 1.0f; x = 0.5f * csSnapGetBias(1.0f - g, x) + 0.5f; }
    return x;
}
inline float csSnapApplyGainAndBias(float value, float gain, float bias) {
    float g = saturate(gain);
    float b = saturate(bias);
    if (value > 0.9999f) return 1.0f;
    if (value < 0.00001f) return 0.0f;
    if (g < 0.5f) { value = csSnapGetBias(b, value); value = csSnapGetSchlickBias(g, value); }
    else          { value = csSnapGetSchlickBias(g, value); value = csSnapGetBias(b, value); }
    return value;
}
inline float3 csSnapApplyGainAndBias3(float3 x, float gain, float bias) {
    return float3(csSnapApplyGainAndBias(x.x, gain, bias),
                  csSnapApplyGainAndBias(x.y, gain, bias),
                  csSnapApplyGainAndBias(x.z, gain, bias));
}

kernel void computeshaderstage_snaptogrid(
    const device SwPoint* SourcePoints [[buffer(CS_SRV_BASE + 0)]],   // t0
    device SwPoint*       ResultPoints [[buffer(CS_UAV_BASE + 0)]],   // u0
    constant float*       cb0          [[buffer(CS_CB_BASE + 0)]],    // b0: 12 floats
    constant uint&        numStructs   [[buffer(CS_CB_BASE + 3)]],    // dispatch bound (SRV element count)
    uint3 tid [[thread_position_in_grid]])
{
    if (tid.x >= numStructs) return;
    uint idx = tid.x;

    float3 gridStretch = float3(cb0[0], cb0[1], cb0[2]);
    float  Amount      = cb0[3];
    float3 gridOffset  = float3(cb0[4], cb0[5], cb0[6]);
    float  GridScale   = cb0[7];
    // cb0[8]=Scatter (baked 0), cb0[9]=Mode
    float  Mode        = cb0[9];
    float  GainAndBiasX = cb0[10];
    float  GainAndBiasY = cb0[11];

    SwPoint p = SourcePoints[idx];
    float3 gridSize = GridScale * gridStretch;

    float3 orgPosition = p.Position;
    float3 pos = orgPosition;

    float3 safeGridSize = select(float3(1.0f), gridSize, gridSize != float3(0.0f));

    float3 normalizedPosition      = pos / safeGridSize;
    float3 normlizedOffsetPosition = normalizedPosition + 0.5f - gridOffset;
    float3 signedFraction = (csSnapFloorMod3(normlizedOffsetPosition, float3(1.0f)) - 0.5f) * 2.0f;
    float3 centerPoint    = pos - signedFraction * safeGridSize / 2.0f;

    float3 snapAmount = float3(0.0f);
    int mode = (int)(Mode + 0.5f);
    if (mode == 0) {
        float len = length(signedFraction * safeGridSize);
        float lenG = length(safeGridSize);
        snapAmount = float3(saturate(len / max(lenG, 1e-6f)));
    } else if (mode == 1) {
        float len = length(signedFraction * safeGridSize);
        float lenG = length(safeGridSize);
        snapAmount = float3(1.0f - saturate(len / max(lenG, 1e-6f)));
    } else if (mode == 2) {
        snapAmount = abs(signedFraction);
    } else {
        snapAmount = 1.0f - abs(signedFraction);
    }

    float3 biasedSnap = csSnapApplyGainAndBias3(snapAmount, GainAndBiasX, GainAndBiasY);
    float strength = Amount;   // StrengthFactor=None baked
    float3 ff = (1.0f - saturate(biasedSnap - Amount * 2.0f + 1.0f)) * strength;
    p.Position = mix(orgPosition, centerPoint, ff);

    ResultPoints[idx] = p;
}
