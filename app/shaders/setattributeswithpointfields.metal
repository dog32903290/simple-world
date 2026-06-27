// setattributeswithpointfields.metal — faithful Metal port of TiXL
// SetPointAttributesWithPointFields.hlsl.
// Source: external/tixl/Operators/Lib/Assets/shaders/points/modify/SetPointAttributesWithPointFields.hlsl
//
// A point MODIFY op with a SECOND Points input: for each SourcePoint (t0 / inputs[0]) it accumulates a
// stylistic "gravity" field from every FieldPoint (t1 / inputs[1]) within range, then offsets position,
// orients toward the field, blends color (via a Gradient baked to GradientImage), and writes W (via a
// Curve baked to CurveImage). VERBATIM math from the .hlsl main() (lines 68-183).
//
// SwPoint mapping (tixl_point.h, 64 bytes): TiXL LegacyPoint.W -> SwPoint.FX1.
//   packed_float3 Position @0 | float FX1 @12 | float4 Rotation @16 |
//   float4 Color @32 | packed_float3 Scale @48 | float FX2 @60
//
// NAMED FORKS:
//   fork[Selected=1]: SwPoint has NO Selected field (selection lives in FX1/FX2 weights in
//     simple_world). TiXL LegacyPoint.Selected multiplies the per-field falloff `f` (:103) and
//     `selectAmount` (:126). We substitute Selected = 1.0 (every point fully selected) — the faithful
//     default for a point bag with no explicit selection mask, and the same value TiXL's generators
//     write into Selected. Identical output to TiXL when all points are selected.
//   fork[uninitialized accumulators]: TiXL declares `float3 totalForce;` and `float totalW;` WITHOUT
//     initializing them (.hlsl:80,84) — HLSL leaves them garbage, but the field loop overwrites/adds
//     before use only when FieldCount>0. We zero-init them (MSL would also be garbage otherwise) so the
//     FieldCount==0 / all-fields-culled path is deterministic (totalForce=0 -> no offset/orient,
//     totalW=0 -> W per WMode). This is the obvious correct intent; TiXL relies on the loop running.
#include <metal_stdlib>
#include "tixl_point.h"                         // SwPoint (64B), packed_float3
#include "setattributeswithpointfields_params.h" // SetAttrWithFieldsParams, SAWF_* bindings
#include "shared/quat.metal.h"                  // qLookAt, qSlerp, QUATERNION_IDENTITY
#include "shared/hash.metal.h"                  // _PRIME0 (used by sawfHash11u below)
using namespace metal;

#define SAWF_COLORMODE_REPLACE_ADD 0
#define SAWF_COLORMODE_REPLACE_AVERAGE 1
#define SAWF_COLORMODE_BLEND 2

#define SAWF_WMODE_SET 0
#define SAWF_WMODE_ADD 1
#define SAWF_WMODE_BLEND 2

// hash11u — uint -> float in [0,1). The canonical TiXL hash11u (hash-functions.hlsl:115-123), restated
// here identically to mappointattributes.metal / clearsomepoints.metal (hash.metal.h exports
// hash41u/hash11/etc. but NOT this single-uint hash). _PRIME0 (13331u) from shared/hash.metal.h.
static float sawfHash11u(uint x) {
    const uint k = 1103515245u;  // GLIBC LCG constant
    x *= _PRIME0;
    x = ((x >> 8u) ^ x) * k;
    x = ((x >> 8u) ^ x) * k;
    return float(x) * (1.0f / float(0xffffffffu));
}

// GetBias / GetSchlickBias / ApplyGainAndBias — VERBATIM port of bias-functions.hlsl (the scalar
// overload, same as velocity_force.metal / snaptogrid.metal). With GainAndBias=(0.365,0.59) this is
// the .t3 default falloff shaping.
static float sawfGetBias(float bias, float x) {
    return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}
static float sawfGetSchlickBias(float g, float x) {
    if (x < 0.5f) { x *= 2.0f; x = 0.5f * sawfGetBias(g, x); }
    else          { x = 2.0f * x - 1.0f; x = 0.5f * sawfGetBias(1.0f - g, x) + 0.5f; }
    return x;
}
static float sawfApplyGainAndBias(float value, float2 gainBias) {
    float g = saturate(gainBias.x);
    float b = saturate(gainBias.y);
    if (value > 0.9999f) return 1.0f;
    if (value < 0.00001f) return 0.0f;
    if (g < 0.5f) { value = sawfGetBias(b, value); value = sawfGetSchlickBias(g, value); }
    else          { value = sawfGetSchlickBias(g, value); value = sawfGetBias(b, value); }
    return value;
}

kernel void setattributeswithpointfields(
    const device SwPoint*               SourcePoints  [[buffer(SAWF_SourcePoints)]],
    const device SwPoint*               FieldPoints   [[buffer(SAWF_FieldPoints)]],
    device SwPoint*                     ResultPoints  [[buffer(SAWF_ResultPoints)]],
    constant SetAttrWithFieldsParams&   P             [[buffer(SAWF_Params)]],
    texture2d<float>                    CurveImage    [[texture(SAWF_CurveImage)]],
    texture2d<float>                    GradientImage [[texture(SAWF_GradientImage)]],
    sampler                             texSampler    [[sampler(SAWF_TexSampler)]],
    uint                                index [[thread_position_in_grid]])
{
    uint pointCount = P.Count;
    if (index >= pointCount) return;

    SwPoint p = SourcePoints[index];

    int ColorMode = (int)P.ColorMode;
    int WMode = (int)P.WMode;
    int WCurveAffectsWeight = (int)P.WCurveAffectsWeight;
    int FieldCount = (int)P.FieldCount;
    float2 GainAndBias = float2(P.GainAndBiasX, P.GainAndBiasY);
    float3 OrientationUpVector = float3(P.OrientationUpVector[0], P.OrientationUpVector[1],
                                        P.OrientationUpVector[2]);

    // p.Selected -> 1.0 (fork[Selected=1]: no Selected field in SwPoint).
    const float pSelected = 1.0f;

    float3 pPos = float3(p.Position.x, p.Position.y, p.Position.z);

    // fork[uninitialized accumulators]: zero-init (see header).
    float3 totalForce = float3(0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;
    float4 totalColor = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float totalW = 0.0f;
    // int usedCount = 0;  // (.hlsl:85 computed but UNUSED in the kernel — dropped, dead)

    float noise = (sawfHash11u(index) - 0.5f) * P.Variation;

    for (int fieldIndex = 0; fieldIndex < FieldCount; fieldIndex++) {
        float w = FieldPoints[fieldIndex].FX1;  // LegacyPoint.W -> SwPoint.FX1
        if (isnan(w) || w < 0.0001f)
            continue;

        float3 fPos = float3(FieldPoints[fieldIndex].Position.x,
                             FieldPoints[fieldIndex].Position.y,
                             FieldPoints[fieldIndex].Position.z);
        float3 dir = (pPos - fPos) / w;
        float len = length(dir);
        // float dd = 1 / (len + 0.1);  // (.hlsl:99 computed but UNUSED — dropped, dead)

        float f = (1.0f - saturate((len - P.OffsetRange) / P.Range)) + noise;
        f = sawfApplyGainAndBias(f, GainAndBias);
        f *= pSelected;

        float fw = CurveImage.sample(texSampler, float2(f, 0.5f), level(0)).r;
        totalW += fw;

        f *= (WCurveAffectsWeight != 0) ? fw : 1.0f;

        float4 color = GradientImage.sample(texSampler, float2(f, 0.5f), level(0));
        float4 fieldColor = FieldPoints[fieldIndex].Color;
        totalColor += fieldColor * color * ((ColorMode == SAWF_COLORMODE_BLEND) ? f : 1.0f);

        totalWeight += f;

        float distanceSq = dot(dir, dir);
        if (distanceSq > 0.0001f) {
            float invDistance = rsqrt(distanceSq);
            float3 force = dir * invDistance * invDistance * invDistance;  // r / |r|^3
            totalForce += force;
        }
    }

    float selectAmount = P.Amount * pSelected;

    float gMagnitude = length(totalForce) + 0.0001f;

    // Offset
    float3 gdir = totalForce / gMagnitude;
    pPos -= gdir * totalWeight * selectAmount * P.AffectPosition;
    p.Position = packed_float3(pPos.x, pPos.y, pPos.z);

    // Orient towards
    float4 lookAtRotation = normalize(qLookAt(-gdir, OrientationUpVector));
    float4 pRot = p.Rotation;
    p.Rotation = qSlerp(pRot, lookAtRotation, totalWeight * selectAmount * P.AffectOrientation);

    // Color
    float colorAffect = selectAmount * P.AffectColor;
    float4 c = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 pColor = p.Color;

    switch (ColorMode) {
    case SAWF_COLORMODE_REPLACE_ADD:
        c = mix(pColor, totalColor, colorAffect);
        break;
    case SAWF_COLORMODE_REPLACE_AVERAGE:
        if (totalWeight > 0.001f)
            totalColor /= totalWeight;
        c = mix(pColor, totalColor, colorAffect);
        break;
    case SAWF_COLORMODE_BLEND:
        if (totalWeight > 0.001f)
            totalColor /= totalWeight;
        c = mix(pColor, totalColor, saturate(totalWeight) * colorAffect);
        break;
    }

    p.Color = float4(max(c.rgb, float3(0.0f)), saturate(c.a));

    // W (LegacyPoint.W -> SwPoint.FX1)
    float wAffect = selectAmount * P.AffectW;
    switch (WMode) {
    case SAWF_WMODE_SET:
        p.FX1 = totalW * wAffect;
        break;
    case SAWF_WMODE_ADD:
        p.FX1 += totalW * wAffect;
        break;
    case SAWF_WMODE_BLEND:
        p.FX1 = mix(p.FX1, totalW, totalWeight * wAffect);
        break;
    }

    ResultPoints[index] = p;
}
