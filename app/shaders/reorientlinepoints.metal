// reorientlinepoints.metal — faithful Metal port of TiXL's ReorientLinePoints.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/modify/ReorientLinePoints.hlsl
// A count-preserving MODIFIER: re-orients each point's Rotation so its +Z forward follows the
// local LINE TANGENT (the direction from the previous live neighbour to the next live neighbour),
// blended by Amount via qSlerp.  Dead (NAN-Scale) points and isolated/degenerate points pass through.
//
// TiXL main() (ReorientLinePoints.hlsl:105-152, verbatim logic):
//   if (isnan(SourcePoints[index].Scale.x)) return;          // dead point — leave ResultPoints untouched
//   uint prevIndex = index, nextIndex = index;
//   if (index > 0 && !isnan(SourcePoints[index-1].Scale.x))  prevIndex--;
//   if (index <= numStructs-1 && !isnan(SourcePoints[index+1].Scale.x)) nextIndex++;
//   if (prevIndex == nextIndex) return;                       // isolated — leave untouched
//   float3 v = SourcePoints[nextIndex].Position - SourcePoints[prevIndex].Position;
//   float  l = length(v);
//   if (l < 0.0001) return;                                   // coincident neighbours — leave untouched
//   float3 dir = v / l;
//   Point p = SourcePoints[index];
//   p.Rotation = qSlerp(p.Rotation, qAlignForward2(p.Rotation, dir), Amount);
//   ResultPoints[i.x] = p;
//
// NAMED FORK — early-return passthrough:
//   TiXL's `return` on dead/isolated/degenerate points leaves ResultPoints[index] UNWRITTEN.
//   In TiXL the RWStructuredBuffer is the SAME live buffer family; here SourcePoints and
//   ResultPoints are separate bags (cook writes a fresh output buffer), so a bare `return`
//   would leave a garbage/zero point.  We FORK to a faithful copy-through:
//   on every early-return path we write `ResultPoints[index] = SourcePoints[index]` so the
//   point survives unchanged (== TiXL's observable "rotation not modified" result).
//
// NAMED FORK — dropped dead ports (Center/UpVector/WIsWeight/Flip):
//   ReorientLinePoints.cs declares Center/UpVector/WIsWeight/Flip as [Input]s and the .hlsl cbuffer
//   carries them, but main() READS NONE of them (only Amount).  Porting them as operable knobs
//   would invent controls TiXL itself ignores; we keep only the live Amount param.
//
// qAlignForward2 is ported VERBATIM from the .hlsl (the variant the kernel actually calls);
// the unused qAlignForward / qAlignForward3 helpers are not ported.
#include <metal_stdlib>
#include "tixl_point.h"                  // SwPoint (64B)
#include "reorientlinepoints_params.h"   // ReorientLineParams, ReorientLineBinding
#include "shared/quat.metal.h"           // qRotateVec3, qLookAt, qSlerp
using namespace metal;

// Aligns orientation quaternion q so that its +Z forward points towards newForward.
// VERBATIM port of ReorientLinePoints.hlsl qAlignForward2 (lines 52-75).
inline float4 qAlignForward2(float4 q, float3 newForward) {
    newForward = normalize(newForward);

    // old up from current orientation (+Y rotated by q)
    float3 oldUp = qRotateVec3(float3(0, 1, 0), q);

    // project old up onto plane perpendicular to newForward
    float3 projUp = oldUp - newForward * dot(oldUp, newForward);

    // handle degenerate case: oldUp nearly parallel to newForward
    if (length(projUp) < 1e-5) {
        projUp = normalize(abs(newForward.x) < 0.9 ? float3(1, 0, 0) : float3(0, 1, 0));
        projUp = normalize(projUp - newForward * dot(projUp, newForward));
    } else {
        projUp = normalize(projUp);
    }

    // rebuild quaternion with forward = newForward, up ~= projected up
    return qLookAt(newForward, -projUp);
}

kernel void reorientlinepoints(
    device const SwPoint*         SourcePoints [[buffer(REORIENTLINE_SourcePoints)]],
    device       SwPoint*         ResultPoints [[buffer(REORIENTLINE_ResultPoints)]],
    constant ReorientLineParams&  P            [[buffer(REORIENTLINE_Params)]],
    uint3 i [[thread_position_in_grid]])
{
    uint index = i.x;
    uint numStructs = P.Count;
    if (index >= numStructs) return;

    // Dead point: leave rotation unchanged (FORK: copy-through, see header note).
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
    // TiXL guards index <= numStructs-1; index+1 may equal numStructs (OOB read in HLSL is
    // clamped/zeroed). We additionally guard index+1 < numStructs to avoid a real OOB read.
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
    p.Rotation = qSlerp(p.Rotation, qAlignForward2(r, dir), P.Amount);
    ResultPoints[index] = p;
}
