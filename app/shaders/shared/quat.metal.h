#pragma once

// Quaternion helpers ported from TiXL's quat-functions.hlsl
// Convention: float4(x, y, z, w) where xyz = imaginary part, w = real part
//
// Ported functions:
//   QUATERNION_IDENTITY  — identity quaternion macro
//   qMul                 — Hamilton product
//   qConjugate           — negate xyz
//   qInverse             — conjugate / squared-norm
//   qFromAngleAxis       — axis-angle to quaternion
//   qFromVectors         — rotation from one vector to another
//   qRotateVec3          — rotate a vector (fast Rodrigues form)
//   qSlerp               — spherical linear interpolation
//   qLookAt              — forward+up to quaternion (Shoemake method)
//
// HLSL→MSL changes applied:
//   - Removed #pragma warning, const static NAN, #ifndef mod macro
//   - Removed HLSL 'in' parameter qualifier on qSlerp
//   - Added #include <metal_stdlib> and using namespace metal
//   - All math intrinsics (cross, dot, normalize, sin, cos, acos, sqrt,
//     length, mix) are from metal_stdlib — no lerp/frac used in these funcs

#include <metal_stdlib>
using namespace metal;

#define QUATERNION_IDENTITY float4(0, 0, 0, 1)

#ifndef PI
#define PI 3.14159265359f
#endif

// Hamilton product: q1 * q2
inline float4 qMul(float4 q1, float4 q2)
{
    return float4(
        q2.xyz * q1.w + q1.xyz * q2.w + cross(q1.xyz, q2.xyz),
        q1.w * q2.w - dot(q1.xyz, q2.xyz));
}

inline float4 qConjugate(float4 q)
{
    return float4(-q.x, -q.y, -q.z, q.w);
}

// https://jp.mathworks.com/help/aeroblks/quaternioninverse.html
inline float4 qInverse(float4 q)
{
    float4 conj = qConjugate(q);
    return conj / (q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
}

// A given angle of rotation about a given axis
inline float4 qFromAngleAxis(float angle, float3 axis)
{
    float sn = sin(angle * 0.5);
    float cs = cos(angle * 0.5);
    return float4(axis * sn, cs);
}

// https://stackoverflow.com/questions/1171849/finding-quaternion-representing-the-rotation-from-one-vector-to-another
inline float4 qFromVectors(float3 v1, float3 v2)
{
    float4 q;
    float d = dot(v1, v2);
    if (d < -0.999999)
    {
        float3 right = float3(1, 0, 0);
        float3 up = float3(0, 1, 0);
        float3 tmp = cross(right, v1);
        if (length(tmp) < 0.000001)
        {
            tmp = cross(up, v1);
        }
        tmp = normalize(tmp);
        q = qFromAngleAxis(PI, tmp);
    }
    else if (d > 0.999999)
    {
        q = QUATERNION_IDENTITY;
    }
    else
    {
        q.xyz = cross(v1, v2);
        q.w = 1 + d;
        q = normalize(q);
    }
    return q;
}

// Vector rotation with a quaternion
// https://blog.molecular-matters.com/2013/05/24/a-faster-quaternion-vector-multiplication/
inline float3 qRotateVec3(float3 v, float4 q)
{
    float3 t = 2 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

inline float4 qSlerp(float4 a, float4 b, float t)
{
    // if either input is zero, return the other.
    if (length(a) == 0.0)
    {
        if (length(b) == 0.0)
        {
            return QUATERNION_IDENTITY;
        }
        return b;
    }
    else if (length(b) == 0.0)
    {
        return a;
    }

    float cosHalfAngle = a.w * b.w + dot(a.xyz, b.xyz);

    if (cosHalfAngle >= 1.0 || cosHalfAngle <= -1.0)
    {
        return a;
    }
    else if (cosHalfAngle < 0.0)
    {
        b.xyz = -b.xyz;
        b.w = -b.w;
        cosHalfAngle = -cosHalfAngle;
    }

    float blendA;
    float blendB;
    if (cosHalfAngle < 0.99)
    {
        // do proper slerp for big angles
        float halfAngle = acos(cosHalfAngle);
        float sinHalfAngle = sin(halfAngle);
        float oneOverSinHalfAngle = 1.0 / sinHalfAngle;
        blendA = sin(halfAngle * (1.0 - t)) * oneOverSinHalfAngle;
        blendB = sin(halfAngle * t) * oneOverSinHalfAngle;
    }
    else
    {
        // do lerp if angle is really small.
        blendA = 1.0 - t;
        blendB = t;
    }

    float4 result = float4(blendA * a.xyz + blendB * b.xyz, blendA * a.w + blendB * b.w);
    if (length(result) > 0.0)
    {
        return normalize(result);
    }
    return QUATERNION_IDENTITY;
}

inline float4 qLookAt(float3 forward, float3 up)
{
    float3 right = normalize(cross(forward, up));
    up = normalize(cross(forward, right));

    float m00 = right.x;
    float m01 = right.y;
    float m02 = right.z;
    float m10 = up.x;
    float m11 = up.y;
    float m12 = up.z;
    float m20 = forward.x;
    float m21 = forward.y;
    float m22 = forward.z;

    float num8 = (m00 + m11) + m22;
    float4 q = QUATERNION_IDENTITY;
    if (num8 > 0.0)
    {
        float num = sqrt(num8 + 1.0);
        q.w = num * 0.5;
        num = 0.5 / num;
        q.x = (m12 - m21) * num;
        q.y = (m20 - m02) * num;
        q.z = (m01 - m10) * num;
        return q;
    }

    if ((m00 >= m11) && (m00 >= m22))
    {
        float num7 = sqrt(((1.0 + m00) - m11) - m22);
        float num4 = 0.5 / num7;
        q.x = 0.5 * num7;
        q.y = (m01 + m10) * num4;
        q.z = (m02 + m20) * num4;
        q.w = (m12 - m21) * num4;
        return q;
    }

    if (m11 > m22)
    {
        float num6 = sqrt(((1.0 + m11) - m00) - m22);
        float num3 = 0.5 / num6;
        q.x = (m10 + m01) * num3;
        q.y = 0.5 * num6;
        q.z = (m21 + m12) * num3;
        q.w = (m20 - m02) * num3;
        return q;
    }

    float num5 = sqrt(((1.0 + m22) - m00) - m11);
    float num2 = 0.5 / num5;
    q.x = (m20 + m02) * num2;
    q.y = (m21 + m12) * num2;
    q.z = 0.5 * num5;
    q.w = (m01 - m10) * num2;
    return q;
}

// qFromMatrix3Precise — rotation matrix (column-major float3x3) to unit quaternion.
// Direct port of TiXL quat-functions.hlsl qFromMatrix3Precise (Shepperd's method).
// MSL float3x3 is column-major: m[col][row]. HLSL _mRC = m[C][R] -> same indexing.
// Convention: float4(x, y, z, w) — matches the rest of this file.
inline float4 qFromMatrix3Precise(float3x3 m)
{
    // m[col][row]: m00=m[0][0], m01=m[0][1], m02=m[0][2],
    //              m10=m[1][0], m11=m[1][1], m12=m[1][2],
    //              m20=m[2][0], m21=m[2][1], m22=m[2][2]
    float tr = m[0][0] + m[1][1] + m[2][2];
    if (tr > 0.0f) {
        float S = sqrt(tr + 1.0f) * 2.0f;  // S = 4*qw
        return float4(
            (m[1][2] - m[2][1]) / S,
            (m[2][0] - m[0][2]) / S,
            (m[0][1] - m[1][0]) / S,
            0.25f * S);
    } else if ((m[0][0] > m[1][1]) && (m[0][0] > m[2][2])) {
        float S = sqrt(1.0f + m[0][0] - m[1][1] - m[2][2]) * 2.0f;  // S = 4*qx
        return float4(
            0.25f * S,
            (m[0][1] + m[1][0]) / S,
            (m[2][0] + m[0][2]) / S,
            (m[1][2] - m[2][1]) / S);
    } else if (m[1][1] > m[2][2]) {
        float S = sqrt(1.0f + m[1][1] - m[0][0] - m[2][2]) * 2.0f;  // S = 4*qy
        return float4(
            (m[0][1] + m[1][0]) / S,
            0.25f * S,
            (m[1][2] + m[2][1]) / S,
            (m[2][0] - m[0][2]) / S);
    } else {
        float S = sqrt(1.0f + m[2][2] - m[0][0] - m[1][1]) * 2.0f;  // S = 4*qz
        return float4(
            (m[2][0] + m[0][2]) / S,
            (m[1][2] + m[2][1]) / S,
            0.25f * S,
            (m[0][1] - m[1][0]) / S);
    }
}
