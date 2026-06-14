// snaptogrid.metal — faithful Metal port of TiXL SnapPointsToGrid.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/_internal/SnapPointsToGrid.hlsl
//
// TiXL parity (SnapPointsToGrid.hlsl lines 28-70):
//   float3 gridSize = GridScale * GridStretch;
//   float3 orgPosition = p.Position;
//   float3 normalizedPosition = pos / gridSize;
//   float3 normlizedOffsetPosition = normalizedPosition + 0.5 - GridOffset;
//   float3 signedFraction = (mod(normlizedOffsetPosition, 1) - 0.5) * 2;
//   float3 centerPoint = pos - signedFraction * gridSize / 2;
//   [scatter=0 baked]
//   Mode 0 (CenterDistance):
//     snapAmount = saturate(length(signedFraction*gridSize)/length(gridSize) + scatter.x)
//   Mode 1 (CornersDistance):
//     snapAmount = 1 - saturate(length(signedFraction*gridSize)/length(gridSize) + scatter.x)
//   Mode 2 (AxisCenterDistance):
//     snapAmount = abs(signedFraction + scatter)
//   Mode 3 (AxisEdgeDistance):
//     snapAmount = 1 - abs(signedFraction + scatter)
//   biasedSnap = ApplyGainAndBias(snapAmount.xyzz, GainAndBias).xyz
//     ApplyGainAndBias (bias-functions.hlsl, lines 26-49): gain=GainAndBias.x, bias=GainAndBias.y.
//     g<0.5 -> bias THEN schlick ; g>=0.5 -> schlick THEN bias (see applyGainAndBias below).
//     NOTE: TiXL's float4 ApplyGainAndBias matches the scalar form; we port the scalar verbatim.
//   strength = Amount * 1.0  (StrengthFactor=None baked)
//   ff = (1 - saturate(biasedSnap - Amount*2 + 1)) * strength
//   p.Position = lerp(orgPosition, centerPoint, ff)
//
// NAMED FORKS:
//   Scatter baked to 0 (hash-jitter deferred).
//   StrengthFactor=None baked (strength=1).
//   UseWAsWeight / UseSelection baked to 0.
//
// NOTE on mod: MSL fmod() is truncated (C semantics). TiXL uses GLSL floored mod.
// We define floorMod() for correctness on negative inputs (same formula as wrappoints.metal).
#include <metal_stdlib>
#include "tixl_point.h"          // SwPoint (64B)
#include "snaptogrid_params.h"   // SnapToGridParams, SnapToGridBinding
using namespace metal;

// GLSL floored modulo (== TiXL `mod` macro). Guards y==0 -> return 0.
inline float3 floorMod3(float3 x, float3 y) {
    float3 q = select(float3(0.0f), floor(x / y), y != float3(0.0f));
    return x - y * q;
}

// TiXL ApplyGainAndBias — VERBATIM port of bias-functions.hlsl (scalar form, lines 6-49).
//   GetBias(bias,x)      = x / ((1/bias - 2)*(1-x) + 1)
//   GetSchlickBias(g,x)  = x<0.5 ? 0.5*GetBias(g,2x)
//                                : 0.5*GetBias(1-g,2x-1) + 0.5
//   ApplyGainAndBias(value, gainBias):  g=saturate(gainBias.x), b=saturate(gainBias.y)
//     value>0.9999  -> 1            (hard early-out)
//     value<0.00001 -> 0            (hard early-out)
//     g<0.5  -> bias THEN schlick   ; g>=0.5 -> schlick THEN bias   (ORDER DEPENDS ON g)
// TiXL feeds float4 .xyzz to ApplyGainAndBias; we operate on float3 directly.
inline float getBias(float bias, float x) {
    return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}
inline float getSchlickBias(float g, float x) {
    if (x < 0.5f) {
        x *= 2.0f;
        x = 0.5f * getBias(g, x);
    } else {
        x = 2.0f * x - 1.0f;
        x = 0.5f * getBias(1.0f - g, x) + 0.5f;
    }
    return x;
}
inline float applyGainAndBias(float value, float gain, float bias) {
    float g = saturate(gain);
    float b = saturate(bias);
    if (value > 0.9999f) return 1.0f;
    if (value < 0.00001f) return 0.0f;
    if (g < 0.5f) {
        value = getBias(b, value);
        value = getSchlickBias(g, value);
    } else {
        value = getSchlickBias(g, value);
        value = getBias(b, value);
    }
    return value;
}
inline float3 applyGainAndBias3(float3 x, float gain, float bias) {
    return float3(applyGainAndBias(x.x, gain, bias),
                  applyGainAndBias(x.y, gain, bias),
                  applyGainAndBias(x.z, gain, bias));
}

kernel void snaptogrid(
    device const SwPoint*            SourcePoints [[buffer(SNAPTOGRID_SourcePoints)]],
    device       SwPoint*            ResultPoints [[buffer(SNAPTOGRID_ResultPoints)]],
    constant SnapToGridParams&       P            [[buffer(SNAPTOGRID_Params)]],
    uint3 tid [[thread_position_in_grid]])
{
    uint idx = tid.x;
    if (idx >= P.Count) return;

    SwPoint p = SourcePoints[idx];

    float3 gridStretch = float3(P.GridStretchX, P.GridStretchY, P.GridStretchZ);
    float3 gridOffset  = float3(P.GridOffsetX,  P.GridOffsetY,  P.GridOffsetZ);
    float3 gridSize    = P.GridScale * gridStretch;

    float3 orgPosition = p.Position;
    float3 pos = orgPosition;

    // Guard zero gridSize to avoid NaN (TiXL doesn't explicitly guard but scatter=0 & non-zero defaults)
    float3 safeGridSize = select(float3(1.0f), gridSize, gridSize != float3(0.0f));

    float3 normalizedPosition     = pos / safeGridSize;
    float3 normlizedOffsetPosition = normalizedPosition + 0.5f - gridOffset;
    // TiXL `mod` = floored mod
    float3 signedFraction = (floorMod3(normlizedOffsetPosition, float3(1.0f)) - 0.5f) * 2.0f;
    float3 centerPoint    = pos - signedFraction * safeGridSize / 2.0f;

    // scatter baked to 0
    float3 snapAmount = float3(0.0f);
    int mode = (int)(P.Mode + 0.5f);
    if (mode == 0) {
        // CenterDistance: distance from grid center
        float len = length(signedFraction * safeGridSize);
        float lenG = length(safeGridSize);
        snapAmount = float3(saturate(len / max(lenG, 1e-6f)));
    } else if (mode == 1) {
        // CornersDistance: inverted distance from corners
        float len = length(signedFraction * safeGridSize);
        float lenG = length(safeGridSize);
        snapAmount = float3(1.0f - saturate(len / max(lenG, 1e-6f)));
    } else if (mode == 2) {
        // AxisCenterDistance: per-axis
        snapAmount = abs(signedFraction);
    } else {
        // AxisEdgeDistance (mode 3): per-axis complement
        snapAmount = 1.0f - abs(signedFraction);
    }

    float3 biasedSnap = applyGainAndBias3(snapAmount,
                                           P.GainAndBiasX,   // gain
                                           P.GainAndBiasY);  // bias
    // TiXL: strength = Amount * 1.0 (StrengthFactor=None)
    float strength = P.Amount;
    float3 ff = (1.0f - saturate(biasedSnap - P.Amount * 2.0f + 1.0f)) * strength;
    p.Position = mix(orgPosition, centerPoint, ff);

    ResultPoints[idx] = p;
}
