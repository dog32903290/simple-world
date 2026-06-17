// pairpointsforsplines.metal — Metal port of TiXL PairPointsForSplines.hlsl
// Reference: external/tixl/Operators/Lib/Assets/shaders/points/combine/PairPointsForSplines.hlsl
//            external/tixl/Operators/Lib/Assets/shaders/shared/quat-functions.hlsl (qRotateVec3,qLookAt)
//
// Faithful port. For each (PointsA[pairIndex%countA], PointsB[pairIndex%countB]) pair, emits a
// Hermite cubic spline strip of `SegmentCount` (= pointsPerSegment) output points:
//   indexInLine in [0, SegmentCount-2]  -> interpolated point at f = indexInLine/(SegmentCount-2)
//   indexInLine == SegmentCount-1       -> NaN divider (Scale = NaN)
// Output buffer length = ResultCount = max(countA,countB) * SegmentCount.
//
// Tangents are the per-endpoint forward direction rotated by the endpoint's Rotation, scaled by
// (TangentX + endpointW * TangentX_WFactor). Rotation along the strip = qLookAt(finite-diff
// forward, lerp(tA,tB,f)). Mirrors the .hlsl 1:1 including the "ugly fix" last-point rotation.

#include <metal_stdlib>
using namespace metal;

#include "../src/runtime/tixl_point.h"
#include "../src/runtime/pairpointsforsplines_params.h"

namespace ppfs {

// quat-functions.hlsl: faster quaternion-vector multiply
inline float3 qRotateVec3(float3 v, float4 q) {
    float3 t = 2.0f * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// quat-functions.hlsl: qLookAt (matrix-to-quaternion of the look-at basis)
inline float4 qLookAt(float3 forward, float3 up) {
    float3 right = normalize(cross(forward, up));
    up = normalize(cross(forward, right));

    float m00 = right.x,   m01 = right.y,   m02 = right.z;
    float m10 = up.x,      m11 = up.y,      m12 = up.z;
    float m20 = forward.x, m21 = forward.y, m22 = forward.z;

    float num8 = (m00 + m11) + m22;
    float4 q = float4(0, 0, 0, 1);  // QUATERNION_IDENTITY
    if (num8 > 0.0f) {
        float num = sqrt(num8 + 1.0f);
        q.w = num * 0.5f;
        num = 0.5f / num;
        q.x = (m12 - m21) * num;
        q.y = (m20 - m02) * num;
        q.z = (m01 - m10) * num;
        return q;
    }
    if ((m00 >= m11) && (m00 >= m22)) {
        float num7 = sqrt(((1.0f + m00) - m11) - m22);
        float num4 = 0.5f / num7;
        q.x = 0.5f * num7;
        q.y = (m01 + m10) * num4;
        q.z = (m02 + m20) * num4;
        q.w = (m12 - m21) * num4;
        return q;
    }
    if (m11 > m22) {
        float num6 = sqrt(((1.0f + m11) - m00) - m22);
        float num3 = 0.5f / num6;
        q.x = (m10 + m01) * num3;
        q.y = 0.5f * num6;
        q.z = (m21 + m12) * num3;
        q.w = (m20 - m02) * num3;
        return q;
    }
    float num5 = sqrt(((1.0f + m22) - m00) - m11);
    float num2 = 0.5f / num5;
    q.x = (m20 + m02) * num2;
    q.y = (m21 + m12) * num2;
    q.z = 0.5f * num5;
    q.w = (m01 - m10) * num2;
    return q;
}

// HLSL Interpolate(): Hermite cubic. tA / tB are tangent vectors; tB is negated in the basis term.
inline float3 Interpolate(float t, float3 pA, float3 tA, float3 tB, float3 pB) {
    float t2 = t * t;
    float t3 = t2 * t;
    return (2.0f * t3 - 3.0f * t2 + 1.0f) * pA +
           (t3 - 2.0f * t2 + t)           * tA +
           (-2.0f * t3 + 3.0f * t2)       * pB +
           (t3 - t2)                      * (-tB);
}

}  // namespace ppfs

[[kernel]]
void pairpointsforsplines(
    constant SwPoint*                    PointsA   [[buffer(PAIRPOINTSFORSPLINES_GPoints)]],
    constant SwPoint*                    PointsB   [[buffer(PAIRPOINTSFORSPLINES_GTargets)]],
    device   SwPoint*                    Result    [[buffer(PAIRPOINTSFORSPLINES_Result)]],
    constant PairPointsForSplinesParams& P         [[buffer(PAIRPOINTSFORSPLINES_Params)]],
    uint3                                tid       [[thread_position_in_grid]])
{
    uint i = tid.x;
    uint resultCount = (uint)P.ResultCount;
    if (i >= resultCount) return;

    uint countA = (uint)P.CountA;
    uint countB = (uint)P.CountB;

    // .hlsl: segmentCount = (int)(SegmentCount + 0.5); pointsPerSegment = segmentCount
    int segmentCount = (int)(P.SegmentCount + 0.5f);
    int pointsPerSegment = segmentCount;

    uint pairIndex   = i / (uint)pointsPerSegment;
    uint indexInLine = i % (uint)pointsPerSegment;
    float f = (float)indexInLine / (float)(pointsPerSegment - 2);

    // .hlsl: last point in each segment = NaN divider
    if ((int)indexInLine == pointsPerSegment - 1) {
        Result[i].Scale = packed_float3(NAN, NAN, NAN);
        return;
    }

    uint indexA = (countA > 0u) ? (pairIndex % countA) : 0u;
    uint indexB = (countB > 0u) ? (pairIndex % countB) : 0u;

    float3 posA1   = float3(PointsA[indexA].Position);
    float3 posB1   = float3(PointsB[indexB].Position);
    float3 forward = float3(P.TangentDirection);

    float paW = PointsA[indexA].FX1;
    float pbW = PointsB[indexB].FX1;

    float3 tA = ppfs::qRotateVec3(forward, PointsA[indexA].Rotation) *
                (P.TangentA + paW * P.TangentA_WFactor);
    float3 tB = ppfs::qRotateVec3(forward, PointsB[indexB].Rotation) *
                (P.TangentB + pbW * P.TangentB_WFactor);

    float3 pF = ppfs::Interpolate(f, posA1, tA, tB, posB1);
    Result[i].Position = packed_float3(pF);
    Result[i].Color    = mix(PointsA[indexA].Color, PointsB[indexB].Color, f);
    Result[i].Scale    = packed_float3(
        mix(float3(PointsA[indexA].Scale), float3(PointsB[indexB].Scale), f));

    // .hlsl: finite-difference forward, qLookAt for orientation
    float3 pF2      = ppfs::Interpolate(f + 0.001f, posA1, tA, tB, posB1);
    float3 forward2 = normalize(pF2 - pF);
    float3 refUp    = mix(tA, tB, f);
    Result[i].Rotation = ppfs::qLookAt(forward2, refUp);

    // .hlsl "ugly fix" for the last point's orientation: ResultPoints[segmentCount-2].Rotation *=
    // float4(1,-1,-1,1). fixPpoint is a scalar int; .x on a scalar is itself => index segmentCount-2.
    // This is a per-thread fixed-index write (a deliberate race in the original); ported faithfully
    // with an OOB guard.
    int fixPpoint = segmentCount - 2;
    if (fixPpoint >= 0 && (uint)fixPpoint < resultCount) {
        Result[fixPpoint].Rotation = Result[fixPpoint].Rotation * float4(1, -1, -1, 1);
    }

    // .hlsl: InitWTo01 ? FX1 = f : lerp(A.FX1, B.FX2, f)
    Result[i].FX1 = (P.InitWTo01 > 0.5f) ? f
                                         : mix(PointsA[indexA].FX1, PointsB[indexB].FX2, f);
    Result[i].FX2 = 1.0f;
}
