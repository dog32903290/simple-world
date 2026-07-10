// computeshaderstage_reorientlinepoints — ABI-repacked kernel driving ReorientLinePoints through
// the GENERIC ComputeShaderStage atom after the flat atom retires.
//
// Faithful MSL port of external/tixl/Operators/Lib/Assets/shaders/points/modify/ReorientLinePoints.hlsl
// with the generic const-buffer / SRV / UAV binding contract (NOT the fused scalar reorientlinepoints.metal's
// ReorientLineParams struct). Driven by the raw bytes FloatsToBuffer assembles. Math is line-for-line the
// fused reorientlinepoints.metal (which is itself the faithful ReorientLinePoints.hlsl port); only the
// parameter SOURCE changes (raw cbuffer bytes vs a marshalled struct) — same two NAMED FORKS carry over
// verbatim (see below).
//
// ── HLSL cbuffer byte layout (FloatsToBuffer, wire order == .t3 ReorientLinePoints Connections) ────────
//   b0 (ReorientLinePoints.hlsl Params register b0), 9 floats tightly packed, wire order verbatim from
//   ReorientLinePoints.t3 Connections (Vector3Components(Center) x/y/z, root.Amount, Vector3Components
//   (UpVector) x/y/z, BoolToFloat(WIsWeight), BoolToFloat(Flip)):
//        cb0[0..2]=Center.xyz(DEAD)  cb0[3]=Amount(LIVE)  cb0[4..6]=UpVector.xyz(DEAD)
//        cb0[7]=WIsWeight(DEAD)  cb0[8]=Flip(DEAD)
//   Only cb0[3] is read — Center/UpVector/WIsWeight/Flip are declared in the cbuffer and wired from the
//   compound's own boundary inputs but never touched by main() (same DROPPED-DEAD-PORTS fork as the fused
//   reorientlinepoints.metal / reorientlinepoints_params.h — see those files' header notes).
//   t0 (SRV) = SourcePoints (the wired Points bag, via GetBufferComponents) ; u0 (UAV) = ResultPoints
//        (a FRESH StructuredBufferWithViews, Stride=64 — ReorientLinePoints.t3 bakes Stride=64 natively,
//         no 32→64 legacy-point fork needed here, unlike SnapToPoints/SnapPointsToGrid).
//
// ── NAMED FORKS (carried over verbatim from reorientlinepoints.metal) ───────────────────────────────────
//   • copy-through on early-return: TiXL's `return;` (dead/isolated/degenerate-neighbour points) leaves
//     ResultPoints[index] UNTOUCHED. sw's SourcePoints/ResultPoints are separate MTL buffers (the .t3 itself
//     routes ResultPoints through a FRESH StructuredBufferWithViews, not literally the same buffer object),
//     so a bare early return would read back whatever the fresh UAV's un-seeded contents happen to be. We
//     FORK to an explicit `ResultPoints[index] = SourcePoints[index]` copy-through on every early-return
//     path — the same faithful "unchanged" observable result, independent of the UAV's initial contents.
//   • next-neighbour OOB-read guard (genuine TiXL kernel bug, not a translation slip — see
//     reorientlinepoints.metal's header for the full derivation): TiXL's literal guard
//     `index <= numStructs - 1` is algebraically always-true given the earlier `index >= numStructs`
//     early-return, so TiXL reads `SourcePoints[numStructs]` (one past the end) for the LAST point. sw's
//     SRV buffer has no slack past `numStructs` elements, so we FORK to a real guard
//     `index + 1 < numStructs` — the last point falls back to a backward difference instead of an
//     out-of-bounds read (mathv_ref_reorientlinepoints.h's own contract: GPU behaves as if the OOB read's
//     padding slot were NaN — pinned by --selftest-mathv-reorientlinepoints's OOB-last-point tooth).
#include <metal_stdlib>
#include "tixl_point.h"                     // SwPoint (64B), packed_float3
#include "computeshaderstage_params.h"      // CS_CB_BASE / CS_SRV_BASE / CS_UAV_BASE
#include "shared/quat.metal.h"              // qRotateVec3, qLookAt, qSlerp
using namespace metal;

// Aligns orientation quaternion q so that its +Z forward points towards newForward.
// VERBATIM port of ReorientLinePoints.hlsl qAlignForward2 (lines 52-75) — same as reorientlinepoints.metal.
inline float4 csReorientQAlignForward2(float4 q, float3 newForward) {
    newForward = normalize(newForward);

    float3 oldUp = qRotateVec3(float3(0, 1, 0), q);

    float3 projUp = oldUp - newForward * dot(oldUp, newForward);

    if (length(projUp) < 1e-5) {
        projUp = normalize(abs(newForward.x) < 0.9 ? float3(1, 0, 0) : float3(0, 1, 0));
        projUp = normalize(projUp - newForward * dot(projUp, newForward));
    } else {
        projUp = normalize(projUp);
    }

    return qLookAt(newForward, -projUp);
}

kernel void computeshaderstage_reorientlinepoints(
    const device SwPoint* SourcePoints [[buffer(CS_SRV_BASE + 0)]],   // t0
    device SwPoint*       ResultPoints [[buffer(CS_UAV_BASE + 0)]],   // u0
    constant float*       cb0          [[buffer(CS_CB_BASE + 0)]],    // b0: 9 floats, only cb0[3]=Amount live
    constant uint&        numStructs   [[buffer(CS_CB_BASE + 3)]],    // dispatch bound (SRV element count)
    uint3 i [[thread_position_in_grid]])
{
    uint index = i.x;
    if (index >= numStructs) return;

    float Amount = cb0[3];

    // Dead point: leave rotation unchanged (FORK: copy-through, see file header NAMED FORKS).
    if (isnan(SourcePoints[index].Scale.x)) {
        ResultPoints[index] = SourcePoints[index];
        return;
    }

    // Find live neighbours (TiXL ReorientLinePoints.hlsl:117-129).
    uint prevIndex = index;
    uint nextIndex = index;

    if (index > 0 && !isnan(SourcePoints[index - 1].Scale.x)) {
        prevIndex--;
    }
    // NAMED FORK (next-neighbour OOB-read guard) — see file header. TiXL's guard `index <= numStructs-1`
    // is always-true (algebraic slip); we guard index+1 < numStructs for real.
    if (index + 1 < numStructs && !isnan(SourcePoints[index + 1].Scale.x)) {
        nextIndex++;
    }

    // Isolated point: nothing to align (FORK: copy-through).
    if (prevIndex == nextIndex) {
        ResultPoints[index] = SourcePoints[index];
        return;
    }

    float3 v = SourcePoints[nextIndex].Position - SourcePoints[prevIndex].Position;
    float l = length(v);

    // Coincident neighbours: nothing to align (FORK: copy-through).
    if (l < 0.0001) {
        ResultPoints[index] = SourcePoints[index];
        return;
    }

    float3 dir = v / l;
    SwPoint p = SourcePoints[index];

    float4 r = p.Rotation;
    p.Rotation = qSlerp(p.Rotation, csReorientQAlignForward2(r, dir), Amount);
    ResultPoints[index] = p;
}
