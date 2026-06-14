// softtransformpoints.metal — faithful Metal port of TiXL's SoftTransformPoints.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/modify/SoftTransformPoints.hlsl
// A count-preserving MODIFIER: computes a volume-falloff weight (Sphere/Box/Plane/Zebra, smoothstep)
// shaped by GainAndBias × Strength × StrengthFactor, then SOFT-applies a Translate/Rotate/Scale
// transform to Position (lerp by the weight), composes Rotation by the X-axis rotation, and lerps
// FX1 by ScaleFx1/OffsetFx1.
//
// TiXL main() (SoftTransformPoints.hlsl:59-121, verbatim logic):
//   posInVolume = mul(float4(p.Position,1), TransformVolume).xyz
//   Sphere(<0.5): s = smoothstep(1+FallOff, 1, length(posInVolume))
//   Box(<1.5):    s = smoothstep(1+FallOff, 1, max3(abs(posInVolume))+Phase)
//   Plane(<2.5):  s = smoothstep(FallOff, 0, posInVolume.y)
//   Zebra(<3.5):  s = smoothstep(Th+0.5+FallOff, Th+0.5, 1-abs(mod(posInVolume.y+Phase,2)-1))
//   s = ApplyGainAndBias(s, GainAndBias)
//   strength = s * Strength * (StrengthFactor==0?1:StrengthFactor==1?p.FX1:p.FX2)
//   volumeCenter = TransformVolume._m30_m31_m32_m03.xyz
//   posInVolume2 = posInObject + volumeCenter
//   rot = RotateAxis * PI/180 * strength
//   rotX/Y/Z = qFromAngleAxis(rot.{x,y,z}, axis)
//   posInVolume2 = qRotate(qRotate(qRotate(posInVolume2, rotX), rotY), rotZ)
//   p.Position = lerp(p.Position, -volumeCenter + posInVolume2*Scale*ScaleMagnitude, strength)
//                + strength*Translate
//   p.Rotation = qMul(p.Rotation, rotX)
//   p.FX1 = lerp(fx1, fx1*ScaleFx1 + OffsetFx1, strength)
//   p.Position.y += Translate.y * strength
//
// NAMED FORK — TransformVolume (= TransformMatrix(Invert=true) of Translate(VolumeCenter)·
//   Scale(VolumeStretch·VolumeSize); SoftTransformPoints.t3 has NO volume rotate) composed in-shader.
//   For the no-shear no-rotate volume, the inverse-map is analytic:
//     posInVolume = (posInObject - VolumeCenter) / (VolumeStretch·VolumeSize).
//   The kernel also extracts volumeCenter = TransformVolume._m30_m31_m32 (the inverse matrix's
//   translation row).  For M = Translate(C)·Scale(Sv) (row-major after TiXL's transpose), the
//   inverse's translation row _m30_m31_m32 = -C / Sv (= the value the HLSL reads).  We reproduce
//   exactly that: volumeCenter = -VolumeCenter / safeSv.  (Verified by the golden: a point at the
//   volume center maps to posInVolume=0 -> fully inside.)
//
// ApplyGainAndBias ported VERBATIM from bias-functions.hlsl (scalar form), same as snaptogrid.metal.
#include <metal_stdlib>
#include "tixl_point.h"                  // SwPoint (64B)
#include "softtransformpoints_params.h"  // SoftTransformParams, SoftTransformBinding
#include "shared/quat.metal.h"           // qFromAngleAxis, qMul, qRotateVec3
using namespace metal;

inline float getBias(float bias, float x) {
    return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}
inline float getSchlickBias(float g, float x) {
    if (x < 0.5f) { x *= 2.0f; x = 0.5f * getBias(g, x); }
    else          { x = 2.0f * x - 1.0f; x = 0.5f * getBias(1.0f - g, x) + 0.5f; }
    return x;
}
inline float applyGainAndBias(float value, float2 gainBias) {
    float g = saturate(gainBias.x);
    float b = saturate(gainBias.y);
    if (value > 0.9999f) return 1.0f;
    if (value < 0.00001f) return 0.0f;
    if (g < 0.5f) { value = getBias(b, value); value = getSchlickBias(g, value); }
    else          { value = getSchlickBias(g, value); value = getBias(b, value); }
    return value;
}

// GLSL floored mod (TiXL `mod`). Used by the Zebra branch.
inline float floorMod1(float x, float y) {
    return (y == 0.0f) ? 0.0f : x - y * floor(x / y);
}

kernel void softtransformpoints(
    device const SwPoint*       SourcePoints [[buffer(SOFTXF_SourcePoints)]],
    device       SwPoint*       ResultPoints [[buffer(SOFTXF_ResultPoints)]],
    constant SoftTransformParams& P          [[buffer(SOFTXF_Params)]],
    uint3 i [[thread_position_in_grid]])
{
    uint idx = i.x;
    if (idx >= P.Count) return;

    SwPoint p = SourcePoints[idx];

    float3 posInObject = p.Position;

    // --- world -> volume inverse-map (FORK: analytic TRS inverse, no volume rotate) ---
    float3 vCenter  = float3(P.VolumeCenterX, P.VolumeCenterY, P.VolumeCenterZ);
    float3 vStretch = float3(P.VolumeStretchX, P.VolumeStretchY, P.VolumeStretchZ) * P.VolumeSize;
    float3 safeSv   = select(float3(1.0f), vStretch, vStretch != float3(0.0f));
    float3 posInVolume = (posInObject - vCenter) / safeSv;

    float s = 1.0f;
    // VolumeShape compared against float thresholds 0.5/1.5/2.5/3.5 in TiXL; our int maps:
    // 0->Sphere(<0.5), 1->Box(<1.5), 2->Plane(<2.5), 3->Zebra(<3.5).
    float vshape = (float)P.VolumeShape + 0.0f;
    if (vshape < 0.5f) {            // Sphere
        float distance = length(posInVolume);
        s = smoothstep(1.0f + P.FallOff, 1.0f, distance);
    } else if (vshape < 1.5f) {     // Box
        float3 t = abs(posInVolume);
        float distance = max(max(t.x, t.y), t.z) + P.Phase;
        s = smoothstep(1.0f + P.FallOff, 1.0f, distance);
    } else if (vshape < 2.5f) {     // Plane
        float distance = posInVolume.y;
        s = smoothstep(P.FallOff, 0.0f, distance);
    } else {                        // Zebra (<3.5)
        float distance = 1.0f - abs(floorMod1(posInVolume.y * 1.0f + P.Phase, 2.0f) - 1.0f);
        s = smoothstep(P.Threshold + 0.5f + P.FallOff, P.Threshold + 0.5f, distance);
    }

    s = applyGainAndBias(s, float2(P.GainAndBiasX, P.GainAndBiasY));

    float strength = s * P.Strength * (P.StrengthFactor == 0 ? 1.0f
                                    : (P.StrengthFactor == 1) ? p.FX1
                                                              : p.FX2);

    // volumeCenter = inverse-matrix translation row _m30_m31_m32 = -VolumeCenter / safeSv (FORK).
    float3 volumeCenter = -vCenter / safeSv;
    float3 posInVolume2 = posInObject + volumeCenter;

    float3 rot = float3(P.RotateAxisX, P.RotateAxisY, P.RotateAxisZ) * (M_PI_F / 180.0f) * strength;

    float4 rotationX = qFromAngleAxis(rot.x, float3(1, 0, 0));
    float4 rotationY = qFromAngleAxis(rot.y, float3(0, 1, 0));
    float4 rotationZ = qFromAngleAxis(rot.z, float3(0, 0, 1));

    posInVolume2 = qRotateVec3(posInVolume2, rotationX);
    posInVolume2 = qRotateVec3(posInVolume2, rotationY);
    posInVolume2 = qRotateVec3(posInVolume2, rotationZ);

    float3 scale = float3(P.ScaleX, P.ScaleY, P.ScaleZ) * P.ScaleMagnitude;
    float3 translate = float3(P.TranslateX, P.TranslateY, P.TranslateZ);

    p.Position = mix(p.Position, -volumeCenter + posInVolume2 * scale, strength) + strength * translate;
    p.Rotation = qMul(p.Rotation, rotationX);

    float fx1 = SourcePoints[idx].FX1;
    p.FX1 = mix(fx1, fx1 * P.ScaleFx1 + P.OffsetFx1, strength);

    p.Position.y += translate.y * strength;
    ResultPoints[idx] = p;
}
