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
//  - Color (white), Rotation (identity quat), FX1/FX2 (0): TiXL ColorA/ColorB,
//    the Orientation quat (UsingUpVector/Simple modes + Twist/Axis/Angle) and F1/F2
//    are Vector/enum inputs baked to TiXL-equivalent defaults until vector/enum params
//    land in NodeSpec. AddSeparator/W/WOffset unused (W/WOffset are commented out in
//    the .hlsl too). Flagged in parityNotes.
#include <metal_stdlib>
#include "tixl_point.h"        // SwPoint (64B)
#include "linepoints_params.h" // LineParams, LineBinding
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
  if (tid.x >= P.Count) return;
  uint i = tid.x;

  // AddSeparator baked false: steps = pointCount - 1 (endpoints inclusive).
  int steps = int(P.Count) - 1;
  float t = steps > 0 ? float(i) / float(steps) : 0.5f;
  float f1 = applyGainAndBias(t, float2(P.GainBiasX, P.GainBiasY));
  float f = f1 - P.Pivot;

  float3 center = float3(P.CenterX, P.CenterY, P.CenterZ);
  float3 dir = float3(P.DirectionX, P.DirectionY, P.DirectionZ);
  float3 endPt = center + dir * P.LengthFactor;
  float3 pos = mix(center, endPt, f);  // HLSL lerp(Center, Center+Dir*Len, f)

  SwPoint p;
  p.Position = pos;
  p.FX1 = 0.0f;                                    // TiXL FX1 = F1.x + F1.y*f1; F1 baked 0
  p.Rotation = float4(0.0f, 0.0f, 0.0f, 1.0f);     // identity — orientation quat deferred
  p.Color = float4(1.0f, 1.0f, 1.0f, 1.0f);        // ColorA/ColorB baked white
  p.Scale = float3(P.ScaleBase + P.ScaleByF * f1); // TiXL PointSize.x + PointSize.y*f1
  p.FX2 = 0.0f;                                    // TiXL FX2 = F2.x + F2.y*f1; F2 baked 0
  pts[i] = p;
}
