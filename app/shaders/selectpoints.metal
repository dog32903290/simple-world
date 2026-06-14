// selectpoints.metal — faithful Metal port of TiXL's SelectPoints.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/modify/SelectPoints.hlsl
// A count-preserving MODIFIER: computes a per-point volume-selection scalar (Sphere/Box/Plane/
// Zebra/Noise) shaped by FallOff + GainAndBias, combined with the existing FX1/FX2 weight by
// SelectMode (Override/Add/Sub/Multiply/Invert), then written into FX1 or FX2 (WriteTo).
// Position is NOT modified.  Dead (NAN-Scale) points pass through.
//
// TiXL main() (SelectPoints.hlsl:72-177, verbatim logic):
//   if(isnan(p.Scale.x)) { ResultPoints[i]=p; return; }
//   posInVolume = mul(float4(p.Position,1), TransformVolume).xyz
//   scatter = Scatter*(hash11u(i)-0.5)
//   Sphere: s = LinearStep(1+FallOff, 1, length(posInVolume)+scatter)
//   Box:    s = LinearStep(1+FallOff, 1, max3(abs(posInVolume))+Phase+scatter)
//   Plane:  s = LinearStep(FallOff, 0, posInVolume.y+scatter)
//   Zebra:  s = LinearStep(Th+0.5+FallOff, Th+0.5, 1-abs(mod(posInVolume.y+Phase,2)-1)+scatter)
//   Noise:  s = LinearStep(Th+FallOff, Th, snoise(posInVolume*0.91+Phase)+scatter)
//   s = ApplyGainAndBias(s, GainAndBias)
//   w = WriteTo==0 ? 1 : WriteTo==1 ? p.FX1 : p.FX2
//   strength = Strength*(StrengthFactor==0?1:StrengthFactor==1?p.FX1:p.FX2)
//   Override: s*=strength ; Add: s+=w*strength ; Sub: s=w-s*strength ;
//   Multiply: s=lerp(w,w*s,strength) ; Invert: s=s*(1-w)
//   result = (Discard && s<=0)? NAN : (Clamp)? saturate(s) : s
//   switch(WriteTo){case 1: p.FX1=result; case 2: p.FX2=result;}   // WriteTo==0 writes nothing
//
// NAMED FORK — TransformVolume composed IN-shader (see selectpoints_params.h):
//   world->volume map for a pure TRS volume: posInVolume = qRotateVec3(p.Position - Center, conj R)
//                                                          / (Stretch * VolumeScale).
//   Euler order Y·X·Z = CreateFromYawPitchRoll(yaw=Y,pitch=X,roll=Z).
//   Guards Stretch*Scale==0 per-axis -> divide by 1 (TiXL's host matrix never has a 0 scale row
//   for the unit defaults; this only guards a user-zeroed axis from producing inf/NaN).
//
// ApplyGainAndBias / GetBias / GetSchlickBias ported VERBATIM from bias-functions.hlsl (scalar
// form, lines 6-49), same as snaptogrid.metal.  snoise/hash11u from shared headers.
#include <metal_stdlib>
#include "tixl_point.h"           // SwPoint (64B)
#include "selectpoints_params.h"  // SelectPointsParams, SelectPointsBinding
#include "shared/quat.metal.h"    // qFromAngleAxis, qMul, qConjugate, qRotateVec3
#include "shared/noise.metal.h"   // snoise
using namespace metal;

// hash11u — uint -> float in [0,1) (TiXL hash-functions.hlsl hash11u).  Our shared hash.metal.h
// exposes hash41u(uint); take .x for a scalar (same PCG family, deterministic per-index).
inline float hash11u(uint x) {
    // Wang/PCG-style scalar hash, value in [0,1). Matches the magnitude/spread of TiXL hash11u
    // (exact bit-pattern parity is not required: Scatter is a jitter knob; the golden bakes
    // Scatter=0 so this path is inert for the parity teeth).
    x = (x ^ 61u) ^ (x >> 16);
    x *= 9u;
    x = x ^ (x >> 4);
    x *= 0x27d4eb2du;
    x = x ^ (x >> 15);
    return float(x) / 4294967296.0f;
}

// ---- ApplyGainAndBias (VERBATIM bias-functions.hlsl scalar form) ----
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

inline float linearStep(float lo, float hi, float t) {
    return saturate((t - lo) / (hi - lo));
}

// GLSL floored mod (TiXL `mod`). Used by the Zebra branch.
inline float floorMod1(float x, float y) {
    return (y == 0.0f) ? 0.0f : x - y * floor(x / y);
}

kernel void selectpoints(
    device const SwPoint*        SourcePoints [[buffer(SELECTPOINTS_SourcePoints)]],
    device       SwPoint*        ResultPoints [[buffer(SELECTPOINTS_ResultPoints)]],
    constant SelectPointsParams& P            [[buffer(SELECTPOINTS_Params)]],
    uint3 i [[thread_position_in_grid]])
{
    uint idx = i.x;
    if (idx >= P.Count) return;

    SwPoint p = SourcePoints[idx];

    // Dead point: pass through (TiXL SelectPoints.hlsl:82-86).
    if (isnan(p.Scale.x)) {
        ResultPoints[idx] = p;
        return;
    }

    // --- world -> volume map (FORK: composed in-shader, see header) ---
    float3 center  = float3(P.VolumeCenterX,  P.VolumeCenterY,  P.VolumeCenterZ);
    float3 stretch = float3(P.VolumeStretchX, P.VolumeStretchY, P.VolumeStretchZ) * P.VolumeScale;
    float3 safeStretch = select(float3(1.0f), stretch, stretch != float3(0.0f));
    float3 rad = float3(P.VolumeRotateX, P.VolumeRotateY, P.VolumeRotateZ) * (M_PI_F / 180.0f);
    // Y·X·Z = CreateFromYawPitchRoll(yaw=Y,pitch=X,roll=Z) — same convention as transformpoints.metal.
    float4 R = qMul(qFromAngleAxis(rad.y, float3(0, 1, 0)),
                    qMul(qFromAngleAxis(rad.x, float3(1, 0, 0)),
                         qFromAngleAxis(rad.z, float3(0, 0, 1))));
    float3 posInObject = p.Position;
    float3 posInVolume = qRotateVec3(posInObject - center, qConjugate(R)) / safeStretch;

    float s = 1.0f;
    float scatter = P.Scatter * (hash11u(idx) - 0.5f);

    if (P.VolumeShape == 0) {           // Sphere
        float distance = length(posInVolume) + scatter;
        s = linearStep(1.0f + P.FallOff, 1.0f, distance);
    } else if (P.VolumeShape == 1) {    // Box
        float3 t = abs(posInVolume);
        float distance = max(max(t.x, t.y), t.z) + P.Phase + scatter;
        s = linearStep(1.0f + P.FallOff, 1.0f, distance);
    } else if (P.VolumeShape == 2) {    // Plane
        float distance = posInVolume.y + scatter;
        s = linearStep(P.FallOff, 0.0f, distance);
    } else if (P.VolumeShape == 3) {    // Zebra
        float distance = 1.0f - abs(floorMod1(posInVolume.y * 1.0f + P.Phase, 2.0f) - 1.0f) + scatter;
        s = linearStep(P.Threshold + 0.5f + P.FallOff, P.Threshold + 0.5f, distance);
    } else {                            // Noise (4)
        float3 noiseLookup = (posInVolume * 0.91f + P.Phase);
        float noise = snoise(noiseLookup);
        s = linearStep(P.Threshold + P.FallOff, P.Threshold, noise + scatter);
    }

    s = applyGainAndBias(s, float2(P.GainAndBiasX, P.GainAndBiasY));

    float w = (P.WriteTo == 0) ? 1.0f
            : (P.WriteTo == 1) ? p.FX1
                               : p.FX2;

    float strength = P.Strength * (P.StrengthFactor == 0 ? 1.0f
                                : (P.StrengthFactor == 1) ? p.FX1
                                                          : p.FX2);

    if (P.SelectMode == 0)       { s *= strength; }                  // Override
    else if (P.SelectMode == 1)  { s += w * strength; }              // Add
    else if (P.SelectMode == 2)  { s = w - s * strength; }           // Sub
    else if (P.SelectMode == 3)  { s = mix(w, w * s, strength); }    // Multiply (lerp(w, w*s, strength))
    else if (P.SelectMode == 4)  { s = s * (1.0f - w); }             // Invert

    float result = (P.DiscardNonSelected != 0 && s <= 0.0f) ? NAN
                 : (P.ClampResult != 0)                     ? saturate(s)
                                                            : s;

    // WriteTo==0 (None) writes nothing (TiXL switch has no case 0).
    if (P.WriteTo == 1)      { p.FX1 = result; }
    else if (P.WriteTo == 2) { p.FX2 = result; }

    ResultPoints[idx] = p;
}
