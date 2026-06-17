// pairpointsforgridwalklines.metal — Metal port of TiXL PairPointsForGridWalkLine.hlsl
// Reference: external/tixl/Operators/Lib/Assets/shaders/points/combine/PairPointsForGridWalkLine.hlsl
//
// Faithful 1:1 port. Each pair (line) = StartPoints[lineIndex%countA] -> TargetPoints[lineIndex%countB],
// rendered as an 11-step grid-walk polyline (the 11th step is the NaN divider). Output count =
// max(countA,countB) * 11 (sized by the cook fn / count policy).
//
// HLSL->MSL notes:
//   - StructuredBuffer<Point>.GetDimensions() has no MSL equivalent for constant buffers; the cook
//     passes (totalCount, countA, countB) via the Counts cbuffer (PAIRGRIDWALK_Counts) verbatim.
//   - frac->fract, lerp->mix, sqrt(-1)->NAN sentinel, fmod identical, saturate identical.
//   - hash21 ported inline from hash-functions.hlsl lines 38-43; hash41u from shared/hash.metal.h.
//   - The HLSL `quat-functions.hlsl` include is unused by the kernel body — not needed here.
//   - HLSL `>` (not `>=`) guards are kept verbatim (faithful).
#include <metal_stdlib>
using namespace metal;

#include "../src/runtime/tixl_point.h"
#include "../src/runtime/pairpointsforgridwalklines_params.h"
#include "shared/hash.metal.h"   // hash41u

// hash21 — 2 out, 1 in (verbatim port of hash-functions.hlsl lines 38-43). frac->fract.
static inline float2 hash21(float p)
{
    float3 p3 = fract(float3(p, p, p) * float3(0.1031f, 0.1030f, 0.0973f));
    p3 += dot(p3, p3.yzx + 33.33f);
    return fract((p3.xx + p3.yz) * p3.zy);
}

// PairPointsForGridWalkLine.hlsl lines 22-45 — transition step factors + axis orders.
constant int3 TransitionSteps[] = {
    int3(0, 0, 0), // 0
    int3(0, 0, 1), // 1
    int3(1, 0, 1), // 2
    int3(1, 1, 1), // 3
    int3(1, 1, 2), // 4
    int3(2, 1, 2), // 5
    int3(2, 2, 2), // 6
    int3(2, 2, 3), // 7
    int3(3, 2, 3), // 8
    int3(3, 3, 3), // 9
    int3(3, 3, 3), // 10
};

constant int3 AxisOrders[] = {
    int3(2, 1, 0),
    int3(0, 2, 1),
    int3(1, 0, 2),
    int3(2, 1, 0),
    int3(2, 0, 1),
};

[[kernel]]
void pairpointsforgridwalklines(
    constant SwPoint*                              StartPoints  [[buffer(PAIRGRIDWALK_StartPoints)]],
    constant SwPoint*                              TargetPoints [[buffer(PAIRGRIDWALK_TargetPoints)]],
    device   SwPoint*                              ResultPoints [[buffer(PAIRGRIDWALK_Result)]],
    constant PairPointsForGridWalkLinesParams&     P            [[buffer(PAIRGRIDWALK_Params)]],
    constant uint3&                                Counts       [[buffer(PAIRGRIDWALK_Counts)]],
    uint3                                          i            [[thread_position_in_grid]])
{
    const float3 GridSize      = float3(P.GridSizeX, P.GridSizeY, P.GridSizeZ);
    const float3 GridOffset    = float3(P.GridOffsetX, P.GridOffsetY, P.GridOffsetZ);
    const float3 RandomizeGrid = float3(P.RandomizeGridX, P.RandomizeGridY, P.RandomizeGridZ);
    const float  StrokeLength  = P.StrokeLength;
    const float  Speed         = P.Speed;
    const float  PhaseOffset   = P.PhaseOffset;

    uint totalCount = Counts.x;  // HLSL: ResultPoints.GetDimensions (output buffer length)
    uint countA     = Counts.y;  // HLSL: StartPoints.GetDimensions
    uint countB     = Counts.z;  // HLSL: TargetPoints.GetDimensions

    if (i.x > totalCount)
        return;

    const int stepsPerPairCount = 11;
    if (i.x > (uint)totalCount * stepsPerPairCount)
        return;

    uint lineIndex     = i.x / stepsPerPairCount;
    uint lineStepIndex = i.x % stepsPerPairCount;

    SwPoint A = StartPoints[lineIndex % countA];
    SwPoint B = TargetPoints[lineIndex % countB];

    float2 hash = hash21(lineIndex);
    int3 axisOrder = AxisOrders[(int)(hash.x * 4)];

    float3 randomOffset = (hash41u(lineIndex + 321) * 2.0f - 1.0f).xyz * RandomizeGrid;
    float3 posA = (float3(A.Position) + 0.0001f) / GridSize + fmod(GridOffset, GridSize);
    float3 posB = (float3(B.Position) + 0.0001f) / GridSize + fmod(GridOffset, GridSize);

    float3 transition[4] = {
        posA,
        floor(posA) + (hash.x > 0.5f ? 1.0f : 0.0f) + randomOffset,
        floor(posB) + (hash.y > 0.5f ? 1.0f : 0.0f) + randomOffset,
        posB
    };

    float3 previousPos = float3(0.0f);
    float3 pos = float3(0.0f);
    float d = 0.0f;

    float4 stepPositions[11];

    for (int step = 0; step <= 10; step++)
    {
        int3 factorsForStep = TransitionSteps[step];

        pos = float3(
            transition[factorsForStep[axisOrder.x]].x,
            transition[factorsForStep[axisOrder.y]].y,
            transition[factorsForStep[axisOrder.z]].z
        );

        if (step > 0)
        {
            d += length(pos - previousPos);
        }

        stepPositions[step] = float4(pos,
                                     1.0f - A.FX1 * Speed * StrokeLength + d / StrokeLength + PhaseOffset);
        previousPos = pos;
    }

    // HLSL: prev = stepPositions[max(0, lineStepIndex-1)]. lineStepIndex is uint, so at step 0
    // `lineStepIndex-1` underflows; on D3D the OOB register-array read returns 0 (D3D11 spec).
    // MSL OOB on a thread-local array is UB, so we encode D3D's "OOB -> zero" explicitly: step 0
    // gets float4(0). For steps 1..10 the index is in-bounds and identical to the HLSL expression.
    float4 prev    = (lineStepIndex == 0u) ? float4(0.0f) : stepPositions[lineStepIndex - 1u];
    float4 current = stepPositions[lineStepIndex];
    float4 next    = stepPositions[min(lineStepIndex + 1u, 10u)];

    float w = 1.0f;
    const float NaN = sqrt(-1.0f);

    pos = current.xyz;
    d = current.w;

    // Case A1
    if (current.w < 0.0f && next.w > 1.0f) {
        float a = abs(current.w);
        float b = next.w;
        float f = saturate(b / (a + b));
        pos.xyz = mix(current.xyz, next.xyz, 1.0f - f);
        d = 0.0f;
    }
    // Case A2
    else if (prev.w < 0.0f && current.w > 1.0f) {
        float a = abs(current.w) - 1.0f;
        float b = abs(prev.w) + 1.0f;
        float f = saturate(a / (a + b));
        pos.xyz = mix(prev.xyz, current.xyz, 1.0f - f);
        d = 1.0f;
    }
    // Case B0
    else if (current.w <= 0.0f && next.w < 0.0f) {
        w = NaN;
    }
    // Case B1
    else if (current.w <= 0.0f && next.w > 0.0f && next.w < 1.0f) {
        float a = -current.w;
        float b = next.w;
        float f = saturate(a / (a + b));
        pos.xyz = mix(pos, next.xyz, f);
        d = 0.0f;
    }
    // Case B2
    else if (current.w >= 0.0f && next.w < 1.0f) {
        // (no-op, faithful to HLSL)
    }
    // Case B3
    else if (prev.w < 1.0f && current.w > 1.0f) {
        float a = 1.0f - prev.w;
        float b = current.w - 1.0f;
        float f = saturate(a / (a + b));
        pos.xyz = mix(prev.xyz, pos, f);
        d = 1.0f;
    }
    // Case B4
    else if (prev.w > 1.0f && current.w > 1.0f) {
        w = NaN;
    }

    SwPoint p = A;

    p.Position = packed_float3((pos - fmod(GridOffset, 1.0f)) * GridSize);
    p.FX1 = 1.0f - d * w;

    if (lineStepIndex == 10u)
        w = NaN;  // NaN for divider

    float scaleFactor = isnan(w * d) ? NaN : 1.0f;

    p.Scale = packed_float3(0.5f * scaleFactor);

    ResultPoints[i.x] = p;
}
