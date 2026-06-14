// Shared host<->shader params for the TiXL-ported ResampleLinePoints MODIFIER (lane point_modify).
// Mirrors external/tixl .../point/modify/ResampleLinePoints.cs (.cs ports) +
// .../Assets/shaders/points/modify/ResampleLinePoints.hlsl (.hlsl math).
//
// A COUNT-CHANGING MODIFIER: resamples a line (the input point bag) into `Count` points by
// sampling along the NORMALIZED PARAMETER f in [0,1] (NOT true arc-length — TiXL samples the
// source list by linear-index parameter; see SamplePosAtF in the .hlsl). Each output point is a
// SmoothDistance-weighted average of (1 + 2*Samples) parameter taps around f. Output count comes
// from the Count port, independent of the source count.
//
// TiXL inputs (ResampleLinePoints.cs — 1 bag + 7 scalar/vector inputs, [Input] order):
//   Points          (BufferWithViews)            — the source line (input bag)
//   Count           (int,   default 100)         — number of output points
//   RangeMode       (int enum SampleModes,  d=0) — StartEnd(0) / StartLength(1)
//   SampleRange     (Vector2, default (0,1))     — (start, end-or-length) of f sweep
//   SmoothDistance  (float, default 0.5)         — neighbourhood width for the smoothing average
//   Samples         (int,   default 3, clamp 1..10) — taps per side of the smoothing average
//   Rotation        (int enum RotationModes, d=0)— Interpolate(0) / Recompute(1)
//   RotationUpVector(Vector3, default (0,0,1))   — up vector for Recompute(qLookAt)
//
// TiXL cbuffers (ResampleLinePoints.hlsl:6-21):
//   b0 { float SmoothDistance; float2 SampleRange; float __padding; float3 UpVector; }
//   b1 { int SourceCount; int ResultCount; int SampleMode; int SampleCount; int RotationMode; }
// We FLATTEN both cbuffers into one struct (16-byte rows; static_assert). The .cs ports map 1:1;
// no port is dropped (every [Input] is read by main() or its samplers — see .cpp parity notes).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct ResampleLineParams {
#ifdef __METAL_VERSION__
  // row 0
  float SmoothDistance;  // b0.SmoothDistance
  float SampleRangeX;    // b0.SampleRange.x  (start of f sweep)
  float SampleRangeY;    // b0.SampleRange.y  (end-or-length of f sweep)
  float _pad0;           // b0.__padding -> 16 bytes
  // row 1
  float UpVectorX;       // b0.UpVector.x
  float UpVectorY;       // b0.UpVector.y
  float UpVectorZ;       // b0.UpVector.z
  float _pad1;           // -> 16 bytes
  // row 2
  uint  SourceCount;     // b1.SourceCount (input bag size)
  uint  ResultCount;     // b1.ResultCount (output Count)
  int   SampleMode;      // b1.SampleMode  (RangeMode enum: 0 StartEnd / 1 StartLength)
  int   SampleCount;     // b1.SampleCount (Samples, clamped 1..10) -> 16 bytes
  // row 3
  int   RotationMode;    // b1.RotationMode (Rotation enum: 0 Interpolate / 1 Recompute)
  int   _pad2;
  int   _pad3;
  int   _pad4;           // -> 16 bytes
#else
  float    SmoothDistance;
  float    SampleRangeX;
  float    SampleRangeY;
  float    _pad0;
  float    UpVectorX;
  float    UpVectorY;
  float    UpVectorZ;
  float    _pad1;
  uint32_t SourceCount;
  uint32_t ResultCount;
  int32_t  SampleMode;
  int32_t  SampleCount;
  int32_t  RotationMode;
  int32_t  _pad2;
  int32_t  _pad3;
  int32_t  _pad4;
#endif
};

enum ResampleLineBinding {
  RESAMPLELINE_SourcePoints = 0,  // const device SwPoint* (t0)
  RESAMPLELINE_ResultPoints = 1,  // device SwPoint*       (u0)
  RESAMPLELINE_Params       = 2,  // constant ResampleLineParams& (b0+b1 flattened)
};

#ifndef __METAL_VERSION__
// 4 rows x 16 bytes = 64 bytes
static_assert(sizeof(ResampleLineParams) == 64, "ResampleLineParams must be 64 bytes");
#endif
