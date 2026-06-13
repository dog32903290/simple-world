// wrappoints.metal — faithful Metal port of TiXL's WrapPoints.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/modify/WrapPoints.hlsl
// A count-preserving MODIFIER: each point's Position is wrapped (toroidally tiled) into a box of
// the given Size centered at Position. The off-box body of the kernel (the commented-out random
// jitter block in the .hlsl) is dead in TiXL too — we port only the live tail.
//
// TiXL parity (WrapPoints.hlsl lines 90-96):
//   ResultPoints[i] = SourcePoints[i];
//   float3 a = (SourcePoints[i].Position - Position + Size/2);
//   float3 newPosition = mod(a, Size) - Size/2 + Position;
//   ResultPoints[i].Position = newPosition;
//
// NAMED FORK — floored modulo:
//   TiXL's `mod` is the macro from shared/quat-functions.hlsl line 16:
//     #define mod(x, y) ((x) - (y) * floor((x) / (y)))
//   i.e. GLSL floored-mod, NOT C truncated fmod. MSL's built-in fmod() is TRUNCATED (C semantics)
//   and would give the WRONG sign for negative inputs (a point left of the box would not wrap in).
//   We therefore define floorMod() locally with the same formula TiXL uses. This is the whole
//   point of the op for negative-side points, so it is load-bearing, not cosmetic.
#include <metal_stdlib>
#include "tixl_point.h"              // SwPoint (64B layout)
#include "wrappoints_params.h"       // WrapPointsParams, WrapPointsBinding
using namespace metal;

// GLSL floored modulo (== TiXL `mod` macro). Component-wise; guards Size==0 -> 0 (no div-by-zero).
// select(a, b, c) returns c ? b : a per component; so where y==0 we keep q=0 (no NaN from /0).
inline float3 floorMod(float3 x, float3 y) {
    float3 q = select(float3(0.0f), floor(x / y), y != float3(0.0f));
    return x - y * q;
}

kernel void wrappoints(
    device const SwPoint* SourcePoints [[buffer(WRAPPOINTS_SourcePoints)]],
    device       SwPoint* ResultPoints [[buffer(WRAPPOINTS_ResultPoints)]],
    constant WrapPointsParams& P       [[buffer(WRAPPOINTS_Params)]],
    uint3 i [[thread_position_in_grid]])
{
    uint idx = i.x;
    if (idx >= P.Count) return;

    float3 Position = float3(P.PositionX, P.PositionY, P.PositionZ);
    float3 Size     = float3(P.SizeX, P.SizeY, P.SizeZ);

    SwPoint p = SourcePoints[idx];          // ResultPoints[i] = SourcePoints[i] (carry attributes)
    float3 a = (p.Position - Position + Size * 0.5f);
    p.Position = floorMod(a, Size) - Size * 0.5f + Position;
    ResultPoints[idx] = p;
}
