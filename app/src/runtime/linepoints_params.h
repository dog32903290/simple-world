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
// TiXL Vector/enum inputs NOT ported (baked to TiXL defaults in linepoints.metal,
// see parityNotes): ColorA/ColorB (Color = white), the two Orientation modes +
// OrientationAxis/Angle/Twist (Rotation = identity quat), F1/F2 (FX1/FX2 = 0),
// AddSeparator (false), W/WOffset (unused in the .hlsl — commented out there too).
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
  float _pad0;
  float _pad1;
  float _pad2;          // -> 64 bytes (16-byte multiple, like RadialParams)
};

enum LineBinding {
  LINE_Points = 0,  // device SwPoint* (u0)
  LINE_Params = 1,  // constant LineParams& (b0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(LineParams) == 64, "LineParams must be a 16-byte multiple (64)");
#endif
