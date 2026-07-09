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
//   1. Mod(val, repeat) div/mod-by-zero (ClearSomePoints.hlsl lines 21-27, `Mod(i.x, Resolution)`
//      called with Resolution<=0, i.e. a genuine `val % 0`): FORK RETIRED 2026-07-10. TiXL's HLSL
//      text has no caller-side guard, but the D3D11 functional spec DEFINES integer div/mod by
//      zero as returning 0xFFFFFFFF (all bits set) on real DX11 hardware -- that IS TiXL's actual
//      ground-truth behavior, not an ambiguity (mathv_ref_clearsomepoints.h's hlslMod already pins
//      this convention). This kernel used to short-circuit the whole `repeat<=0` range to a silent
//      0 (an unverified same-repo fork); now only the true div-by-zero case (repeat==0) is pinned
//      to the D3D 0xFFFFFFFF convention. Genuine negative Resolution (repeat<0 — a NONZERO divisor,
//      not a div-by-zero case at all) takes the real signed-modulo path instead of being swept into
//      that shortcut, matching HLSL's `int Mod(int val, int repeat)` (both operands signed)
//      bit-for-bit; MSL integer % can give negative results for negative val — we replicate TiXL's
//      `if x<0 x+=repeat` correction for both branches.
//      Verified via --selftest-mathv-clearsomepoints's probeResolutionNonPositive: 16/16 agree with
//      the HLSL-pinned CPU oracle after this fix (was 11/16, i.e. 5/16 diverge, under the old
//      blanket short-circuit — see selftests_mathv_clearsomepoints.cpp). XS 2026-07-10 verification:
//      Resolution's Min=1/ClampMin=true (ClearSomePoints.t3ui:13-22) is an Editor-UI-only guard;
//      TiXL's Core eval path applies ZERO clamp, so Resolution<=0 (including the .t3-authored
//      DEFAULT of 0) is a reachable value that flows straight into this kernel — not dead code.
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
// Non-negative integer remainder. repeat==0 (the actual div-by-zero case): pinned to the D3D11
// functional spec's defined div/mod-by-zero result, 0xFFFFFFFF (see NAMED FORK 1 above — FORK
// RETIRED, this now matches TiXL's real DX11 behavior instead of silently returning 0). repeat<0
// (a normal NONZERO divisor, not a div-by-zero case) takes the real signed-modulo path, matching
// HLSL's `int Mod(int val, int repeat)` (both operands signed) bit-for-bit — `val` is cast to
// `int` first so a negative `repeat` isn't bit-reinterpreted into a huge unsigned divisor.
inline uint cwMod(uint val, int repeat) {
    if (repeat == 0) return 0xFFFFFFFFu;
    int x = (repeat > 0) ? (int)(val % (uint)repeat) : ((int)val % repeat);
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
