// computeshaderstage_wrappointposition — the ABI-repacked kernel that lets the GENERIC
// ComputeShaderStage atom drive WrapPointPosition after the flat atom retires.
//
// This is NOT the fused scalar wrappointposition.metal (which takes a hand-assembled
// WrapPointPositionParams struct). It is a faithful MSL port of
// external/tixl/Operators/Lib/Assets/shaders/points/modify/WrapPointPosition.hlsl with the SAME
// constant-buffer / UAV binding contract the .hlsl declares, so it is driven by the generic
// ComputeShaderStage exactly as TiXL drives it: the const buffer is the raw bytes FloatsToBuffer
// assembles, the write target is the in-place Point UAV.
//
// ── IN-PLACE (no SRV) ──────────────────────────────────────────────────────────────────────────
// WrapPointPosition.hlsl declares `RWStructuredBuffer<LegacyPoint> ResultPoints : u0;` and reads AND
// writes ResultPoints[i.x] — there is NO SourcePoints SRV. The .t3 wires the input GPoints buffer
// straight onto ComputeShaderStage.Uavs (u0) via GetBufferComponents; no ShaderResources are wired.
// The generic cook (buffer_ops_computeshaderstage.cpp) sizes the dispatch from the UAV when no SRV is
// present. This kernel therefore binds only u0 (in-place) + the const buffers.
//
// ── HLSL cbuffer byte layout (what FloatsToBuffer writes, tightly packed float[]) ────────────────
//   b0 (FloatsToBuffer, WrapPointPosition.hlsl:6-12, wire order == .t3 FloatsToBuffer.Params):
//        [0]=Center.x [1]=Center.y [2]=Center.z [3]=UseCamera
//        [4]=Size.x   [5]=Size.y   [6]=Size.z   [7]=WriteLineBreaks
//   b1 (TransformsConstBuffer, WrapPointPosition.hlsl:14-26) — only CameraToWorld's translation row is
//        read, and only on the UseCamera>0.5 branch (:44). We DO NOT bind/read b1: UseCamera is baked
//        to 0 (NAMED FORK, same as the flat kernel — no camera matrix in cook ctx), so center=Center.
// We read cb0 as a raw float pointer so the byte layout is unambiguous (no MSL struct padding).
//
// ── NAMED FORKS (preserved from wrappointposition.metal — S-audit verdict (a) 2026-07-09) ────────
//   • UseCamera baked to 0 → center = Center (cb0[0..2]); b1 Transforms unused.
//   • z-NaN guard: TiXL HLSL :56 reads `isnan(p.x + p.y + p.x)` (p.x twice, p.z never) — an unambiguous
//     hand-slip. sw checks all three axes: isnan(p.x + p.y + p.z). Divergence reachable only on a
//     z-alone-NaN / opposing-Inf-in-z input; sw recovers it to the visible sentinel exactly as TiXL
//     does for x/y-NaN.
//   • WriteLineBreaks is NOT baked here (unlike the flat kernel): the generic ABI reads the REAL wired
//     value (cb0[7], .t3 default AddLineBreaks=true → 1). The W line-break path (:77-78, W=sqrt(-1) on
//     any wrap) is thus live and faithful to the compound's default.
#include <metal_stdlib>
#include "tixl_point.h"                     // SwPoint (64B)
#include "computeshaderstage_params.h"      // CS_CB_BASE / CS_UAV_BASE
using namespace metal;

kernel void computeshaderstage_wrappointposition(
    device SwPoint*   ResultPoints [[buffer(CS_UAV_BASE + 0)]],   // u0 (in-place: read+write)
    constant float*   cb0          [[buffer(CS_CB_BASE + 0)]],    // b0: Center+UseCamera+Size+WriteLineBreaks
    constant uint&    numStructs   [[buffer(CS_CB_BASE + 3)]],    // dispatch bound (UAV element count)
    uint3 i [[thread_position_in_grid]])
{
    if (i.x >= numStructs) return;
    uint idx = i.x;

    float3 center = float3(cb0[0], cb0[1], cb0[2]);   // UseCamera (cb0[3]) baked 0 — NAMED FORK
    float3 size   = float3(cb0[4], cb0[5], cb0[6]);
    float writeLineBreaks = cb0[7];

    SwPoint pt = ResultPoints[idx];
    float3 p = pt.Position - center;

    // NaN recovery (TiXL :56-59, sw 3-axis guard): place at center - Size*0.2 with W=0.01
    if (isnan(p.x + p.y + p.z)) {
        pt.Position = center - size * 0.2f;
        pt.FX1 = 0.010f;
        ResultPoints[idx] = pt;
        return;
    }

    float3 padding  = float3(size.x * 0.1f);   // TiXL :62 Padding = Size.x*0.1 (scalar broadcast)
    float3 halfSize = size * 0.5f;
    float3 padded   = halfSize + padding;

    float3 offsetFactor = float3(0.0f);
    if (fabs(p.x) > padded.x) { offsetFactor.x = (p.x < 0.0f) ?  1.0f : -1.0f; }
    if (fabs(p.y) > padded.y) { offsetFactor.y = (p.y < 0.0f) ?  1.0f : -1.0f; }
    if (fabs(p.z) > padded.z) { offsetFactor.z = (p.z < 0.0f) ?  1.0f : -1.0f; }

    float3 wrappedP = p + size * offsetFactor;
    pt.Position = wrappedP + center;

    // W: line-break (WriteLineBreaks>0.5 && wrapped) → NaN, else edge-fade product (TiXL :77-86).
    float wrapSum = fabs(offsetFactor.x) + fabs(offsetFactor.y) + fabs(offsetFactor.z);
    if (writeLineBreaks > 0.5f && wrapSum != 0.0f) {
        pt.FX1 = sqrt(-1.0f);
    } else {
        float3 distToEdge = halfSize - fabs(wrappedP);
        float3 minDist = saturate(distToEdge * 10.0f);
        pt.FX1 = minDist.x * minDist.y * minDist.z;
    }

    ResultPoints[idx] = pt;
}
