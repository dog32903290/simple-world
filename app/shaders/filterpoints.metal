// filterpoints.metal — faithful Metal port of TiXL's FilterPoints.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/generate/FilterPoints.hlsl
// (shader is in the "generate" folder in TiXL, but the op is classified as "modify"
// because it consumes an input bag. The key distinction: it CHANGES the output count.)
//
// TiXL parity:
//   - Output count = ResultCount (the Count port), NOT the input count.
//   - Each output slot i draws from:
//       scatterOffset = Scatter > 0.001 ?
//           (float)SourceCount * Scatter * hash11u(i + Seed * SourceCount + StartIndex) : 0
//       index = imod2(StartIndex + (int)(i * StepSize) + scatterOffset, SourceCount)
//       ResultPoints[i] = SourcePoints[index]
//   - imod2: modulo with correct sign for negative values.
//
// hash11u is from shared/hash.metal.h (hash41u is there; hash11u inlined from TiXL).
#include <metal_stdlib>
#include "tixl_point.h"              // SwPoint (64B)
#include "filterpoints_params.h"     // FilterPointsParams, FilterPointsBinding
#include "shared/hash.metal.h"       // hash41u (we derive hash11u from it)
using namespace metal;

// hash11u — uint -> float in [0,1). Inlined from TiXL hash-functions.hlsl.
// TiXL uses: (hash41u(x * _PRIME0).x) cast as a single float.
// We reuse the same LCG logic.
inline float hash11u(uint x) {
    const uint k = 1103515245u;
    x *= _PRIME0;
    x = ((x >> 8u) ^ x) * k;
    return float(x) * (1.0f / float(0xffffffffu));
}

// imod2 — modulo with positive result for negative values (TiXL imod2).
inline int imod2(int val, int repeat) {
    int x = val % repeat;
    return x + ((x < 0) ? repeat : 0);
}

kernel void filterpoints(
    device const SwPoint* SourcePoints [[buffer(FILTERPOINTS_SourcePoints)]],
    device       SwPoint* ResultPoints [[buffer(FILTERPOINTS_ResultPoints)]],
    constant FilterPointsParams& P     [[buffer(FILTERPOINTS_Params)]],
    uint3 i [[thread_position_in_grid]])
{
    int idx = (int)i.x;
    if (idx >= P.ResultCount) return;

    int scatterOffset = 0;
    if (P.Scatter > 0.001f) {
        scatterOffset = (int)((float)P.SourceCount * P.Scatter *
                              hash11u((uint)(idx + P.Seed * P.SourceCount + P.StartIndex)));
    }

    int index = imod2(P.StartIndex + (int)((float)idx * P.StepSize) + scatterOffset,
                      P.SourceCount);
    ResultPoints[idx] = SourcePoints[index];
}
