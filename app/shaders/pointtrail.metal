// pointtrail.metal — Metal port of TiXL PointTrail-{Clear,Collect,Copy}.hlsl (3-pass trail).
// Reference: external/tixl/Operators/Lib/Assets/shaders/points/sim/PointTrail-{Clear,Collect,Copy}.hlsl
//
// PASS 1 Clear  (pointtrail_clear)  : NaN-Scale the whole per-frame output ring (all break).
// PASS 2 Collect(pointtrail_collect): write each source point into the PERSISTENT CyclePoints ring at
//                                     targetIndex = (CycleIndex + sourceIndex*TrailLength) % BufferLength.
//                                     The ring persists across frames (cross-frame state) — this is the
//                                     accumulation. dispatch = PointCount.
// PASS 3 Copy   (pointtrail_copy)   : read the ring NEWEST-FIRST into the output:
//                                     sourceIndex = (i + CycleIndex) % BufferLength,
//                                     targetIndex = BufferLength - i - 1; write the fade f into the
//                                     WriteOrderTo channel; stamp NaN separators at trail ends. dispatch
//                                     = BufferLength.
//
// TrailLength here == ringPerPoint (userTrailLength + 1; the host bakes the +1, faithful to the .t3).
// SwPoint <-> TiXL Point: Position/Color/Scale 1:1; Scale/FX1/FX2 carry the fade + NaN separators.

#include <metal_stdlib>
using namespace metal;

#include "../src/runtime/tixl_point.h"
#include "../src/runtime/pointtrail_params.h"

[[kernel]]
void pointtrail_clear(
    device SwPoint*            TrailPoints [[buffer(POINTTRAIL_TrailPoints)]],
    constant PointTrailParams& P           [[buffer(POINTTRAIL_Params)]],
    uint3                      tid          [[thread_position_in_grid]])
{
    int i = (int)tid.x;
    if (i >= P.BufferLength) return;
    TrailPoints[i].Scale = packed_float3(NAN, NAN, NAN);
}

[[kernel]]
void pointtrail_collect(
    const device SwPoint*      SourcePoints [[buffer(POINTTRAIL_SourcePoints)]],
    device SwPoint*            CyclePoints  [[buffer(POINTTRAIL_CyclePoints)]],
    constant PointTrailParams& P            [[buffer(POINTTRAIL_Params)]],
    uint3                      tid           [[thread_position_in_grid]])
{
    int sourceIndex = (int)tid.x;
    if (sourceIndex >= P.PointCount) return;
    if (P.BufferLength <= 0) return;
    int targetIndex = (P.CycleIndex + sourceIndex * P.TrailLength) % P.BufferLength;
    CyclePoints[targetIndex] = SourcePoints[sourceIndex];
}

[[kernel]]
void pointtrail_copy(
    const device SwPoint*      CyclePoints [[buffer(POINTTRAIL_CyclePoints)]],
    device SwPoint*            TrailPoints  [[buffer(POINTTRAIL_TrailPoints)]],
    constant PointTrailParams& P            [[buffer(POINTTRAIL_Params)]],
    uint3                      tid           [[thread_position_in_grid]])
{
    int i = (int)tid.x;
    if (i >= P.BufferLength) return;

    int sourceIndex = (i + P.CycleIndex) % P.BufferLength;
    int targetIndex = P.BufferLength - i - 1;

    TrailPoints[targetIndex] = CyclePoints[sourceIndex];

    float fInBuffer = 1.0f;
    int indexInTrail = targetIndex % P.TrailLength;
    if (P.WriteLineSeperators != 0) {
        fInBuffer = P.TrailLength > 1
                        ? (1.0f - indexInTrail / (float)(P.TrailLength - 1))
                        : 0.5f;
        if (indexInTrail == P.TrailLength - 1)
            TrailPoints[targetIndex].Scale = packed_float3(NAN, NAN, NAN);
    } else {
        fInBuffer = P.TrailLength > 2
                        ? (1.0f - indexInTrail / (float)(P.TrailLength - 1))
                        : 0.5f;
    }

    if (P.WriteOrderTo == 1) {
        TrailPoints[targetIndex].FX1 = fInBuffer;
    } else if (P.WriteOrderTo == 2) {
        TrailPoints[targetIndex].FX2 = fInBuffer;
    } else if (P.WriteOrderTo == 3) {
        TrailPoints[targetIndex].Scale = packed_float3(fInBuffer, fInBuffer, fInBuffer);
    }
}
