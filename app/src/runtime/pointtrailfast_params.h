// pointtrailfast_params.h — host/GPU shared cbuffer layout for PointTrailFast.
// Reference: external/tixl/Operators/Lib/Assets/shaders/points/sim/PointTrailFast.hlsl
//   cbuffer Params  : register(b0) { float AddSeparatorThreshold; }
//   cbuffer Params2 : register(b1) { int CycleIndex; int TrailLength; int PointCount; int WriteOrderTo; }
//
// PointTrailFast is a FIXED-size trail ring: a single kernel writes each source point into a ring slot
// rotating by CycleIndex each frame. BufferLength = PointCount * TrailLength (the .t3 wires the buffer
// allocation as srcN * (TrailLength+1) — the +1 is the trailing NaN line-separator slot per point; see
// the cook for the ring-size fork). targetIndex = (CycleIndex + sourceIndex*TrailLength) % BufferLength.
//
// metal-cpp-discipline: this struct goes to a Metal buffer. Only scalar int/float members (4-byte each),
// no float3 → no packed alignment trap. static_assert pins the size so host & GPU agree.
#pragma once
#ifndef __METAL_VERSION__
  #include <cstdint>
#endif

// Buffer binding indices (host setBuffer/setBytes slot == [[buffer(N)]] in the .metal).
enum {
  POINTTRAILFAST_SourcePoints = 0,  // const device SwPoint* (input bag)
  POINTTRAILFAST_TrailPoints  = 1,  // device SwPoint* (output ring)
  POINTTRAILFAST_Params       = 2,  // PointTrailFastParams (b0+b1 merged)
};

// Single merged params blob (HLSL split b0/b1 → one Metal constant buffer; same fields, same order).
struct PointTrailFastParams {
  float AddSeparatorThreshold;  // b0: distance threshold to insert a NaN break (0 = off, .t3 default)
  int   CycleIndex;             // b1: ring write head (advances by TrailLength each enabled frame)
  int   TrailLength;            // b1: trail slots per source point (== ringPerPoint; SHADER ring stride)
  int   PointCount;             // b1: source point count
  int   WriteOrderTo;           // b1: PointTrailFast has no WriteOrderTo wired (.t3) → 0 (unused here)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(PointTrailFastParams) == 20, "PointTrailFastParams: 5 x 4-byte scalars");
#endif
