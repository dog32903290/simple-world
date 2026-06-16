#ifndef HASH_METAL_H
#define HASH_METAL_H

// Ported from TiXL:
//   external/tixl/Operators/Lib/Assets/shaders/shared/hash-functions.hlsl
//
// Ported functions:
//   float4 hash41u(uint x)  — uint → float4, full 4-channel integer hash
//   float3 hash31(float p)  — float → float3 (Dave Hoskins / hash-functions.hlsl line 65-70)
//   float  hash11(float p)  — float → float  (Dave Hoskins / hash-functions.hlsl line 10-16)
//   float2 hash22(float2 p) — float2 → float2 (Dave Hoskins / hash-functions.hlsl line 47-52)
//   float4 hash42(float2 p) — float2 → float4 (hash-functions.hlsl line 136-141)
//
// HLSL→MSL notes:
//   - uint, uint4, float, float4 : identical names in MSL
//   - unsigned integer overflow wraps identically (both are defined wrap-around)
//   - bit-shift (>>, <<) and XOR (^) : identical semantics
//   - float(0xffffffffU) : valid in MSL, no change
//   - float4(uint4) explicit cast : valid in MSL, no change
//   - No asfloat/asuint reinterpret casts needed here (only integer→float conversions)
//   - #define macros : valid in MSL (C-based preprocessor)
//   - HLSL frac() -> MSL fract() (same semantics, fractional part)

#include <metal_stdlib>
using namespace metal;

#define _PRIME0 13331U
#define _PRIME1 1345777U
#define _PRIME2 98777177U

// 4 out, 1 uint in — integer-domain hash
// Source: IQ-style LCG cascade, GLIBC constant k = 1103515245
inline float4 hash41u(uint x)
{
    const uint k = 1103515245U; // GLIB C
    x *= _PRIME0;
    x = ((x >> 8U) ^ x) * k;
    uint y = ((x >> 8U) ^ x) * k;
    uint z = ((y >> 8U) ^ x) * k;
    uint w = ((z >> 8U) ^ y) * k;
    uint4 i4 = uint4(x, y, z, w);
    return float4(i4) * (1.0f / float(0xffffffffU));
}

// 3 out, 1 float in — Dave Hoskins hash (verbatim port of hash-functions.hlsl lines 65-70).
// HLSL frac() -> MSL fract(); float3/dot identical. Used by PointAttributesFromNoise variation.
inline float3 hash31(float p)
{
    float3 p3 = fract(float3(p, p, p) * float3(0.1031f, 0.1030f, 0.0973f));
    p3 += dot(p3, p3.yzx + 33.33f);
    return fract((p3.xxy + p3.yzz) * p3.zyx);
}

// 1 out, 1 float in — Dave Hoskins hash (verbatim port of hash-functions.hlsl lines 10-16).
// HLSL frac() -> MSL fract(). Used by Rings and point-pattern ops.
inline float hash11(float p)
{
    p = fract(p * 0.1031f);
    p *= p + 33.33f;
    p *= p + p;
    return fract(p);
}

// 2 out, 2 float in — Dave Hoskins hash (verbatim port of hash-functions.hlsl lines 47-52).
// HLSL frac() -> MSL fract(); swizzles .xyx/.yz/.zy identical.
inline float2 hash22(float2 p)
{
    float3 p3 = fract(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
    p3 += dot(p3, p3.yzx + 33.33f);
    return fract((p3.xx + p3.yz) * p3.zy);
}

// 4 out, 2 float in — Dave Hoskins hash (verbatim port of hash-functions.hlsl lines 136-141).
// HLSL frac() -> MSL fract(); float4 swizzles .xyxy/.wzxy/.xxyz/.yzzw/.zywx identical.
inline float4 hash42(float2 p)
{
    float4 p4 = fract(float4(p.xyxy) * float4(0.1031f, 0.1030f, 0.0973f, 0.1099f));
    p4 += dot(p4, p4.wzxy + 33.33f);
    return fract((p4.xxyz + p4.yzzw) * p4.zywx);
}

#endif // HASH_METAL_H
