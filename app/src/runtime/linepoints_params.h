// LinePoints (point-operator graph, lane A) — faithful port of TiXL
// .../points/generate/LinePoints.hlsl POSITION + SCALE math (a line of Count points
// from Center along Direction over Length, with Pivot + GainAndBias distribution).
//
// All-scalar params layout (NO packed_float3) so there are zero cbuffer alignment
// traps — TiXL's Vector3 Center/Direction and Vector2 GainAndBias/PointSize are laid
// out as X/Y/Z(/W) scalars; the shader reassembles float3/float2. Mirrors the
// RadialParams discipline in particle_params.h. Padded to a 16-byte multiple +
// static_assert.
//
// PARAM-COMPLETION GATE: the full TiXL [Input] set is now wired. ColorA/ColorB (Color =
// lerp(ColorA,ColorB,f1)), the two Orientation modes (UsingUpVector qLookAt / Simple
// qFromAngleAxis) + OrientationAxis/Angle/Twist, F1/F2 (FX1/FX2 = x + y·f1), AddSeparator
// (last point NaN-scale terminator + step span shrink) are real struct members the cook
// fills from the NodeSpec and the kernel reads. W/WOffset stay UNUSED — they are commented
// out in the .hlsl too (no cbuffer field, no ResultPoints[i].W write): faithful-dead.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

// cbuffer Params : register(b0) — mirror of LinePoints.hlsl's Params, scalarized.
struct LineParams {
#ifdef __METAL_VERSION__
  uint Count;
#else
  uint32_t Count;
#endif
  float LengthFactor;   // TiXL Length — factor multiplied onto Direction
  float Pivot;          // TiXL Pivot (0 start / 0.5 centered / 1 end at Center)
  float CenterX;        // TiXL Center (Vector3) — line start, before pivot/length
  float CenterY;        //   (all-scalar so no packed_float3 alignment trap; shader
  float CenterZ;        //   reassembles float3(CenterX,Y,Z))
  float DirectionX;     // TiXL Direction (Vector3) — line orientation (need not be unit;
  float DirectionY;     //   Length scales it, exactly like the .hlsl lerp endpoint)
  float DirectionZ;
  float GainBiasX;      // TiXL GainAndBias.x (gain)  — distribution along the line
  float GainBiasY;      // TiXL GainAndBias.y (bias)
  float ScaleBase;      // TiXL Scale/PointSize.x — base point scale
  float ScaleByF;       // TiXL Scale/PointSize.y — scale * normalized index (f1)
  // ---- param-completion fan-out: per-point attribute inputs (were baked) -----------------
  float FX1Base;        // TiXL F1.x — FX1 = F1.x + F1.y·f1.  .t3 default (1,0)
  float FX1ByF;         // TiXL F1.y
  float FX2Base;        // TiXL F2.x — FX2 = F2.x + F2.y·f1.  .t3 default (1,0)
  float FX2ByF;         // TiXL F2.y
  float ColorAR;        // TiXL ColorA (Vector4) — Color = lerp(ColorA,ColorB,f1). .t3 white
  float ColorAG;
  float ColorAB;
  float ColorAA;
  float ColorBR;        // TiXL ColorB (Vector4). .t3 white
  float ColorBG;
  float ColorBB;
  float ColorBA;
  float Twist;          // TiXL Twist (degrees) — angle += Twist·f.  .t3 default 0
  float OrientationAngle;  // TiXL OrientationAngle (degrees).  .t3 default 0
  float OrientAxisX;    // TiXL OrientationAxis (Vector3, Simple mode). .t3 default (0,0,1)
  float OrientAxisY;
  float OrientAxisZ;
#ifdef __METAL_VERSION__
  uint AddSeparator;    // TiXL AddSeparator (bool→int): last point NaN-scale + steps-1.  .t3 false
  uint OrientationMode; // TiXL Orientation enum: 0 = UsingUpVector (qLookAt), 1 = Simple. .t3 = 1
  uint _pad0;
  uint _pad1;
  uint _pad2;
  uint _pad3;           // -> 144 bytes (16-byte multiple)
#else
  uint32_t AddSeparator;
  uint32_t OrientationMode;
  uint32_t _pad0;
  uint32_t _pad1;
  uint32_t _pad2;
  uint32_t _pad3;
#endif
};

enum LineBinding {
  LINE_Points = 0,  // device SwPoint* (u0)
  LINE_Params = 1,  // constant LineParams& (b0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(LineParams) == 144, "LineParams must be a 16-byte multiple (144)");
#endif
