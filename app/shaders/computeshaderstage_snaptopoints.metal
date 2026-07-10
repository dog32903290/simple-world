// computeshaderstage_snaptopoints — ABI-repacked kernel driving SnapToPoints through the GENERIC
// ComputeShaderStage atom after the flat atom retires.
//
// Faithful MSL port of external/tixl/Operators/Lib/Assets/shaders/points/modify/SnapToPoints.hlsl
// with the generic const-buffer / DUAL-SRV / UAV binding contract (NOT the fused scalar snaptopoints.metal's
// SnapToPointsParams struct). Driven by the raw bytes FloatsToBuffer assembles.
//
// SnapToPoints is an INDEX-PAIRED 2-input op: each point in Points1 is lerped toward the same-index point
// in Points2 by a distance-based smoothstep. Output count = Points1 count.
//
// ── HLSL cbuffer / SRV / UAV layout (wire order == .t3 FloatsToBuffer / ComputeShaderStage MultiInputs) ─
//   b0 (SnapToPoints.hlsl Params register b0, FloatsToBuffer — tight float[], no pad):
//        cb0[0]=BlendFactor  cb0[1]=Distance  cb0[2]=MaxAmount
//        (the .t3 wires BlendValue→Distance→MaxAmount into FloatsToBuffer.Params, SnapToPoints.t3
//         Connections:157/163/169 == SnapToPoints.hlsl :8-10. The BlendMode int rides IntToFloat and is
//         DISCARDED — no wire into FloatsToBuffer — so it never enters b0.)
//   t0 (SRV) = Points1 (PointsA_) ; t1 (SRV) = Points2 (PointsB_) ; u0 (UAV) = ResultPoints (fresh SBV)
//        (ShaderResources wire order Points1-before-Points2, SnapToPoints.t3 Connections:229/235.)
// Math is line-for-line the fused snaptopoints.metal (faithful SnapToPoints.hlsl port); only the parameter
// SOURCE changes (raw cbuffer bytes vs a marshalled struct) and the count-guard is dropped (see fork).
//
// ── NAMED FORKS ───────────────────────────────────────────────────────────────────────────────────────
//   • legacypoint-stride-32-to-64-swpoint: TiXL's SnapToPoints.t3 declares its ResultPoints
//     StructuredBufferWithViews Stride=32 (LegacyPoint), but sw stores every point as a 64B SwPoint. The
//     catalog asset assets/catalog_t3/SnapToPoints.t3 bakes Stride=64 so the fresh UAV is sized correctly
//     (production-correct, unlike the mesh test-only PbrVertex 64→80 override). W (LegacyPoint.W) maps to
//     SwPoint.FX1 @12.
//   • count-guard dropped (index-paired raw i.x into Points2): TiXL's HLSL reads Points2[i.x] with no OOB
//     guard (equal-length assumption). The generic seam sizes the dispatch from Points1 (t0) and passes no
//     Points2 count, so we follow TiXL exactly — the .t3 replay always pairs equal-length bags. (The flat
//     snaptopoints.metal added a sw-only Points2Count clamp; that safety fork has no ABI channel here and
//     is not part of TiXL parity.)
//   • NAMED-FORK-BY-SHADOWING (verbatim from the HLSL): the W channel lerps by the RAW cbuffer BlendFactor,
//     NOT the smoothstep-derived local blendFactor used for Position (SnapToPoints.hlsl :26 vs :27).
//   • attribute carry-over: only Position and FX1(W) are written; Rotation/Color/Scale/FX2 carry from A.
#include <metal_stdlib>
#include "tixl_point.h"                     // SwPoint (64B), packed_float3
#include "computeshaderstage_params.h"      // CS_CB_BASE / CS_SRV_BASE / CS_UAV_BASE
using namespace metal;

kernel void computeshaderstage_snaptopoints(
    const device SwPoint* Points1    [[buffer(CS_SRV_BASE + 0)]],   // t0 (PointsA_)
    const device SwPoint* Points2    [[buffer(CS_SRV_BASE + 1)]],   // t1 (PointsB_)
    device SwPoint*       ResultPoints [[buffer(CS_UAV_BASE + 0)]], // u0
    constant float*       cb0        [[buffer(CS_CB_BASE + 0)]],    // b0: [BlendFactor, Distance, MaxAmount]
    constant uint&        numStructs [[buffer(CS_CB_BASE + 3)]],    // dispatch bound (Points1 element count)
    uint3 tid [[thread_position_in_grid]])
{
    if (tid.x >= numStructs) return;
    uint i = tid.x;

    float BlendFactor = cb0[0];
    float Distance    = cb0[1];
    float MaxAmount   = cb0[2];

    SwPoint A         = Points1[i];
    SwPoint SnapPoint = Points2[i];   // index-paired (TiXL raw i.x, equal-length assumption)

    float3 posA    = float3(A.Position.x, A.Position.y, A.Position.z);
    float3 posSnap = float3(SnapPoint.Position.x, SnapPoint.Position.y, SnapPoint.Position.z);

    float dist = length(posA - posSnap);
    // SnapToPoints.hlsl :24 — smoothstep(BlendFactor+Distance, Distance, dist)*MaxAmount (edge0>edge1
    // is the intended monotonically-decreasing falloff — snap fades out beyond Distance+BlendFactor).
    float blendFactor = smoothstep(BlendFactor + Distance, Distance, dist) * MaxAmount;

    SwPoint out   = A;  // carry-over: Rotation, Color, Scale, FX2 from Points1
    float3 newPos = mix(posA, posSnap, blendFactor);
    out.Position  = packed_float3(newPos.x, newPos.y, newPos.z);
    out.FX1       = mix(A.FX1, SnapPoint.FX1, BlendFactor);  // W channel: raw BlendFactor (shadowing fork)

    ResultPoints[i] = out;
}
