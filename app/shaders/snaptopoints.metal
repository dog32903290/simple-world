// snaptopoints.metal — faithful Metal port of TiXL SnapToPoints.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/modify/SnapToPoints.hlsl
//
// TiXL kernel (SnapToPoints.hlsl lines 20-27, verbatim logic):
//   LegacyPoint A          = Points1[i.x];
//   LegacyPoint SnapPoint  = Points2[i.x];   // index-paired, NOT nearest-point
//   float distance         = length(A.Position - SnapPoint.Position);
//   float blendFactor      = smoothstep(BlendFactor + Distance, Distance, distance) * MaxAmount;
//   ResultPoints[i.x].Position = lerp(A.Position, SnapPoint.Position, blendFactor);
//   ResultPoints[i.x].W        = lerp(A.W, SnapPoint.W, BlendFactor);
//
// NAMED FORKS:
//   count-guard: TiXL .hlsl assumes Points1 and Points2 are equal length and performs
//     Points2[i.x] without any bounds check.  We clamp the Points2 index to
//     (Points2Count-1) when i >= Points2Count, and short-circuit to pass-through when
//     Points2Count == 0.  This prevents GPU OOB reads when the two inputs differ in size.
//     // fork[count-guard]: TiXL .hlsl assumes equal length, no OOB guard;
//     //   we clamp Points2 index to (Points2Count-1) to prevent OOB reads.
//
//   Attribute carry-over: TiXL .hlsl only writes Position and W (FX1 in SwPoint layout);
//     all other SwPoint fields (Rotation, Color, Scale, FX2) are taken from Points1[i]
//     unchanged (pass-through from A).
//
// SwPoint layout (tixl_point.h, 64 bytes):
//   packed_float3 Position @0  |  float FX1 @12  |  float4 Rotation @16
//   float4 Color @32           |  packed_float3 Scale @48  |  float FX2 @60
// TiXL LegacyPoint.W maps to SwPoint.FX1.
#include <metal_stdlib>
#include "tixl_point.h"           // SwPoint (64B), packed_float3
#include "snaptopoints_params.h"  // SnapToPointsParams, SnapToPointsBinding
using namespace metal;

kernel void snaptopoints(
    device const SwPoint*               Points1 [[buffer(SNAPTOPOINTS_Points1)]],
    device const SwPoint*               Points2 [[buffer(SNAPTOPOINTS_Points2)]],
    device       SwPoint*               Result  [[buffer(SNAPTOPOINTS_Result)]],
    constant SnapToPointsParams&        P       [[buffer(SNAPTOPOINTS_Params)]],
    uint3 tid [[thread_position_in_grid]])
{
    uint i = tid.x;
    if (i >= P.Count) return;

    SwPoint A = Points1[i];

    // fork[count-guard]: TiXL .hlsl assumes equal length, no OOB guard;
    // we clamp Points2 index to (Points2Count-1) to prevent OOB reads.
    if (P.Points2Count == 0u) {
        // No snap target: pass A through unchanged.
        Result[i] = A;
        return;
    }
    uint snapIdx = (i < P.Points2Count) ? i : (P.Points2Count - 1u);
    SwPoint SnapPoint = Points2[snapIdx];

    float3 posA    = float3(A.Position.x, A.Position.y, A.Position.z);
    float3 posSnap = float3(SnapPoint.Position.x, SnapPoint.Position.y, SnapPoint.Position.z);

    float dist        = length(posA - posSnap);
    // TiXL: smoothstep(BlendFactor + Distance, Distance, distance) — note arg order:
    // smoothstep(edge0, edge1, x): edge0 = upper (far), edge1 = lower (near), so at
    // dist < Distance the factor -> 1 (snap), at dist > BlendFactor+Distance -> 0 (no snap).
    float blendFactor = smoothstep(P.BlendFactor + P.Distance, P.Distance, dist) * P.MaxAmount;

    // Position and W (FX1) from TiXL formula; all other fields carry from A.
    SwPoint out   = A;  // carry-over: Rotation, Color, Scale, FX2 from Points1
    float3 newPos = mix(posA, posSnap, blendFactor);
    out.Position  = packed_float3(newPos.x, newPos.y, newPos.z);
    out.FX1       = mix(A.FX1, SnapPoint.FX1, P.BlendFactor);  // W channel (TiXL: lerp A.W, Snap.W, BlendFactor)

    Result[i] = out;
}
