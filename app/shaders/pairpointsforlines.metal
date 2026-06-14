// pairpointsforlines.metal — Metal port of TiXL PairPointsForLines.hlsl
// Reference: external/tixl/Operators/Lib/Assets/shaders/points/combine/PairPointsForLines.hlsl
//
// Faithful port of PairPointsForLines.hlsl lines 16-43:
//   pairIndex = threadIdx / 3; pairElement = threadIdx % 3
//   pairElement==1 -> ResultPoints[i] = PointsB[pairIndex % CountB]  (B point)
//   else           -> ResultPoints[i] = PointsA[pairIndex % CountA]  (A point, or NaN divider)
//   pairElement==2 -> ResultPoints[i].Scale = NAN  (divider sentinel)
//   if (InitWTo01) -> A gets FX1=0, B gets FX1=1
//
// Output layout per pair: [A, B, NaN-divider]
// Total output count = ResultCount * 3 (ResultCount = max(CountA, CountB)).

#include <metal_stdlib>
using namespace metal;

#include "../src/runtime/tixl_point.h"
#include "../src/runtime/pairpointsforlines_params.h"

[[kernel]]
void pairpointsforlines(
    constant SwPoint*                  GPoints   [[buffer(PAIRPOINTSFORLINES_GPoints)]],
    constant SwPoint*                  GTargets  [[buffer(PAIRPOINTSFORLINES_GTargets)]],
    device   SwPoint*                  Result    [[buffer(PAIRPOINTSFORLINES_Result)]],
    constant PairPointsForLinesParams& P         [[buffer(PAIRPOINTSFORLINES_Params)]],
    uint3                              tid       [[thread_position_in_grid]])
{
    uint i = tid.x;
    uint resultCount3 = (uint)P.ResultCount * 3u;

    // PairPointsForLines.hlsl line 21: guard
    if (i >= resultCount3)
        return;

    uint pairIndex   = i / 3u;
    uint pairElement = i % 3u;

    // pairElement==1 -> B point; else -> A point (element 0 = A, element 2 = NaN divider)
    if (pairElement == 1u) {
        // PairPointsForLines.hlsl line 29: PointsB[pairIndex % CountB]
        Result[i] = GTargets[pairIndex % (uint)P.CountB];
        if (P.InitWTo01 > 0.5f)
            Result[i].FX1 = 1.0f;
    } else {
        // PairPointsForLines.hlsl line 35: PointsA[pairIndex % CountA]
        Result[i] = GPoints[pairIndex % (uint)P.CountA];
        if (P.InitWTo01 > 0.5f)
            Result[i].FX1 = 0.0f;
    }

    // PairPointsForLines.hlsl line 42: pairElement==2 -> NaN divider (Scale.x = NaN)
    if (pairElement == 2u) {
        Result[i].Scale.x = NAN;
    }
}
