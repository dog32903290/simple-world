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
// NAMED FORK — next-neighbour OOB-read guard (genuine TiXL kernel bug, not a translation slip):
//   TiXL's guard is `if (index <= numStructs - 1 && !isnan(SourcePoints[index + 1].Scale.x))`
//   (ReorientLinePoints.hlsl:126). Given main()'s earlier `index >= numStructs` early-return already
//   established `index < numStructs`, the condition `index <= numStructs - 1` is ALGEBRAICALLY
//   ALWAYS TRUE — the evident intent was `index < numStructs - 1` ("not the last element"), guarding
//   the `SourcePoints[index + 1]` read just below it. As written, when `index == numStructs - 1` (the
//   LAST point), the guard never blocks the read: `SourcePoints[numStructs]`, one past the declared
//   buffer, is read out-of-bounds — undefined per the D3D/Metal spec (real GPUs typically zero-fill
//   slack capacity past `Count`, presumably why this never visibly misbehaved). sw's GPU buffer is
//   allocated to EXACTLY `numStructs` slots (no slack), so a literal port would read uninitialized/
//   OOB device memory. We FORK to a real guard, `index + 1 < numStructs`, so the last point never
//   triggers a next-neighbour bump (falls back to the previous neighbour instead — the same
//   "backward difference" outcome an in-bounds TiXL slack-zero-read would produce for a dead
//   sentinel). mathv_ref_reorientlinepoints.h keeps TiXL's literal always-true condition and
//   requires callers to supply a `count+1`-element buffer with a caller-chosen padding value at
//   `in[count]`, so the CPU oracle can reproduce the OOB read's observable divergence on demand;
//   --selftest-mathv-reorientlinepoints's OOB-last-point quirk probe asserts GPU==ref(padding=NaN)
//   (this fork's own invariant) while ref(padding=a real point) diverges from both (proving TiXL's
//   literal bug is real and distinct from this fork, not just an unexercised corner).
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
    // NAMED FORK (next-neighbour OOB-read guard) -- see file header NAMED FORKS list. TiXL's guard
    // `index <= numStructs-1` is always-true (algebraic slip); we guard index+1 < numStructs for real.
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
