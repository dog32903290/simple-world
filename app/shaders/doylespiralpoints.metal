// doylespiralpoints.metal — faithful Metal port of TiXL DoyleSpiralPoints.hlsl.
// Source: external/tixl/Operators/Lib/Assets/shaders/points/generate/DoyleSpiralPoints.hlsl
//
// TiXL DoyleSpiralPoints.hlsl main() (verbatim, lines 33-73):
//   uint count, stride; ResultPoints.GetDimensions(count, stride);
//   int index = Di.x;
//   int p = (int)(P + 0.5);
//   int steps = (count / p) - 1;
//   int _j = index % steps;
//   int _i = index / steps;
//   float i = _i;
//   float j = _j - Offset;
//   float scale = Scale;
//   float ang = AAng * i + BAng * j;
//   float mag = max(0, pow(pow(AMag, i) * pow(BMag, j), Bias2) * scale + CutOff);
//   float x = cos(ang) * mag;
//   float y = sin(ang) * mag;
//   float radius = mag * R * 100;
//   float3 pos = float3(x, y, 0);
//   pos += Center;
//   ResultPoints[index].Position = pos;
//   ResultPoints[index].W = pow(radius * W + CutOff2, Bias);
//   float4 rot = qFromAngleAxis(OrientationAngle * PI / 180, normalize(OrientationAxis));
//   rot = qMul(rot, qFromAngleAxis(ang, float3(0, 0, 1)));
//   ResultPoints[index].Rotation = rot;
//   ResultPoints[index].Color = 1;
//   ResultPoints[index].Selected = 1;
//
// The A=(AMag,AAng), B=(BMag,BAng), R values are derived on the CPU (cook side) by the
// _DoyleSpiralRoot Newton-Raphson solver and arrive via the cbuffer (see params header).
//
// NAMED FORKS:
//   - steps guarded: TiXL computes `steps = count/p - 1` and uses it as a divisor/modulus.
//     With count==p (single ring) steps==0 -> div-by-zero. We clamp steps>=1 (GPU safety;
//     TiXL relies on the .t3 buffer being sized large; our golden never hits steps<1 but a
//     corrupt .swproj could). This only affects the degenerate count<=p case.
//   - ToRad: TiXL declares `static const float ToRad = 3.141578 / 180;` (a PI typo) but the
//     body actually uses `PI / 180` for the orientation angle. We use PI/180 to match the body
//     verbatim; the typo'd ToRad const is unused in TiXL's main() too.
//   - Color baked white (TiXL: ResultPoints[index].Color = 1). Selected -> n/a in SwPoint.
#include <metal_stdlib>
#include "tixl_point.h"               // SwPoint (64B)
#include "doylespiralpoints_params.h" // DoyleSpiralParams, DoyleSpiralBinding
#include "shared/quat.metal.h"        // qFromAngleAxis, qMul, PI
using namespace metal;

kernel void doylespiralpoints(
    device SwPoint*             pts [[buffer(DOYLE_Points)]],
    constant DoyleSpiralParams& P   [[buffer(DOYLE_Params)]],
    uint3                       Di  [[thread_position_in_grid]])
{
    // TiXL: ResultPoints.GetDimensions(count, stride). On Metal the cook passes the element
    // count explicitly via the cbuffer (Count field). See params header note.
    uint count = (uint)(P.Count + 0.5f);

    int index = (int)Di.x;
    if ((uint)index >= count) return;

    int p = (int)(P.P + 0.5f);
    if (p < 1) p = 1;                 // GPU safety (TiXL clamps P to >=1 upstream)

    int steps = ((int)count / p) - 1;
    if (steps < 1) steps = 1;         // FORK: guard div/mod by zero (degenerate count<=p)

    int _j = index % steps;
    int _i = index / steps;

    float i = (float)_i;
    float j = (float)_j - P.Offset;

    float scale = P.Scale;
    float ang = P.AAng * i + P.BAng * j;
    float mag = max(0.0f,
                    pow(pow(P.AMag, i) * pow(P.BMag, j), P.Bias2) * scale + P.CutOff);
    float x = cos(ang) * mag;
    float y = sin(ang) * mag;
    float radius = mag * P.R * 100.0f;
    float3 pos = float3(x, y, 0.0f);

    float3 center = float3(P.CenterX, P.CenterY, P.CenterZ);
    pos += center;

    float3 axis = float3(P.OrientAxisX, P.OrientAxisY, P.OrientAxisZ);
    float  axLen = length(axis);
    float3 normAxis = (axLen > 1e-6f) ? (axis / axLen) : float3(0.0f, 0.0f, 1.0f);
    float4 rot = qFromAngleAxis(P.OrientAngle * PI / 180.0f, normAxis);
    rot = qMul(rot, qFromAngleAxis(ang, float3(0.0f, 0.0f, 1.0f)));

    SwPoint sp;
    sp.Position = pos;
    sp.FX1      = pow(radius * P.W + P.CutOff2, P.Bias);  // TiXL: .W = pow(radius*W+CutOff2, Bias)
    sp.Rotation = rot;
    sp.Color    = float4(1.0f, 1.0f, 1.0f, 1.0f);          // TiXL: Color = 1
    sp.Scale    = float3(1.0f);                             // TiXL writes no Stretch -> baked 1
    sp.FX2      = 1.0f;                                     // TiXL: .Selected = 1 (== FX2/BirthTime @60)
    pts[index] = sp;
}
