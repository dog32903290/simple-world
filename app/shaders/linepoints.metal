// linepoints — faithful port of external/tixl .../points/generate/LinePoints.hlsl
// (POSITION + SCALE). A generator op for the lane-A point-graph: writes a bag of
// SwPoints (no input bag).
//
// TiXL parity (LinePoints.hlsl):
//  - Position is faithful (line 62-66):
//      steps = pointCount - 1                       (AddSeparator baked false -> no -1)
//      f1    = ApplyGainAndBias(steps>0 ? i/steps : 0.5, GainAndBias)
//      f     = f1 - Pivot
//      Pos   = lerp(Center, Center + Direction*LengthFactor, f)
//    NOTE the divisor is steps = Count-1 (endpoints inclusive), NOT Count — the
//    last point lands exactly at Center + Direction*Length (before pivot).
//  - Scale is faithful (line 96): PointSize.x + PointSize.y * f1.
//  - ApplyGainAndBias + GetBias + GetSchlickBias ported 1:1 from shared/bias-functions.hlsl.
//  - PARAM-COMPLETION GATE: Color = lerp(ColorA,ColorB,f1), FX1/FX2 = Fn.x + Fn.y·f1, and the
//    full orientation quaternion (UsingUpVector qLookAt mode / Simple qFromAngleAxis mode +
//    Twist·f) are now READ from LineParams (filled by the cook from the NodeSpec) instead of
//    baked white/0/identity. AddSeparator gives the last point a NaN scale (line terminator)
//    and shrinks the step span by 1. W/WOffset stay unused (commented out in the .hlsl too).
//    Math ported line-by-line from LinePoints.hlsl.
#include <metal_stdlib>
#include "tixl_point.h"        // SwPoint (64B)
#include "linepoints_params.h" // LineParams, LineBinding
#include "shared/quat.metal.h" // qFromAngleAxis / qMul / qLookAt (UsingUpVector + Simple modes)
using namespace metal;

// shared/bias-functions.hlsl :: GetBias (scalar)
static float getBias(float bias, float x) {
  return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f);
}

// shared/bias-functions.hlsl :: GetSchlickBias (scalar)
static float getSchlickBias(float g, float x) {
  if (x < 0.5f) {
    x *= 2.0f;
    x = 0.5f * getBias(g, x);
  } else {
    x = 2.0f * x - 1.0f;
    x = 0.5f * getBias(1.0f - g, x) + 0.5f;
  }
  return x;
}

// shared/bias-functions.hlsl :: ApplyGainAndBias (scalar)
static float applyGainAndBias(float value, float2 gainBias) {
  float g = saturate(gainBias.x);
  float b = saturate(gainBias.y);
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) {
    value = getBias(b, value);
    value = getSchlickBias(g, value);
  } else {
    value = getSchlickBias(g, value);
    value = getBias(b, value);
  }
  return value;
}

kernel void linepoints(device SwPoint*       pts [[buffer(LINE_Points)]],
                       constant LineParams&   P   [[buffer(LINE_Params)]],
                       uint3                  tid [[thread_position_in_grid]]) {
  uint pointCount = P.Count;
  if (tid.x >= pointCount) return;
  uint i = tid.x;

  // HLSL: seperatorOffset = AddSeparator ? 1 : 0; steps = (pointCount - 1 - seperatorOffset).
  int seperatorOffset = P.AddSeparator ? 1 : 0;
  int steps = int(pointCount) - 1 - seperatorOffset;
  float t = steps > 0 ? float(i) / float(steps) : 0.5f;
  float f1 = applyGainAndBias(t, float2(P.GainBiasX, P.GainBiasY));
  float f = f1 - P.Pivot;

  float3 center = float3(P.CenterX, P.CenterY, P.CenterZ);
  float3 dir = float3(P.DirectionX, P.DirectionY, P.DirectionZ);
  float3 endPt = center + dir * P.LengthFactor;
  float3 pos = mix(center, endPt, f);  // HLSL lerp(Center, Center+Dir*Len, f)

  // Orientation (LinePoints.hlsl): note the .hlsl uses the literal 3.141578 (NOT a precise PI),
  // mirrored here for byte-faithful parity.
  const float kPiLike = 3.141578f;
  float4 rot2 = float4(0.0f);
  if (P.OrientationMode < 1u) {  // UsingUpVector (mode 0): qMul(rotatePrefix, qLookAt(dir, up))
    float4 rotate = qFromAngleAxis(kPiLike / 2.0f * 1.0f, float3(0.0f, 0.0f, 1.0f));
    rotate = qMul(rotate, qFromAngleAxis((P.OrientationAngle + P.Twist * f) / 180.0f * kPiLike,
                                         float3(0.0f, 1.0f, 0.0f)));
    float3 upVector = float3(0.0f, 0.0f, 1.0f);
    float td = abs(dot(normalize(dir), normalize(upVector)));
    if (td > 0.999f) upVector = float3(0.0f, 1.0f, 0.0f);
    float4 lookAt = qLookAt(normalize(dir), upVector);
    rot2 = normalize(qMul(rotate, lookAt));
  } else {  // Simple (mode 1): qFromAngleAxis around ManualOrientationAxis
    rot2 = normalize(qFromAngleAxis((P.OrientationAngle + P.Twist * f) / 180.0f * kPiLike,
                                    float3(P.OrientAxisX, P.OrientAxisY, P.OrientAxisZ)));
  }

  SwPoint p;
  p.Position = pos;
  // HLSL: Scale = (AddSeparator && index==last) ? sqrt(-1) : (PointSize.x + PointSize.y*f1).
  float scaleV = (P.AddSeparator && i == pointCount - 1u) ? sqrt(-1.0f)
                                                          : (P.ScaleBase + P.ScaleByF * f1);
  p.Scale = float3(scaleV);
  p.Rotation = rot2;
  p.Color = mix(float4(P.ColorAR, P.ColorAG, P.ColorAB, P.ColorAA),
                float4(P.ColorBR, P.ColorBG, P.ColorBB, P.ColorBA), f1);  // lerp(ColorA,ColorB,f1)
  p.FX1 = P.FX1Base + P.FX1ByF * f1;  // TiXL FX1 = F1.x + F1.y*f1
  p.FX2 = P.FX2Base + P.FX2ByF * f1;  // TiXL FX2 = F2.x + F2.y*f1
  pts[i] = p;
}
