// radial_points — faithful port of external/tixl .../points/generate/RadialPoints.hlsl.
// A generator op for the lane-A point-graph: writes a bag of SwPoints (no input bag).
//
// PARAM-COMPLETION GATE (this file's reason to exist): every TiXL input that the original
// port baked to a hard-coded default — Axis, GainAndBias, CloseCircle, Scale(PointScaleRange),
// F1/F2, Color, Orientation(Axis/Angle/Mode) — is now read from RadialParams (filled by the
// cook from the NodeSpec). The math below is ported line-by-line from RadialPoints.hlsl; the
// only intentional fork is Position's rotation form (Rodrigues vs TiXL's 3x3 matrix — they are
// algebraically identical; --selftest-radialop pins it).
//
// NOTE: replaces a pre-pivot orphan of the same name (32-byte Particle world, never built).
// This version is on the live SwPoint (64B, tixl_point.h) contract.
#include <metal_stdlib>
#include "tixl_point.h"        // SwPoint (64B)
#include "particle_params.h"   // RadialParams, RadialBinding
#include "shared/quat.metal.h" // qFromAngleAxis/qMul/qLookAt/qFromMatrix3Precise
using namespace metal;

// Rodrigues rotation of v around (unit) axis by `angle` rad. == TiXL RotatePointAroundAxis
// (the 3x3 matrix form), algebraically identical; clearer, no row/column transpose trap.
static float3 rotateAroundAxis(float3 v, float3 axis, float angle) {
  axis = normalize(axis);
  float s = sin(angle), c = cos(angle);
  return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0f - c);
}

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

// RadialPoints.hlsl :: GetPosForF — recompute the spiral position for an arbitrary f (already
// gain-biased). Used by the AlignedToCurvature orientation mode to look one step ahead.
static float3 getPosForF(float f, float3 up, constant RadialParams& P) {
  float3 axis = float3(P.AxisX, P.AxisY, P.AxisZ);
  float angle = P.StartAngle * (M_PI_F / 180.0f) + P.Cycles * 2.0f * M_PI_F * f;
  float l = P.Radius + P.RadiusOffset * f;
  float3 direction = normalize(cross(axis, up));
  float3 v2 = rotateAroundAxis(direction * l, axis, angle);
  float3 c = float3(P.CenterX, P.CenterY, P.CenterZ) +
             float3(P.OffsetCenterX, P.OffsetCenterY, P.OffsetCenterZ) * f;
  return v2 + c;
}

kernel void radial_points(device SwPoint*        pts [[buffer(RADIAL_Points)]],
                          constant RadialParams&  P   [[buffer(RADIAL_Params)]],
                          uint3                   tid [[thread_position_in_grid]]) {
  uint pointCount = P.Count;
  if (tid.x >= pointCount) return;
  uint index = tid.x;

  // RadialPoints.hlsl:76-80
  bool closeCircle = P.CloseCircle > 0.5f;
  float angleStepCount = closeCircle ? float(pointCount) - 2.0f : float(pointCount);
  float ff = angleStepCount > 0.0f ? float(index) / angleStepCount : 0.0f;
  float f = applyGainAndBias(ff, float2(P.GainX, P.GainY));

  // RadialPoints.hlsl:82-91 — position
  float3 axis = float3(P.AxisX, P.AxisY, P.AxisZ);
  float angle = P.StartAngle * (M_PI_F / 180.0f) + P.Cycles * 2.0f * M_PI_F * f;
  float3 up = axis.y > 0.7f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
  float l = P.Radius + P.RadiusOffset * f;
  float3 direction = normalize(cross(axis, up));
  float3 v2 = rotateAroundAxis(direction * l, axis, angle);
  float3 c = float3(P.CenterX, P.CenterY, P.CenterZ) +
             float3(P.OffsetCenterX, P.OffsetCenterY, P.OffsetCenterZ) * f;
  float3 v = v2 + c;

  SwPoint p;
  p.Position = v;

  // RadialPoints.hlsl:94-96 — scale (NaN terminator on closed line's last point)
  p.Scale = float3((closeCircle && index == pointCount - 1u)
                       ? NAN
                       : P.ScaleBase + P.ScaleByF * f);

  // RadialPoints.hlsl:98-122 — orientation (two modes)
  if (P.OrientMode < 0.5f) {
    // Classic
    float3 oAxis = float3(P.OrientAxisX, P.OrientAxisY, P.OrientAxisZ);
    float4 spin = qFromAngleAxis(P.OrientAngle / 180.0f * M_PI_F, normalize(oAxis));
    float4 lookat = qLookAt(axis, up);
    float4 spin2 = qFromAngleAxis(angle, float3(axis));
    p.Rotation = qMul(normalize(qMul(spin2, lookat)), spin);
  } else {
    // AlignedToCurvature
    float3 pos2 = getPosForF(f + 0.0001f, up, P);
    float3 vy = normalize(pos2 - v);
    float3 vx = normalize(v - c);
    float3 vz = normalize(cross(vx, vy));
    vx = cross(vy, vz);
    // HLSL float3x3(vx, vy, vz) builds rows; transpose() then feeds qFromMatrix3Precise.
    // MSL float3x3(vx,vy,vz) builds COLUMNS, so the un-transposed MSL matrix already equals
    // HLSL's transpose(rows) — pass it directly (matches the HLSL transpose(meshRotMatrix)).
    float3x3 meshRotMatrix = float3x3(vx, vy, vz);
    float4 rot = qFromMatrix3Precise(meshRotMatrix);
    float4 spin = normalize(qFromAngleAxis(P.OrientAngle / 180.0f * M_PI_F,
                                           normalize(float3(P.OrientAxisX, P.OrientAxisY,
                                                            P.OrientAxisZ))));
    p.Rotation = qMul(rot, spin);
  }

  // RadialPoints.hlsl:124-127
  p.Color = float4(P.ColorR, P.ColorG, P.ColorB, P.ColorA);
  p.FX1 = P.F1Base + P.F1ByF * f;
  p.FX2 = P.F2Base + P.F2ByF * f;

  pts[index] = p;
}
