// computeshaderstage_clearsomepoints — ABI-repacked kernel driving ClearSomePoints through the GENERIC
// ComputeShaderStage atom after the flat atom retires.
//
// Faithful MSL port of external/tixl/Operators/Lib/Assets/shaders/points/modify/ClearSomePoints.hlsl
// with the generic const-buffer / SRV / UAV binding contract (NOT the fused scalar clearsomepoints.metal's
// ClearSomePointsParams struct). Driven by the raw bytes FloatsToBuffer / IntsToBuffer assemble.
//
// A count-preserving MODIFIER: each point is conditionally "killed" (Scale := NAN) based on a per-block
// hash of (Resolution, Seed, Repeat, i). Count is inherited from upstream (no compaction).
//
// ── HLSL cbuffer byte layout (wire order == .t3 FloatsToBuffer / IntsToBuffer .Params) ────────────────
//   b0 (ClearSomePoints.hlsl Params register b0, FloatsToBuffer — NO 16-byte pad, tight float[]):
//        cb0[0] = Ratio
//   b1 (ClearSomePoints.hlsl Params register b1, IntsToBuffer — 16-byte-padded int[], ceil-to-4):
//        cb1[0] = Seed   cb1[1] = Repeat   cb1[2] = Resolution   cb1[3] = 0 (pad)
//   The .t3 wires FloatsToBuffer BEFORE IntsToBuffer onto ComputeShaderStage.ConstantBuffers, so the
//   generic seam binds FloatsToBuffer at b0 and IntsToBuffer at b1 (ClearSomePoints.t3 Connections:122/128;
//   b1 wire order Seed→Repeat→Resolution == ClearSomePoints.hlsl :11-16, matching IntsToBuffer.Params).
//   t0 (SRV) = SourcePoints ; u0 (UAV) = ResultPoints (fresh StructuredBufferWithViews)
// Math is line-for-line the fused clearsomepoints.metal (faithful ClearSomePoints.hlsl port); only the
// parameter SOURCE changes (raw cbuffer bytes vs a marshalled struct).
//
// ── NAMED FORKS (preserved verbatim from clearsomepoints.metal) ───────────────────────────────────────
//   • cwMod: repeat==0 (true div-by-zero) pinned to the D3D11 functional-spec result 0xFFFFFFFF; repeat<0
//     takes the real signed-modulo path (both operands signed) with the HLSL `if x<0 x+=repeat` correction.
//   • hash11u: TiXL LCG from hash-functions.hlsl (MSL unsigned wraps identically to HLSL → verbatim).
//   • NAN: p.Scale = NAN → packed_float3(NAN,NAN,NAN) marks the point dead (invisible downstream).
#include <metal_stdlib>
#include "tixl_point.h"                     // SwPoint (64B)
#include "computeshaderstage_params.h"      // CS_CB_BASE / CS_SRV_BASE / CS_UAV_BASE
using namespace metal;

#define CSCLR_PRIME0 13331U
#define CSCLR_PRIME1 1345777U

// hash11u — uint -> float in [0,1). Verbatim port from TiXL hash-functions.hlsl:115-123.
inline float csClrHash11u(uint x) {
    const uint k = 1103515245u;
    x *= CSCLR_PRIME0;
    x = ((x >> 8u) ^ x) * k;
    x = ((x >> 8u) ^ x) * k;
    return float(x) * (1.0f / float(0xffffffffu));
}

// Mod(val, repeat) — TiXL's ClearSomePoints.hlsl lines 21-27 (see NAMED FORK above; same as the fused kernel).
inline uint csClrMod(uint val, int repeat) {
    if (repeat == 0) return 0xFFFFFFFFu;
    int x = (repeat > 0) ? (int)(val % (uint)repeat) : ((int)val % repeat);
    if (x < 0) x += repeat;
    return (uint)x;
}

kernel void computeshaderstage_clearsomepoints(
    const device SwPoint* SourcePoints [[buffer(CS_SRV_BASE + 0)]],   // t0
    device SwPoint*       ResultPoints [[buffer(CS_UAV_BASE + 0)]],   // u0
    constant float*       cb0          [[buffer(CS_CB_BASE + 0)]],    // b0: [Ratio]
    constant int*         cb1          [[buffer(CS_CB_BASE + 1)]],    // b1: [Seed, Repeat, Resolution, pad]
    constant uint&        numStructs   [[buffer(CS_CB_BASE + 3)]],    // dispatch bound (SRV element count)
    uint3 tid [[thread_position_in_grid]])
{
    if (tid.x >= numStructs) return;
    uint idx = tid.x;

    float Ratio     = cb0[0];
    int   Seed      = cb1[0];
    int   Repeat    = cb1[1];
    int   Resolution= cb1[2];

    // ClearSomePoints.hlsl line 36 verbatim:
    //   uint pointU = ((i.x - Mod(i.x, Resolution) + 1) * _PRIME0 + Seed * _PRIME1)
    //                  % (Repeat == 0 ? 999999999 : Repeat);
    uint blockStart = idx - csClrMod(idx, Resolution);
    uint pointU = ((blockStart + 1u) * CSCLR_PRIME0 + (uint)Seed * CSCLR_PRIME1)
                  % (uint)(Repeat == 0 ? 999999999 : Repeat);
    float h = csClrHash11u(pointU);

    SwPoint p = SourcePoints[idx];
    if (h <= Ratio) {
        p.Scale = packed_float3(NAN, NAN, NAN);  // dead point (TiXL: p.Scale = NAN)
    }
    ResultPoints[idx] = p;
}
