// simforceoffset.metal — faithful Metal port of TiXL's SimForceOffset.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/sim/SimForceOffset.hlsl
// A count-preserving MODIFIER: applies (Gravity + radialForce)*effect to Position, where the
// radial force points away from Center and decays by pow(distance, ForceDecayRate), gated by a
// radius/falloff window `effect`. Writes ResultPoints (same count).
//
// TiXL parity (SimForceOffset.hlsl):
//   float3 variationOffset = hash31((float)(i.x%1234)/0.123) * Variation;   // computed, see fork
//   float3 pos = ResultPoints[i.x].Position;
//   float3 localPos = pos - Center;
//   float distance = max(length(localPos), 0.02);
//   float3 direction = localPos / distance;
//   float effect = saturate(1-(distance - Radius) / RadiusFallOff)/100;
//   float3 radialForce = direction / clamp(pow(distance, ForceDecayRate), 0.02, 1000) * RadialForce;
//   ResultPoints[i.x].Position += (Gravity + radialForce) * effect;
//   ResultPoints[i.x].W += 0;     // dead code in TiXL -> omitted
//
// Notes:
//   - RadiusFallOff=0 => (distance-Radius)/0 = +/-inf; 1-inf = -inf; saturate(-inf)=0 => effect=0
//     (no force). This is TiXL behavior; an effect requires RadiusFallOff>0. MSL float div-by-zero
//     yields inf and saturate(clamp 0..1) gives 0, matching HLSL.
//
// Forks from TiXL (named):
//   1. In-place RWStructuredBuffer<LegacyPoint> -> sw source+dest (const + writable).
//   2. `ResultPoints[i.x].W += 0;` dead code omitted.
//   3. variationOffset is computed in the .hlsl but never used in the position update; we keep the
//      computation (faithful) but it has no effect (TiXL WIP artifact). Marked (void).
//   4. .cs IsEnabled / UseWForMass slots: IsEnabled omitted; UseWForMass present in cbuffer but
//      unused by the .hlsl math (kept for layout parity).
#include <metal_stdlib>
#include "tixl_point.h"               // SwPoint (64B layout)
#include "simforceoffset_params.h"    // SimForceOffsetParams, SimForceOffsetBinding
#include "shared/hash.metal.h"        // hash31(float)
using namespace metal;

kernel void simforceoffset(
    device const SwPoint* SourcePoints   [[buffer(SIMFORCEOFFSET_SourcePoints)]],
    device       SwPoint* ResultPoints   [[buffer(SIMFORCEOFFSET_ResultPoints)]],
    constant SimForceOffsetParams& P     [[buffer(SIMFORCEOFFSET_Params)]],
    uint3 i [[thread_position_in_grid]])
{
    uint idx = i.x;
    if (idx >= P.Count) return;

    SwPoint p = SourcePoints[idx];

    float3 Center  = float3(P.CenterX, P.CenterY, P.CenterZ);
    float3 Gravity = float3(P.GravityX, P.GravityY, P.GravityZ);

    // Fork #3: variationOffset computed (faithful) but unused in TiXL's position update.
    float3 variationOffset = hash31((float)(idx % 1234u) / 0.123f) * P.Variation;
    (void)variationOffset;

    float3 pos = p.Position;
    float3 localPos = pos - Center;
    float distance = max(length(localPos), 0.02f);
    float3 direction = localPos / distance;

    float effect = saturate(1.0f - (distance - P.Radius) / P.RadiusFallOff) / 100.0f;

    float3 radialForce =
        direction / clamp(pow(distance, P.ForceDecayRate), 0.02f, 1000.0f) * P.RadialForce;

    p.Position += (Gravity + radialForce) * effect;

    ResultPoints[idx] = p;
}
