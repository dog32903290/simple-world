// simcentrialoffset.metal — faithful Metal port of TiXL's SimCentricalOffset.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/sim/SimCentricalOffset.hlsl
// A count-preserving MODIFIER: per-point, computes the radial vector d = Position - Center and
// offsets Position along d by a clamped inverse-power force. Writes ResultPoints (same count).
//
// TiXL parity (SimCentricalOffset.hlsl):
//   float3 d = pos - Center;
//   float distance = length(d);
//   if (distance < 0.001) return;            // (sw: pass point through unchanged)
//   float3 dNormalized = d / distance;
//   float force = clamp(Acceleration / pow(distance, DecayExponent), -MaxAcceleration, MaxAcceleration);
//   float3 offset = normalize(dNormalized) * force;
//   Points[i.x].Position += offset;
//
// Forks from TiXL (named):
//   1. In-place RWStructuredBuffer<Point> -> sw source+dest (const SourcePoints + writable
//      ResultPoints). The HLSL early-return for distance<0.001 becomes "write the source point
//      unchanged" so the dest bag is fully populated; math otherwise identical.
//   2. .cs ShowGizmo slot omitted (sw has no gizmo).
#include <metal_stdlib>
#include "tixl_point.h"                  // SwPoint (64B layout)
#include "simcentrialoffset_params.h"    // SimCentricalOffsetParams, SimCentricalOffsetBinding
using namespace metal;

kernel void simcentrialoffset(
    device const SwPoint* SourcePoints     [[buffer(SIMCENTRICALOFFSET_SourcePoints)]],
    device       SwPoint* ResultPoints     [[buffer(SIMCENTRICALOFFSET_ResultPoints)]],
    constant SimCentricalOffsetParams& P   [[buffer(SIMCENTRICALOFFSET_Params)]],
    uint3 i [[thread_position_in_grid]])
{
    uint idx = i.x;
    if (idx >= P.Count) return;

    SwPoint p = SourcePoints[idx];

    float3 Center = float3(P.CenterX, P.CenterY, P.CenterZ);
    float3 d = p.Position - Center;
    float distance = length(d);

    if (distance >= 0.001f) {
        float3 dNormalized = d / distance;
        float force = clamp(P.Acceleration / pow(distance, P.DecayExponent),
                            -P.MaxAcceleration, P.MaxAcceleration);
        float3 offset = normalize(dNormalized) * force;
        p.Position += offset;
    }
    // distance < 0.001: TiXL returns early (no displacement); sw writes the point unchanged.

    ResultPoints[idx] = p;
}
