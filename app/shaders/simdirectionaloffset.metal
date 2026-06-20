// simdirectionaloffset.metal — faithful Metal port of TiXL's SimDirectionalOffset.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/sim/SimDirectionalOffset.hlsl
// A count-preserving MODIFIER. Mode 0 (Legacy): Position += Direction*Amount*(1+hash11*RandomAmount).
// Mode 1 (EncodeInRotation): the offset is added to the velocity vector encoded as
// Rotation * (v+1), re-aimed with qLookAt. Writes ResultPoints (same count).
//
// TiXL parity (SimDirectionalOffset.hlsl):
//   float3 offset = Direction * Amount * (1 + hash11(i.x) * RandomAmount);
//   if (Mode < 0.5) { ResultPoints[i.x].Position += offset; return; }
//   float4 rot = ResultPoints[i.x].Rotation; float4 normalizedRot;
//   float v = q_separate_v(rot, normalizedRot);            // v = length(rot) - 1
//   float3 forward = qRotateVec3(float3(0,0,1), normalizedRot) * v;
//   forward += offset;
//   float newV = length(forward);
//   float4 newRotation = qLookAt(normalize(forward), float3(0,0,1));
//   ResultPoints[i.x].Rotation = q_encode_v(newRotation, newV);   // newRotation * (newV+1)
//
// Forks from TiXL (named):
//   1. In-place RWStructuredBuffer<LegacyPoint> -> sw source+dest (const + writable).
//   2. q_separate_v / q_encode_v are NOT in sw's shared quat.metal.h; defined inline here
//      (verbatim from external/tixl .../shared/quat-functions.hlsl) to avoid polluting the
//      shared header. HLSL `out float4 normalized` -> MSL `thread float4&`.
#include <metal_stdlib>
#include "tixl_point.h"                    // SwPoint (64B layout)
#include "simdirectionaloffset_params.h"   // SimDirectionalOffsetParams, SimDirectionalOffsetBinding
#include "shared/hash.metal.h"             // hash11(float)
#include "shared/quat.metal.h"             // qRotateVec3, qLookAt
using namespace metal;

// Verbatim port of TiXL quat-functions.hlsl q_separate_v / q_encode_v (kept local: not shared).
inline float q_separate_v(float4 q, thread float4& normalized) {
    float l = length(q);
    normalized = q / l;
    return l - 1.0f;
}
inline float4 q_encode_v(float4 q, float v) {
    return q * (v + 1.0f);
}

kernel void simdirectionaloffset(
    device const SwPoint* SourcePoints        [[buffer(SIMDIRECTIONALOFFSET_SourcePoints)]],
    device       SwPoint* ResultPoints        [[buffer(SIMDIRECTIONALOFFSET_ResultPoints)]],
    constant SimDirectionalOffsetParams& P    [[buffer(SIMDIRECTIONALOFFSET_Params)]],
    uint3 i [[thread_position_in_grid]])
{
    uint idx = i.x;
    if (idx >= P.Count) return;

    SwPoint p = SourcePoints[idx];

    float3 Direction = float3(P.DirectionX, P.DirectionY, P.DirectionZ);
    float3 offset = Direction * P.Amount * (1.0f + hash11((float)idx) * P.RandomAmount);

    if (P.Mode < 0.5f) {
        p.Position += offset;
        ResultPoints[idx] = p;
        return;
    }

    float4 rot = p.Rotation;
    float4 normalizedRot;
    float v = q_separate_v(rot, normalizedRot);

    float3 forward = qRotateVec3(float3(0.0f, 0.0f, 1.0f), normalizedRot) * v;
    forward += offset;

    float newV = length(forward);
    float4 newRotation = qLookAt(normalize(forward), float3(0.0f, 0.0f, 1.0f));

    p.Rotation = q_encode_v(newRotation, newV);
    ResultPoints[idx] = p;
}
