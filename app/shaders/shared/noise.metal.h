#pragma once
//
// noise.metal.h — Metal Shading Language port of TiXL's curlNoise dependency closure.
// Source: external/tixl/Operators/Lib/Assets/shaders/shared/noise-functions.hlsl
// Original: Ian McEwan, Ashima Arts — MIT License
//           https://github.com/ashima/webgl-noise
//
// Functions ported (curlNoise dependency closure only):
//   1. mod289(float3)
//   2. mod289(float4)
//   3. permute(float4)
//   4. taylorInvSqrt(float4)
//   5. snoise(float3)
//   6. snoiseVec3(float3)
//   7. curlNoise(float3)
//
// HLSL → MSL changes applied:
//   - frac()  → fract()   (only substitution needed in this closure)
//   - All other built-ins (step, abs, floor, dot, normalize, min, max,
//     float2/3/4 constructors, swizzles) are identical in MSL.
//   - No matrix mul (mul()) present in this closure.
//   - No lerp() present in this closure.
//

#include <metal_stdlib>
using namespace metal;

// ---------------------------------------------------------------------------
// 1. mod289(float3) — maps x into [0, 289) without branching
// ---------------------------------------------------------------------------
inline float3 mod289(float3 x)
{
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

// ---------------------------------------------------------------------------
// 2. mod289(float4) — same, for float4 (used by permute and snoise internally)
// ---------------------------------------------------------------------------
inline float4 mod289(float4 x)
{
    return x - floor(x * (1.0 / 289.0)) * 289.0;
}

// ---------------------------------------------------------------------------
// 3. permute — polynomial hash, result kept in [0, 289)
// ---------------------------------------------------------------------------
inline float4 permute(float4 x)
{
    return mod289(((x * 34.0) + 1.0) * x);
}

// ---------------------------------------------------------------------------
// 4. taylorInvSqrt — fast inverse square root approximation
// ---------------------------------------------------------------------------
inline float4 taylorInvSqrt(float4 r)
{
    return 1.79284291400159 - 0.85373472095314 * r;
}

// ---------------------------------------------------------------------------
// 5. snoise — 3-D simplex noise, returns value in roughly [-1, 1]
// ---------------------------------------------------------------------------
inline float snoise(float3 v)
{
    const float2 C = float2(1.0 / 6.0, 1.0 / 3.0);
    const float4 D = float4(0.0, 0.5, 1.0, 2.0);

    // First corner
    float3 i  = floor(v + dot(v, C.yyy));
    float3 x0 = v - i + dot(i, C.xxx);

    // Other corners
    float3 g  = step(x0.yzx, x0.xyz);
    float3 l  = 1.0 - g;
    float3 i1 = min(g.xyz, l.zxy);
    float3 i2 = max(g.xyz, l.zxy);

    float3 x1 = x0 - i1 + C.xxx;
    float3 x2 = x0 - i2 + C.yyy; // 2.0*C.x = 1/3 = C.y
    float3 x3 = x0 - D.yyy;      // -1.0+3.0*C.x = -0.5 = -D.y

    // Permutations
    i = mod289(i);
    float4 p = permute(permute(permute(
                   i.z + float4(0.0, i1.z, i2.z, 1.0)) +
                   i.y + float4(0.0, i1.y, i2.y, 1.0)) +
                   i.x + float4(0.0, i1.x, i2.x, 1.0));

    // Gradients: 7x7 points over a square, mapped onto an octahedron.
    float  n_ = 0.142857142857; // 1.0/7.0
    float3 ns = n_ * D.wyz - D.xzx;

    float4 j  = p - 49.0 * floor(p * ns.z * ns.z); // mod(p, 7*7)

    float4 x_ = floor(j * ns.z);
    float4 y_ = floor(j - 7.0 * x_);               // mod(j, N)

    float4 x = x_ * ns.x + ns.yyyy;
    float4 y = y_ * ns.x + ns.yyyy;
    float4 h = 1.0 - abs(x) - abs(y);

    float4 b0 = float4(x.xy, y.xy);
    float4 b1 = float4(x.zw, y.zw);

    float4 s0 = floor(b0) * 2.0 + 1.0;
    float4 s1 = floor(b1) * 2.0 + 1.0;
    float4 sh = -step(h, float4(0, 0, 0, 0));

    float4 a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    float4 a1 = b1.xzyw + s1.xzyw * sh.zzww;

    float3 p0 = float3(a0.xy, h.x);
    float3 p1 = float3(a0.zw, h.y);
    float3 p2 = float3(a1.xy, h.z);
    float3 p3 = float3(a1.zw, h.w);

    // Normalise gradients
    float4 norm = taylorInvSqrt(float4(dot(p0, p0), dot(p1, p1), dot(p2, p2), dot(p3, p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;

    // Mix final noise value
    float4 m = max(0.6 - float4(dot(x0, x0), dot(x1, x1), dot(x2, x2), dot(x3, x3)), 0.0);
    m = m * m;
    return 42.0 * dot(m * m, float4(dot(p0, x0), dot(p1, x1), dot(p2, x2), dot(p3, x3)));
}

// ---------------------------------------------------------------------------
// 6. snoiseVec3 — evaluates snoise at 3 decorrelated offsets → float3 field
// ---------------------------------------------------------------------------
inline float3 snoiseVec3(float3 p)
{
    float s  = snoise(float3(p.x + 0.0001, p.y,         p.z));
    float s1 = snoise(float3(p.y - 19.1,   p.z + 33.4,  p.x + 47.2));
    float s2 = snoise(float3(p.z + 74.2,   p.x - 124.5, p.y + 99.4));
    return float3(s, s1, s2);
}

// ---------------------------------------------------------------------------
// 7. curlNoise — curl of snoiseVec3 field, finite-difference approximation
// ---------------------------------------------------------------------------
inline float3 curlNoise(float3 p)
{
    const float e = 0.001;
    float3 dx = float3(e,   0.0, 0.0);
    float3 dy = float3(0.0, e,   0.0);
    float3 dz = float3(0.0, 0.0, e);

    float3 p_x0 = snoiseVec3(p - dx);
    float3 p_x1 = snoiseVec3(p + dx);
    float3 p_y0 = snoiseVec3(p - dy);
    float3 p_y1 = snoiseVec3(p + dy);
    float3 p_z0 = snoiseVec3(p - dz);
    float3 p_z1 = snoiseVec3(p + dz);

    float x = p_y1.z - p_y0.z - p_z1.y + p_z0.y;
    float y = p_z1.x - p_z0.x - p_x1.z + p_x0.z;
    float z = p_x1.y - p_x0.y - p_y1.x + p_y0.x;

    const float divisor = 1.0 / (2.0 * e);
    return normalize(float3(x, y, z) * divisor);
}
