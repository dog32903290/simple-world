// pointattributefromnoise.metal — faithful Metal port of TiXL's PointAttributesFromNoise.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/modify/PointAttributesFromNoise.hlsl
//   .cs: external/tixl/Operators/Lib/point/modify/PointAttributeFromNoise.cs
// A count-preserving MODIFIER: samples a 3D simplex-noise field per point and routes it into the
// chosen point attributes (position X/Y/Z/W or rotation X/Y/Z) through 4 channels (L=Brightness,
// R, G, B), each with its own attribute target + Factor + Offset. Writes ResultPoints (same count).
//
// TiXL parity (PointAttributesFromNoise.hlsl):
//   - Factors[] table (lines 6-15): index 0..5. enum For_X/Y/Z/W = 1/2/3/4 route into position
//     xyzw; Rotate_X/Y/Z = 5/6/7 do NOT contribute to ff (Factors[5] = 0; clamp(idx,0,5.1) caps
//     the index at 5) — they drive the separate rotation accumulators instead. Verbatim.
//   - GetNoise (lines 65-69): noiseLookup = (pos*0.91 + variation + Phase) * Frequency; return
//     snoiseVec3(noiseLookup). snoiseVec3 lives in shared/noise.metal.h.
//   - variationOffset = hash31((i.x%1234)/0.123) * Variation (line 90). hash31 in shared/hash.metal.h.
//   - c = GetNoise(P.Position + Center, variationOffset) (line 91 — note: '+ Center', NOT the dead
//     'pos -= Center' on lines 85-86 which the kernel never uses).
//   - rotation order X -> Y -> Z (lines 134-136): rot = qMul(rot, qFromAngleAxis(angle, axis)).
//
// FORK (named): TiXL's RemapNoise(Gradient) + UseRemapCurve + remapCurveTexture branch (lines
//   92-98) is NOT wired (batch-24 work order). UseRemapCurve is baked false -> the kernel always
//   takes the else branch `c *= Amount/100` (line 100). All other math verbatim. See params.h.
//
// MSL port notes: LegacyPoint -> SwPoint (64B, tixl_point.h); P.Position is packed_float3, copied
//   into a local float3. HLSL frac()->fract() (only inside hash31). cbuffers b0(Transforms,unused)
//   /b1(Params) flattened into one PointAttributeFromNoiseParams struct.
#include <metal_stdlib>
#include "tixl_point.h"                       // SwPoint (64B layout)
#include "pointattributefromnoise_params.h"   // params + binding
#include "shared/hash.metal.h"                // hash31
#include "shared/noise.metal.h"               // snoiseVec3
#include "shared/quat.metal.h"                // qMul, qFromAngleAxis
using namespace metal;

// Factors[] — verbatim from PointAttributesFromNoise.hlsl lines 6-15 (6 entries, 0..5).
constant float4 kFactors[6] = {
    float4(0, 0, 0, 0),   // 0 NotUsed
    float4(1, 0, 0, 0),   // 1 For_X
    float4(0, 1, 0, 0),   // 2 For_Y
    float4(0, 0, 1, 0),   // 3 For_Z
    float4(0, 0, 0, 1),   // 4 For_W
    float4(0, 0, 0, 0),   // 5 avoid rotation effects (Rotate_* clamp here)
};

// GetNoise — direct port of .hlsl lines 65-69.
inline float3 GetNoise(float3 pos, float3 variation,
                       float Phase, float Frequency) {
    float3 noiseLookup = (pos * 0.91f + variation + Phase) * Frequency;
    return snoiseVec3(noiseLookup);
}

kernel void pointattributefromnoise(
    device const SwPoint* SourcePoints              [[buffer(POINTATTRNOISE_SourcePoints)]],
    device       SwPoint* ResultPoints              [[buffer(POINTATTRNOISE_ResultPoints)]],
    constant PointAttributeFromNoiseParams& P       [[buffer(POINTATTRNOISE_Params)]],
    uint3 i [[thread_position_in_grid]])
{
    uint index = i.x;
    if (index >= P.Count) return;

    SwPoint pt = SourcePoints[index];
    float3 center = float3(P.CenterX, P.CenterY, P.CenterZ);

    // .hlsl line 90: per-point variation offset
    float3 variationOffset = hash31((float)(index % 1234) / 0.123f) * P.Variation;
    // .hlsl line 91: sample noise at (Position + Center). (pos -= Center on 85-86 is dead code.)
    float3 c = GetNoise(float3(pt.Position) + center, variationOffset, P.Phase, P.Frequency);

    // FORK: UseRemapCurve baked false -> always the else branch (.hlsl line 100).
    c *= P.Amount / 100.0f;

    float gray = (c.r + c.g + c.b) / 3.0f;

    // .hlsl lines 106-110: route channels into position xyzw via the Factors[] table.
    uint iL = (uint)clamp(P.L, 0.0f, 5.1f);
    uint iR = (uint)clamp(P.R, 0.0f, 5.1f);
    uint iG = (uint)clamp(P.G, 0.0f, 5.1f);
    uint iB = (uint)clamp(P.B, 0.0f, 5.1f);
    float4 ff =
          kFactors[iL] * (gray * P.LFactor + P.LOffset)
        + kFactors[iR] * (c.r  * P.RFactor + P.ROffset)
        + kFactors[iG] * (c.g  * P.GFactor + P.GOffset)
        + kFactors[iB] * (c.b  * P.BFactor + P.BOffset);

    // .hlsl lines 112-113
    pt.Position = float3(pt.Position) + ff.xyz;
    pt.FX1 = clamp(pt.FX1 + ff.w, 0.0f, 10000.0f);   // SwPoint.FX1 == LegacyPoint.W

    // .hlsl lines 116-137: rotation accumulators (attribute == Rotate_X/Y/Z = 5/6/7).
    float4 rot = pt.Rotation;
    float rotXFactor = (P.R == 5 ? (c.r  * P.RFactor + P.ROffset) : 0)
                     + (P.G == 5 ? (c.g  * P.GFactor + P.GOffset) : 0)
                     + (P.B == 5 ? (c.b  * P.BFactor + P.BOffset) : 0)
                     + (P.L == 5 ? (gray * P.LFactor + P.LOffset) : 0);
    float rotYFactor = (P.R == 6 ? (c.r  * P.RFactor + P.ROffset) : 0)
                     + (P.G == 6 ? (c.g  * P.GFactor + P.GOffset) : 0)
                     + (P.B == 6 ? (c.b  * P.BFactor + P.BOffset) : 0)
                     + (P.L == 6 ? (gray * P.LFactor + P.LOffset) : 0);
    float rotZFactor = (P.R == 7 ? (c.r  * P.RFactor + P.ROffset) : 0)
                     + (P.G == 7 ? (c.g  * P.GFactor + P.GOffset) : 0)
                     + (P.B == 7 ? (c.b  * P.BFactor + P.BOffset) : 0)
                     + (P.L == 7 ? (gray * P.LFactor + P.LOffset) : 0);

    if (rotXFactor != 0) { rot = qMul(rot, qFromAngleAxis(rotXFactor, float3(1, 0, 0))); }
    if (rotYFactor != 0) { rot = qMul(rot, qFromAngleAxis(rotYFactor, float3(0, 1, 0))); }
    if (rotZFactor != 0) { rot = qMul(rot, qFromAngleAxis(rotZFactor, float3(0, 0, 1))); }
    pt.Rotation = normalize(rot);

    ResultPoints[index] = pt;
}
