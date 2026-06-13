// Shared host<->shader params for the TiXL-ported SnapPointsToGrid MODIFIER (batch 19).
// Mirrors external/tixl .../point/transform/SnapPointsToGrid.cs (.cs ports) +
// .../Assets/shaders/points/_internal/SnapPointsToGrid.hlsl (.hlsl math).
//
// SnapPointsToGrid lerps each point's position toward the nearest grid-cell center,
// with blend Amount and per-axis stretch/offset. Math (from .hlsl lines 35-69):
//   gridSize = GridScale * GridStretch
//   normalizedPosition = pos / gridSize
//   normlizedOffsetPosition = normalizedPosition + 0.5 - GridOffset
//   signedFraction = (mod(normlizedOffsetPosition, 1) - 0.5) * 2
//   centerPoint = pos - signedFraction * gridSize / 2
//   snapAmount = fn(Mode, signedFraction, gridSize, scatter)
//   biasedSnap = ApplyGainAndBias(snapAmount, GainAndBias)
//   ff = (1 - saturate(biasedSnap - Amount*2 + 1)) * strength
//   p.Position = lerp(orgPosition, centerPoint, ff)
//
// TiXL cbuffer (SnapPointsToGrid.hlsl register b0):
//   float3 GridStretch; float Amount;
//   float3 GridOffset;  float GridScale;
//   float Scatter; float Mode; float2 GainAndBias;
//   (IntParams cbuffer b1: int StrengthFactor)
//
// Forks:
//   Scatter baked to 0.0 (hash-based random jitter, no Point hash available; deferred).
//   StrengthFactor baked to 0 (None: strength=1.0; F1/F2 path deferred).
//   UseWAsWeight / UseSelection baked to 0 (deferred).
//   GainAndBias default = (0.5, 0.5) per TiXL; ApplyGainAndBias = bias-functions.hlsl.
//
// All Vector3 inputs X/Y/Z scalars; no packed_float3 trap. 16-byte aligned.
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SnapToGridParams {
#ifdef __METAL_VERSION__
  uint  Count;    // inherited from input bag
  float Amount;   // .cs Amount (Single), default 1.0 — snap blend strength
  float GridScale; // .cs GridScale (Single), default 1.0
  float Mode;     // .cs Mode enum (0=CenterDistance,1=CornersDistance,2=AxisCenterDistance,3=AxisEdgeDistance)
#else
  uint32_t Count;
  float    Amount;
  float    GridScale;
  float    Mode;    // float for cbuffer alignment; cast to int in shader
#endif
  // TiXL: GridStretch (Vector3, default 1,1,1)
  float GridStretchX, GridStretchY, GridStretchZ;
  float _pad0;      // -> 16 bytes
  // TiXL: GridOffset (Vector3, default 0,0,0)
  float GridOffsetX, GridOffsetY, GridOffsetZ;
  // TiXL: GainAndBias (Vector2, default 0.5,0.5)
  float GainAndBiasX;  // -> 16 bytes
  float GainAndBiasY;
  float _pad1;
  float _pad2;
  float _pad3;  // -> 16 bytes
};

enum SnapToGridBinding {
  SNAPTOGRID_SourcePoints = 0,  // const device SwPoint* (t0)
  SNAPTOGRID_ResultPoints = 1,  // device SwPoint*       (u0)
  SNAPTOGRID_Params       = 2,  // constant SnapToGridParams& (b0)
};

#ifndef __METAL_VERSION__
// Count(4)+Amount(4)+GridScale(4)+Mode(4)=16 | GridStretch(12)+pad(4)=16 |
// GridOffset(12)+GainX(4)=16 | GainY(4)+3xpad(12)=16 = 64 bytes
static_assert(sizeof(SnapToGridParams) == 64, "SnapToGridParams must be 64 bytes");
#endif
