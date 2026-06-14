// Shared host<->shader params for the TiXL-ported SubdivideLinePoints MODIFIER (lane point_modify).
// Mirrors external/tixl .../point/generate/SubdivideLinePoints.cs (.cs ports) +
// .../Assets/shaders/points/modify/SubdivideLinePoints.hlsl (.hlsl math).
//
// A COUNT-CHANGING MODIFIER: subdivides every line SEGMENT of the source bag, inserting
// `InsertCount` interpolated points per segment. With subdiv = InsertCount + 1, an open line of
// SourceCount points produces SourceCount * subdiv outputs (the kernel maps output index i.x to
// segmentIndex = i.x / subdiv, segmentPointIndex = i.x % subdiv, f = segmentPointIndex / subdiv;
// f<=0.001 copies the segment's start point verbatim, else lerps start->start+1 by f).
//
// Two code paths (SubdivideLinePoints.hlsl):
//   CloseShape < 0.5 (OPEN):  simple index math, lerp SourcePoints[seg] -> SourcePoints[seg+1].
//                             (the open path does NOT inspect separators — verbatim TiXL.)
//   CloseShape >= 0.5 (CLOSED): counts ACTUAL non-separator segments, adds one closing segment
//                             (lastValid -> firstValid), then maps i.x onto the found segment;
//                             separators split the line and the closing segment wraps it.
//
// TiXL inputs (SubdivideLinePoints.cs — [Input] order: Points, Count, ClosedShape):
//   Points       (BufferWithViews)        — the source line (input bag)
//   Count        (int,  default 100)      — InsertCount: interpolated points inserted PER segment
//   ClosedShape  (bool, default false)    — when true, add a closing segment (last -> first)
//
// TiXL cbuffer (SubdivideLinePoints.hlsl:6-10):
//   b0 { float InsertCount; float CloseShape; }
// The .hlsl reads the buffer element counts via GetDimensions (ResultPoints.GetDimensions ->
// pointCount, SourcePoints.GetDimensions -> sourceCount). Metal compute has no GetDimensions on a
// device pointer, so we pass those two dimensions through the params struct (ResultCount /
// SourceCount). This is NOT a fork — they are exactly the StructuredBuffer dimensions the HLSL
// queries; we only relocate where they come from. Padded to a 16-byte row (static_assert).
#pragma once

#ifdef __METAL_VERSION__
  #include <metal_stdlib>
  using namespace metal;
#else
  #include <cstdint>
#endif

struct SubdivideLineParams {
#ifdef __METAL_VERSION__
  float InsertCount;  // b0.InsertCount (Count param; points inserted per segment)
  float CloseShape;   // b0.CloseShape  (0 = open, 1 = closed)
  uint  ResultCount;  // ResultPoints.GetDimensions(pointCount, ...) — output buffer element count
  uint  SourceCount;  // SourcePoints.GetDimensions(sourceCount, ...) — input  buffer element count
#else
  float    InsertCount;
  float    CloseShape;
  uint32_t ResultCount;
  uint32_t SourceCount;
#endif
};

enum SubdivideLineBinding {
  SUBDIVIDELINE_SourcePoints = 0,  // const device SwPoint* (t0)
  SUBDIVIDELINE_ResultPoints = 1,  // device SwPoint*       (u0)
  SUBDIVIDELINE_Params       = 2,  // constant SubdivideLineParams& (b0)
};

#ifndef __METAL_VERSION__
static_assert(sizeof(SubdivideLineParams) == 16, "SubdivideLineParams must be 16 bytes");
#endif
