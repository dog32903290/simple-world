#ifndef HASH_METAL_H
#define HASH_METAL_H

// Ported from TiXL:
//   external/tixl/Operators/Lib/Assets/shaders/shared/hash-functions.hlsl
//
// Ported functions:
//   float4 hash41u(uint x)  — uint → float4, full 4-channel integer hash
//
// HLSL→MSL notes:
//   - uint, uint4, float, float4 : identical names in MSL
//   - unsigned integer overflow wraps identically (both are defined wrap-around)
//   - bit-shift (>>, <<) and XOR (^) : identical semantics
//   - float(0xffffffffU) : valid in MSL, no change
//   - float4(uint4) explicit cast : valid in MSL, no change
//   - No asfloat/asuint reinterpret casts needed here (only integer→float conversions)
//   - #define macros : valid in MSL (C-based preprocessor)

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

#endif // HASH_METAL_H
