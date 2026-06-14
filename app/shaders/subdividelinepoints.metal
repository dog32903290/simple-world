// subdividelinepoints.metal — faithful Metal port of TiXL's SubdivideLinePoints.hlsl
// Source: external/tixl/Operators/Lib/Assets/shaders/points/modify/SubdivideLinePoints.hlsl
//
// A COUNT-CHANGING MODIFIER: subdivides each line segment, inserting InsertCount points per segment
// (subdiv = InsertCount + 1). Output index i.x -> segmentIndex = i.x/subdiv,
// segmentPointIndex = i.x%subdiv, f = segmentPointIndex/subdiv. f<=0.001 copies the segment START
// point verbatim, else lerps START -> END by f (position/FX1/FX2/Color/Scale via mix, Rotation via
// qSlerp). VERBATIM TWO PATHS, see below.
//
// VERBATIM port of main() (SubdivideLinePoints.hlsl:22-149):
//   OPEN  (CloseShape < 0.5, .hlsl:35-56):
//     subdiv = (int)(InsertCount + 1);
//     segmentIndex = i.x / subdiv;  segmentPointIndex = i.x % subdiv;  f = segmentPointIndex/subdiv;
//     f<=0.001 ? ResultPoints[i.x] = SourcePoints[segmentIndex]
//              : lerp SourcePoints[segmentIndex] -> SourcePoints[segmentIndex+1] by f.
//     NOTE (verbatim TiXL): the OPEN path does NOT inspect separators — it indexes raw
//     SourcePoints[segmentIndex]/[segmentIndex+1]; a separator (NaN Scale) inside the source is
//     lerped through like any other point, so a NaN tap propagates into that output segment exactly
//     as TiXL does.
//   CLOSED (CloseShape >= 0.5, .hlsl:58-149):
//     sourceCount<=1 -> copy-through (i.x<sourceCount ? ResultPoints[i.x]=SourcePoints[i.x]).
//     Count actualSegmentCount = #consecutive pairs (j,j+1) where NEITHER is a separator
//       (.hlsl:71-75), PLUS one closing segment lastValid->firstValid if firstValid<lastValid and
//       both endpoints are non-separators (.hlsl:79-94).
//     totalResultPoints = actualSegmentCount * subdiv; i.x >= totalResultPoints -> return (the
//       trailing buffer slots are left untouched — verbatim, separators shrink the output).
//     segmentIndex = i.x/subdiv; find the segmentIndex-th non-separator regular segment
//       (.hlsl:113-123); if not found and segmentIndex==currentSegment it's the closing segment
//       (startIndex=lastValidIndex, endIndex=firstValidIndex, .hlsl:126-130). Fallback copy-through
//       (.hlsl:132-138). Then f<=0.001 ? copy start : lerp start->end by f (.hlsl:140-149).
//
// SEPARATOR PRESERVATION (NAMED FORK — none; verbatim TiXL):
//   IsSeparator(p) = isnan(p.Scale.x) && isnan(p.Scale.y) && isnan(p.Scale.z) (.hlsl:16-20).
//   The CLOSED path uses it to skip separator segments and to find the closing segment's endpoints.
//   The OPEN path never calls it (see above). We port both paths verbatim.
#include <metal_stdlib>
#include "tixl_point.h"                  // SwPoint (64B)
#include "subdividelinepoints_params.h"  // SubdivideLineParams, SubdivideLineBinding
#include "shared/quat.metal.h"           // qSlerp
using namespace metal;

// VERBATIM port of IsSeparator (SubdivideLinePoints.hlsl:16-20): a separator is a point whose Scale
// is all-NaN.
inline bool isSeparator(const device SwPoint& p) {
  return isnan(p.Scale.x) && isnan(p.Scale.y) && isnan(p.Scale.z);
}

// Shared lerp body for f>0.001 (both paths, .hlsl:47-54 / 143-148). start/end are source indices.
inline void writeLerp(device SwPoint* ResultPoints, uint outIdx,
                      device const SwPoint* SourcePoints, uint startIndex, uint endIndex, float f) {
  ResultPoints[outIdx].Position = packed_float3(
      mix((float3)SourcePoints[startIndex].Position, (float3)SourcePoints[endIndex].Position, f));
  ResultPoints[outIdx].FX1 = mix(SourcePoints[startIndex].FX1, SourcePoints[endIndex].FX1, f);
  ResultPoints[outIdx].Rotation =
      qSlerp(SourcePoints[startIndex].Rotation, SourcePoints[endIndex].Rotation, f);
  ResultPoints[outIdx].Color =
      mix(SourcePoints[startIndex].Color, SourcePoints[endIndex].Color, f);
  ResultPoints[outIdx].FX2 = mix(SourcePoints[startIndex].FX2, SourcePoints[endIndex].FX2, f);
  ResultPoints[outIdx].Scale = packed_float3(
      mix((float3)SourcePoints[startIndex].Scale, (float3)SourcePoints[endIndex].Scale, f));
}

kernel void subdividelinepoints(device const SwPoint* SourcePoints [[buffer(SUBDIVIDELINE_SourcePoints)]],
                                device SwPoint*       ResultPoints [[buffer(SUBDIVIDELINE_ResultPoints)]],
                                constant SubdivideLineParams& P     [[buffer(SUBDIVIDELINE_Params)]],
                                uint3 i [[thread_position_in_grid]]) {
  uint pointCount  = P.ResultCount;   // ResultPoints.GetDimensions(pointCount, stride)
  uint sourceCount = P.SourceCount;   // SourcePoints.GetDimensions(sourceCount, stride)

  if (i.x >= pointCount) {
    return;
  }

  // ===== OPEN PATH (CloseShape < 0.5) — verbatim .hlsl:35-56 =====
  if (P.CloseShape < 0.5f) {
    int subdiv = (int)(P.InsertCount + 1.0f);

    int segmentIndex      = (int)i.x / subdiv;
    int segmentPointIndex = (int)i.x % subdiv;

    float f = (float)segmentPointIndex / (float)subdiv;

    if (f <= 0.001f) {
      ResultPoints[i.x] = SourcePoints[segmentIndex];
    } else {
      writeLerp(ResultPoints, i.x, SourcePoints, (uint)segmentIndex, (uint)(segmentIndex + 1), f);
    }
    return;
  }

  // ===== CLOSED PATH (CloseShape >= 0.5) — verbatim .hlsl:58-149 =====
  if (sourceCount <= 1) {
    if (i.x < sourceCount) {
      ResultPoints[i.x] = SourcePoints[i.x];
    }
    return;
  }

  // Count actual segments for closed shape (.hlsl:68-94).
  uint actualSegmentCount = 0;
  for (uint j = 0; j < sourceCount - 1; j++) {
    if (!isSeparator(SourcePoints[j]) && !isSeparator(SourcePoints[j + 1])) {
      actualSegmentCount++;
    }
  }

  uint firstValidIndex = 0;
  uint lastValidIndex  = sourceCount - 1;
  while (firstValidIndex < sourceCount && isSeparator(SourcePoints[firstValidIndex])) {
    firstValidIndex++;
  }
  while (lastValidIndex > 0 && isSeparator(SourcePoints[lastValidIndex])) {
    lastValidIndex--;
  }

  if (firstValidIndex < lastValidIndex &&
      !isSeparator(SourcePoints[firstValidIndex]) &&
      !isSeparator(SourcePoints[lastValidIndex])) {
    actualSegmentCount++;
  }

  int  subdiv            = (int)(P.InsertCount + 1.0f);
  uint totalResultPoints = actualSegmentCount * (uint)subdiv;

  if (i.x >= totalResultPoints) {
    return;
  }

  int   segmentIndex      = (int)i.x / subdiv;
  int   segmentPointIndex = (int)i.x % subdiv;
  float f                 = (float)segmentPointIndex / (float)subdiv;

  // Find the actual segment corresponding to segmentIndex (.hlsl:107-130).
  uint currentSegment = 0;
  uint startIndex     = 0;
  uint endIndex       = 0;
  bool foundSegment   = false;

  for (uint j = 0; j < sourceCount - 1 && !foundSegment; j++) {
    if (!isSeparator(SourcePoints[j]) && !isSeparator(SourcePoints[j + 1])) {
      if (currentSegment == (uint)segmentIndex) {
        startIndex   = j;
        endIndex     = j + 1;
        foundSegment = true;
      }
      currentSegment++;
    }
  }

  if (!foundSegment && (uint)segmentIndex == currentSegment) {
    startIndex   = lastValidIndex;
    endIndex     = firstValidIndex;
    foundSegment = true;
  }

  if (!foundSegment) {
    // Fallback (.hlsl:132-138).
    if (i.x < sourceCount) {
      ResultPoints[i.x] = SourcePoints[i.x];
    }
    return;
  }

  if (f <= 0.001f) {
    ResultPoints[i.x] = SourcePoints[startIndex];
  } else {
    writeLerp(ResultPoints, i.x, SourcePoints, startIndex, endIndex, f);
  }
}
