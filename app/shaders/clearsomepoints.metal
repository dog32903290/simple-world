// clearsomepoints.metal — faithful Metal port of TiXL's ClearSomePoints.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/modify/ClearSomePoints.hlsl
// A count-preserving MODIFIER: each point is conditionally "killed" (Scale := NAN) based
// on a per-block hash of (Resolution, Seed, Repeat, i). Count is inherited from upstream.
//
// TiXL kernel (ClearSomePoints.hlsl lines 20-44, verbatim parity):
//   uint pointU = ((i.x - Mod(i.x, Resolution) + 1) * _PRIME0 + Seed * _PRIME1)
//                  % (Repeat == 0 ? 999999999 : Repeat);
//   float hash = hash11u(pointU);
//   Point p = SourcePoints[i.x];
//   if (hash <= Ratio) p.Scale = NAN;
//   ResultPoints[i.x] = p;
//
// NAMED FORKS:
//   1. Mod(val, repeat): TiXL's Mod (ClearSomePoints.hlsl lines 21-27) returns a
//      non-negative remainder with integer semantics (x = val % repeat; if x<0 x+=repeat).
//      MSL integer % can give negative results for negative val — we replicate the same
//      correction. Resolution=0 makes the divisor 0; TiXL never guards this (the GPU
//      behaviour is implementation-defined in HLSL too); we guard with a clamp to 1 to
//      avoid UB in MSL and match the typical "no grouping" intent of Resolution=0.
//   2. hash11u: TiXL uses an LCG from hash-functions.hlsl:
//        x *= _PRIME0; x = ((x>>8)^x)*k; x = ((x>>8)^x)*k; return float(x)*(1/0xffffffff).
//      MSL unsigned integer arithmetic wraps identically to HLSL, so this is verbatim.
//   3. NAN: TiXL sets p.Scale = NAN (the HLSL NAN literal). In MSL, INFINITY and NAN are
//      available as float constants — we use float(NAN) which expands to quiet NaN.
//      The effect (Scale = NAN) marks the point as "dead" for downstream ops that check
//      for finite Scale (TiXL convention: NAN scale = invisible/skipped point).
#include <metal_stdlib>
#include "tixl_point.h"              // SwPoint (64B layout)
#include "clearsomepoints_params.h"  // ClearSomePointsParams, ClearSomePointsBinding
using namespace metal;

#define _PRIME0 13331U
#define _PRIME1 1345777U

// hash11u — uint -> float in [0,1). Verbatim port from TiXL hash-functions.hlsl:115-123.
inline float hash11u(uint x) {
    const uint k = 1103515245u;
    x *= _PRIME0;
    x = ((x >> 8u) ^ x) * k;
    x = ((x >> 8u) ^ x) * k;
    return float(x) * (1.0f / float(0xffffffffu));
}

// Mod(val, repeat) — TiXL's ClearSomePoints.hlsl lines 21-27.
// Non-negative integer remainder; FORK: guards repeat<=0 -> returns 0 (avoids MSL UB).
inline uint cwMod(uint val, int repeat) {
    if (repeat <= 0) return 0u;
    int x = (int)(val % (uint)repeat);
    if (x < 0) x += repeat;
    return (uint)x;
}

kernel void clearsomepoints(
    device const SwPoint* SourcePoints [[buffer(CLEARSOMEPOINTS_SourcePoints)]],
    device       SwPoint* ResultPoints [[buffer(CLEARSOMEPOINTS_ResultPoints)]],
    constant ClearSomePointsParams& P  [[buffer(CLEARSOMEPOINTS_Params)]],
    uint3 i [[thread_position_in_grid]])
{
    uint idx = i.x;
    if (idx >= P.Count) return;

    // Replicate TiXL ClearSomePoints.hlsl line 36 verbatim:
    //   uint pointU = ((i.x - Mod(i.x, Resolution) + 1) * _PRIME0 + Seed * _PRIME1)
    //                  % (Repeat == 0 ? 999999999 : Repeat);
    uint blockStart = idx - cwMod(idx, P.Resolution);
    uint pointU = ((blockStart + 1u) * _PRIME0 + (uint)P.Seed * _PRIME1)
                  % (uint)(P.Repeat == 0 ? 999999999 : P.Repeat);
    float h = hash11u(pointU);

    SwPoint p = SourcePoints[idx];
    if (h <= P.Ratio) {
        // TiXL: p.Scale = NAN (float3 all-NaN); sets Scale to NaN = dead point.
        // packed_float3 cannot be assigned a scalar float — assign component-wise.
        p.Scale = packed_float3(NAN, NAN, NAN);
    }
    ResultPoints[idx] = p;
}
