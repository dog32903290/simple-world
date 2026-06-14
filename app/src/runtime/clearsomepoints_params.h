// Shared host<->shader params for the TiXL-ported ClearSomePoints MODIFIER (batch 20).
// Mirrors external/tixl .../point/modify/ClearSomePoints.cs (.cs ports) +
// .../Assets/shaders/points/modify/ClearSomePoints.hlsl (.hlsl math).
// A count-preserving MODIFIER: per-point hash(Resolution,Seed,Repeat) determines whether
// each point is "killed" by setting p.Scale = NAN. Count is INHERITED from upstream.
//
// TiXL inputs (ClearSomePoints.cs):
//   Ratio   (float, default 0.0)  — fraction of points to kill; 0=none, 1=all
//   Seed    (int,   default 0)    — hash seed
//   Repeat  (int,   default 0)    — 0 = no period (effectively 999999999)
//   Resolution (int, default 0)   — block size: points i in same block share the same hash
//
// TiXL cbuffer layout (ClearSomePoints.hlsl):
//   b0: float Ratio
//   b1: int Seed, int Repeat, int Resolution
//
// Flattened to single struct (no packed_float3 trap; 16-byte rows; static_assert).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct ClearSomePointsParams {
#ifdef __METAL_VERSION__
  uint  Count;    // inherited from input bag (count-preserving modifier)
  float Ratio;    // fraction to kill [0,1]; from b0
  float _pad0;
  float _pad1;    // -> 16 bytes
  int   Seed;
  int   Repeat;
  int   Resolution;
  int   _pad2;    // -> 16 bytes
#else
  uint32_t Count;
  float    Ratio;
  float    _pad0;
  float    _pad1;
  int32_t  Seed;
  int32_t  Repeat;
  int32_t  Resolution;
  int32_t  _pad2;
#endif
};

enum ClearSomePointsBinding {
  CLEARSOMEPOINTS_SourcePoints = 0,  // const device SwPoint* (t0)
  CLEARSOMEPOINTS_ResultPoints = 1,  // device SwPoint*       (u0)
  CLEARSOMEPOINTS_Params       = 2,  // constant ClearSomePointsParams& (b0/b1 merged)
};

#ifndef __METAL_VERSION__
// Count+Ratio+2xpad=16 | Seed+Repeat+Resolution+pad=16 = 32 bytes
static_assert(sizeof(ClearSomePointsParams) == 32, "ClearSomePointsParams must be 32 bytes");
#endif
