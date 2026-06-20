// repeatatpoints.metal — Metal port of TiXL RepeatAtGPoints.hlsl
// Reference: external/tixl/Operators/Lib/Assets/shaders/points/generate/RepeatAtGPoints.hlsl
//
// Faithful port. RepeatAtPoints places each Source point into EACH Target point's local frame =
// the full cartesian product (count = source.N * target.N, the count-product seam's canonical op).
//
//   Linear  (ConnectPointsMode==0): sourceIndex = i % (source.N + AddSep); targetIndex = (i/srcLen) % target.N
//   Interwoven (==1):               sourceIndex = i / target.N;            targetIndex = i % target.N
//   per result point:
//     pLocal   = ApplyTargetOrientation ? qRotateVec3(source.Pos, targetRot) : source.Pos
//     factors  = {1, tgt.F1, tgt.F2, src.F1, src.F2, src.F1*tgt.F1, src.F2*tgt.F2}
//     s        = Scale * factors[ScaleFactorMode] * (ApplyTargetScale ? tgt.Scale : 1)
//     out.Pos  = pLocal * s + target.Pos
//     out.Rot  = ApplyTargetOrientation ? qMul(targetRot, sourceRot) : sourceRot
//     out.Color= src.Color * tgt.Color;  out.Scale = src.Scale * tgt.Scale
//     out.F1   = factors[SetF1To];        out.F2    = factors[SetF2To]
//
// SwPoint<->TiXL Point: Position/Color/Scale 1:1; Rotation<-Orientation; FX1<-F1; FX2<-F2 (四流 rename).
//
// AddSeparators (see repeatatpoints_params.h): the count now includes the separator expansion
// (Linear: sourceLength=source.N+1), so the per-loop trailing NaN-divider row below FIRES at the
// production default (AddSeparators=true). The cook supplies ResultCount=(source.N+1)*target.N so the
// last slot of each source loop (sourceIndex==source.N) lands as a separator — byte-exact with HLSL.

#include <metal_stdlib>
using namespace metal;

#include "../src/runtime/tixl_point.h"
#include "../src/runtime/repeatatpoints_params.h"
#include "shared/quat.metal.h"   // qMul(q1,q2), qRotateVec3(v,q)

[[kernel]]
void repeatatpoints(
    const device SwPoint*           SourcePoints [[buffer(REPEATATPOINTS_SourcePoints)]],
    const device SwPoint*           TargetPoints [[buffer(REPEATATPOINTS_TargetPoints)]],
    device       SwPoint*           Result       [[buffer(REPEATATPOINTS_Result)]],
    constant     RepeatAtPointsParams& P         [[buffer(REPEATATPOINTS_Params)]],
    constant     uint&              SourceCount   [[buffer(4)]],
    constant     uint&              TargetCount   [[buffer(5)]],
    constant     uint&              ResultCount   [[buffer(6)]],
    uint3                           tid           [[thread_position_in_grid]])
{
    uint i = tid.x;
    if (i >= ResultCount) return;

    uint sourcePointCount = SourceCount;
    uint targetPointCount = TargetCount;

    bool isSeperator = false;
    uint sourceIndex;
    uint targetIndex;

    if (P.ConnectPointsMode == 0) {  // Linear
        uint sourceLength = sourcePointCount + (uint)P.AddSeperators;
        sourceIndex = i % sourceLength;
        targetIndex = (i / sourceLength) % targetPointCount;
        isSeperator = (P.AddSeperators != 0) && (sourceIndex == sourcePointCount);
    } else {                          // Interwoven
        uint loopLength = targetPointCount;
        sourceIndex = i / loopLength;
        targetIndex = i % loopLength;
        isSeperator = (targetIndex == loopLength - 1u);
    }

    if (isSeperator) {
        Result[i].Position = packed_float3(0.0f, 0.0f, 0.0f);
        Result[i].Scale    = packed_float3(NAN, NAN, NAN);
        return;
    }

    SwPoint sourceP = SourcePoints[sourceIndex];
    SwPoint targetP = TargetPoints[targetIndex];
    float4 sourceRot = normalize(sourceP.Rotation);
    float4 targetRot = normalize(targetP.Rotation);

    float3 srcPos = float3(sourceP.Position);
    float3 pLocal = (P.ApplyTargetOrientation != 0)
                        ? qRotateVec3(srcPos, targetRot)
                        : srcPos;

    float factors[7] = { 1.0f, targetP.FX1, targetP.FX2, sourceP.FX1, sourceP.FX2,
                         sourceP.FX1 * targetP.FX1, sourceP.FX2 * targetP.FX2 };
    int sfMode = clamp(P.ScaleFactorMode, 0, 6);
    int f1Mode = clamp(P.SetF1To, 0, 6);
    int f2Mode = clamp(P.SetF2To, 0, 6);

    float3 tgtScale = float3(targetP.Scale);
    float3 s = P.Scale * factors[sfMode] * (P.ApplyTargetScale != 0 ? tgtScale : float3(1.0f));

    float3 outPos = pLocal * s + float3(targetP.Position);
    Result[i].Position = packed_float3(outPos);
    Result[i].Rotation = (P.ApplyTargetOrientation != 0) ? qMul(targetRot, sourceRot) : sourceRot;
    Result[i].Color    = sourceP.Color * targetP.Color;
    Result[i].FX1      = factors[f1Mode];
    Result[i].FX2      = factors[f2Mode];
    Result[i].Scale    = packed_float3(float3(sourceP.Scale) * float3(targetP.Scale));
}
