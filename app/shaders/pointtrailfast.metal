// pointtrailfast.metal — Metal port of TiXL PointTrailFast.hlsl
// Reference: external/tixl/Operators/Lib/Assets/shaders/points/sim/PointTrailFast.hlsl
//
// A FIXED-size trail ring. Each frame, each source point i is written into a ring slot at
//   targetIndex = (CycleIndex + sourceIndex * TrailLength) % BufferLength
// where BufferLength = PointCount * TrailLength. The host advances CycleIndex by TrailLength
// (== the ring stride per point) every enabled frame, so the newest sample lands one slot
// forward each frame and old samples persist across frames (the cross-frame trail). The slot
// AHEAD of the write head ((targetIndex+1) % BufferLength) is stamped NaN as a line separator
// so the renderer breaks the polyline between a point's most-recent and oldest samples.
//
// SHADER NOTE (faithful to the .t3): the cook hands TrailLength == (userTrailLength + 1) — the .t3
// wires AddInts(TrailLength, +1) into BOTH the buffer allocation and Params2.TrailLength, so the +1
// separator slot is baked into the ring stride. The HLSL is reproduced 1:1; the +1 lives in the host.
//
// SwPoint <-> TiXL Point: Position/Color/Scale 1:1; Scale.x carries the NaN separator flag (HLSL sets
// Point.Scale = NAN; SwPoint.Scale @48 is the same packed_float3 slot).

#include <metal_stdlib>
using namespace metal;

#include "../src/runtime/tixl_point.h"
#include "../src/runtime/pointtrailfast_params.h"

[[kernel]]
void pointtrailfast(
    const device SwPoint*            SourcePoints [[buffer(POINTTRAILFAST_SourcePoints)]],
    device       SwPoint*            TrailPoints  [[buffer(POINTTRAILFAST_TrailPoints)]],
    constant     PointTrailFastParams& P          [[buffer(POINTTRAILFAST_Params)]],
    uint3                            tid           [[thread_position_in_grid]])
{
    int sourceIndex = (int)tid.x;
    if (sourceIndex >= P.PointCount)
        return;

    int bufferLength = P.PointCount * P.TrailLength;
    if (bufferLength <= 0) return;
    int targetIndex = (P.CycleIndex + sourceIndex * P.TrailLength) % bufferLength;

    TrailPoints[targetIndex] = SourcePoints[sourceIndex];

    if (P.AddSeparatorThreshold > 0.0f) {
        float3 lastPos = float3(TrailPoints[(targetIndex - 1 + bufferLength) % bufferLength].Position);
        float3 pos = float3(SourcePoints[sourceIndex].Position);
        if (length(lastPos - pos) > P.AddSeparatorThreshold)
            TrailPoints[targetIndex].Scale = packed_float3(NAN, NAN, NAN);
    }

    // Flag the follow slot W (Scale) as a NaN line separator.
    TrailPoints[(targetIndex + 1) % bufferLength].Scale = packed_float3(NAN, NAN, NAN);
}
