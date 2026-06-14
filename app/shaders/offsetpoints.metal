// offsetpoints.metal — faithful Metal port of TiXL's OffsetPoints.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/modify/OffsetPoints.hlsl (lines 30-45)
//   .cs: external/tixl/Operators/Lib/point/_internal/_OffsetPoints.cs
// A count-preserving MODIFIER: each SwPoint's Position is offset by Direction*Distance, rotated
// by the POINT'S OWN existing Rotation quaternion (qRotateVec3). Rotation/Color/Scale/FX/W are
// preserved verbatim. Writes ResultPoints (same count, same 64B stride).
//
// TiXL parity (.hlsl line 40):
//   ResultPoints[i].Position = Points1[i].Position + qRotateVec3(Direction*Distance, Points1[i].Rotation)
//   .hlsl lines 41-44: Rotation/Color/Selected/W copied verbatim.
//
// Rotation order note: NOT applicable — OffsetPoints constructs NO new rotation. It rotates the
// offset vector by the point's EXISTING Rotation (qRotateVec3, fast Rodrigues), exactly as the
// .hlsl does. No Euler decomposition, no yaw/pitch/roll order question.
//
// MSL port notes:
//   - LegacyPoint -> SwPoint (64B layout, tixl_point.h). .hlsl's "W" field = SwPoint.FX1 (offset 12).
//     SwPoint also carries Scale/FX2 which the .hlsl LegacyPoint lacks; we pass the whole SwPoint
//     through (copy then overwrite Position) so Scale/FX2 ride along untouched — strictly more
//     faithful than zeroing them.
//   - OOB handling: TiXL writes W=0 for out-of-range threads (numthreads(64) padding). simple_world
//     dispatches an exactly-sized bag and uses the project-standard early-return guard (same as
//     addnoise/wrappointposition), so no dead-point padding write is needed. NAMED FORK.
//   - qRotateVec3 is in shared/quat.metal.h (already present in project).
#include <metal_stdlib>
#include "tixl_point.h"              // SwPoint (64B layout)
#include "offsetpoints_params.h"     // OffsetPointsParams, OffsetPointsBinding
#include "shared/quat.metal.h"       // qRotateVec3
using namespace metal;

kernel void offsetpoints(
    device const SwPoint* SourcePoints [[buffer(OFFSETPOINTS_SourcePoints)]],
    device       SwPoint* ResultPoints [[buffer(OFFSETPOINTS_ResultPoints)]],
    constant OffsetPointsParams& P     [[buffer(OFFSETPOINTS_Params)]],
    uint3 i [[thread_position_in_grid]])
{
    uint idx = i.x;
    if (idx >= P.Count) return;

    SwPoint p = SourcePoints[idx];   // copy: Rotation/Color/Scale/FX1(W)/FX2 ride through untouched

    float3 direction = float3(P.DirectionX, P.DirectionY, P.DirectionZ);
    // .hlsl line 40: Position += qRotateVec3(Direction * Distance, Point.Rotation)
    float3 offset = qRotateVec3(direction * P.Distance, p.Rotation);
    p.Position = float3(p.Position) + offset;

    ResultPoints[idx] = p;
}
