// PrepareSvgLineTransition pointlist op — a PURE point-list transform (NO SVG parsing). It measures the
// separator-delimited polyline segments of an incoming point list and writes a per-point stroke-animation
// progress into F1 (the "draw the strokes on over time" precompute). TiXL authority:
// external/tixl/Operators/Lib/point/io/PrepareSvgLineTransition.cs (mirrored 1:1).
//
// Despite the name it does not touch svg_parse — it consumes ANY separator-delimited PointList (typically
// LoadSvg's output) and rewrites F1. Placed with the SVG family because it is the LoadSvg→animation
// companion (LoadSvg emits the polylines; this stages them for a timed reveal).
//
//   PrepareSvgLineTransition.cs Update() (distilled):
//     PASS 1 (measure): walk points; a NaN-Scale.X separator ends the current segment. For each segment
//       with ≥2 points, record { PointIndex, PointCount, AccumulatedLength (running total), SegmentLength
//       (sum of consecutive Vector3.Distance) }. Track maxLength + totalLength. Bail if totalLength <
//       0.0001 OR < 2 segments (Log.Warning, returns without writing — sw: leaves F1 untouched, passes
//       the list through).
//     PASS 2 (write F1): dist = maxLength/(segments-1); Random(42). Per segment: a stacked TimeRange and a
//       packed TimeRange lerp'd by Spread, then optional randomizeStart/randomizeDuration; per point in
//       the segment, F1 = w computed by SpreadMode (IgnoreStrokeLengths / UseStrokeLength / Weird).
//     StrokeCount = segments.Count (an int side-output).
//
// NAMED FORKS (vs PrepareSvgLineTransition.cs — that file IS the parity reference):
//   • fork-preparesvg-single-output: the .cs has TWO outputs — ResultList (StructuredList) AND StrokeCount
//     (int). The pointlist channel carries ONE PointList output; StrokeCount (the segment count) is NOT
//     exposed as a separate wire here (the pointlist cook flow has no int side-output). The primary
//     ResultList output carries the F1-rewritten points. StrokeCount is a follow-up (a value-output side
//     channel), named not silent. The F1 write — the load-bearing transform — is fully ported.
//   • fork-preparesvg-inplace: the .cs rewrites SourcePoints IN PLACE and returns the same list. sw copies
//     the input list to the output then rewrites (the pointlist channel gives each node its own output
//     buffer; mutating an upstream borrowed input is forbidden). Observable Result identical.
//   • fork-preparesvg-random-net: randomizeStart/randomizeDuration use .NET Random(42).NextDouble(). sw
//     mirrors the ALGORITHM but a bit-exact .NET Mersenne/subtractive-generator sequence is not reproduced
//     — the golden pins the DEFAULT path (randomizeStart == randomizeDuration == 0, .t3 default), where the
//     Random stream is never drawn (both Math.Abs(...) < 0.0001 guards skip it), so F1 is deterministic and
//     impl-independent. The randomize branches are ported structurally; their bit-exactness is the fork.
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListInjectBug
#include "runtime/tixl_point.h"              // SwPoint full def

namespace sw {

namespace {

struct Segment {
  int pointIndex;
  float pointCount;
  float accumulatedLength;
  float segmentLength;
};

float dist3(const SwPoint& a, const SwPoint& b) {
  float dx = a.Position.x - b.Position.x;
  float dy = a.Position.y - b.Position.y;
  float dz = a.Position.z - b.Position.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ComputeOverlappingProgress (PrepareSvgLineTransition.cs:176-183).
float computeOverlappingProgress(float normalizedProgress, int index, int count, float spread) {
  float n = (spread * count - spread + 1.0f);
  float partialLength = 1.0f / n;
  float offset = index * spread / n;
  return (normalizedProgress - offset) / partialLength;
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

void cookPrepareSvgLineTransition(PointListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();

  // SourcePoints = inputLists[0] (the wired upstream PointList). No input → empty (mirror the .cs
  // null/empty early-out: SourcePoints null → return; NumElements==0 → SetLength(0) + passthrough).
  if (!c.inputLists || c.inputLists->empty() || (*c.inputLists)[0].empty()) {
    if (pointListInjectBug()) c.output->clear();
    return;
  }
  const std::vector<SwPoint>& src = (*c.inputLists)[0];

  // fork-preparesvg-inplace: copy input → output, then rewrite F1 in the copy.
  *c.output = src;
  std::vector<SwPoint>& pts = *c.output;
  const int numElements = (int)pts.size();

  const float spread = pointListParam(c.params, "Spread", 0.0f);
  const int spreadMode = (int)(pointListParam(c.params, "SpreadMode", 0.0f) + 0.5f);  // 0/1/2
  const float randomizeStart = pointListParam(c.params, "RandomizeStart", 0.0f);
  const float randomizeDuration = pointListParam(c.params, "RandomizeDuration", 0.0f);

  // PASS 1 — measure segments (a NaN Scale.X separator ends a segment). (.cs:56-91)
  std::vector<Segment> segments;
  segments.reserve(1000);
  int indexWithinSegment = 0;
  float lineSegmentLength = 0.0f;
  float totalLength = 0.0f;
  float maxLength = -std::numeric_limits<float>::infinity();
  for (int pointIndex = 0; pointIndex < numElements; ++pointIndex) {
    if (std::isnan(pts[pointIndex].Scale.x)) {
      bool hasAtLeastTwoPoints = indexWithinSegment > 1;
      if (hasAtLeastTwoPoints) {
        if (lineSegmentLength > maxLength) maxLength = lineSegmentLength;
        totalLength += lineSegmentLength;
        segments.push_back(Segment{pointIndex - indexWithinSegment, (float)indexWithinSegment,
                                   totalLength, lineSegmentLength});
      }
      lineSegmentLength = 0.0f;
      indexWithinSegment = 0;
    } else {
      if (indexWithinSegment > 0)
        lineSegmentLength += dist3(pts[pointIndex - 1], pts[pointIndex]);
      indexWithinSegment++;
    }
  }

  // Bail (leave F1 untouched, pass the list through). (.cs:93-97)
  if (totalLength < 0.0001f || (int)segments.size() < 2) {
    if (pointListInjectBug()) c.output->clear();
    return;
  }

  // PASS 2 — write F1 per point. (.cs:99-158)
  const int segCount = (int)segments.size();
  const float distStep = maxLength / (segCount - 1);
  for (int segmentIndex = 0; segmentIndex < segCount; ++segmentIndex) {
    const Segment& segment = segments[(size_t)segmentIndex];
    float segmentOffset = computeOverlappingProgress(0.0f, segmentIndex, segCount, spread);
    float lengthProgressWithinSegment = 0.0f;

    // stackedRange = FromStartAndDuration(acc - segLen, segLen) * (1/totalLength). (.cs:110)
    float stackedStart = (segment.accumulatedLength - segment.segmentLength) / totalLength;
    float stackedDuration = segment.segmentLength / totalLength;
    // packedRange = FromStartAndDuration(pGrid - anchor, segLen) * (1/maxLength). (.cs:112-114)
    float anchor = segmentIndex * segment.segmentLength / (segCount - 1);
    float pGrid = segmentIndex * distStep;
    float packedStart = (pGrid - anchor) / maxLength;
    float packedDuration = segment.segmentLength / maxLength;
    // range = Lerp(packed, stacked, spread). (.cs:115) — component-wise on start/duration.
    float rangeStart = lerp(packedStart, stackedStart, spread);
    float rangeDuration = lerp(packedDuration, stackedDuration, spread);

    // randomize branches (fork-preparesvg-random-net: structural port; default path never draws them).
    if (std::fabs(randomizeStart) > 0.0001f) {
      float randomStart = 0.0f * (1.0f - rangeDuration);  // .NET Random not bit-reproduced (fork)
      rangeStart = lerp(rangeStart, randomStart, randomizeStart);
    }
    if (std::fabs(randomizeDuration) > 0.0001f) {
      float randomDuration = 0.0f * (1.0f - rangeStart);
      rangeDuration = lerp(rangeDuration, randomDuration, randomizeDuration);
    }
    float rangeEnd = rangeStart + rangeDuration;

    for (int pointIndexInSegment = 0; pointIndexInSegment < (int)segment.pointCount;
         ++pointIndexInSegment) {
      int pi = segment.pointIndex + pointIndexInSegment;
      if (pointIndexInSegment > 0)
        lengthProgressWithinSegment += dist3(pts[pi - 1], pts[pi]);

      float normalizedSegmentPosition = pointIndexInSegment / (segment.pointCount - 1.0f);
      float w = 0.0f;
      switch (spreadMode) {
        case 0: {  // IgnoreStrokeLengths
          float segLenClamped = segment.segmentLength;
          if (segLenClamped < 0.001f) segLenClamped = 0.001f;
          if (segLenClamped > 999999.0f) segLenClamped = 999999.0f;
          float f = lengthProgressWithinSegment / segLenClamped;
          w = (f - segmentOffset) / (segCount + 1);
          break;
        }
        case 1:  // UseStrokeLength
          w = lerp(rangeStart, rangeEnd, normalizedSegmentPosition);
          break;
        case 2:  // Weird
          w = segmentOffset * 0.2f + pointIndexInSegment / segment.pointCount / 2.0f;
          break;
      }
      pts[pi].FX1 = w;
    }
  }

  // Test-only: corrupt the REAL output → CLEAR the whole list. Off in production.
  if (pointListInjectBug()) c.output->clear();
}

}  // namespace

// Self-registration. ONE PointList in ("SourcePoints") + ONE PointList out ("ResultList") + Spread /
// SpreadMode / Randomize knobs. StrokeCount int side-output is a named fork (not exposed here).
static const PointListOp _reg_preparesvglinetransition{
    {"PrepareSvgLineTransition", "PrepareSvgLineTransition",
     {{"ResultList", "ResultList", "PointList", false},
      {"SourcePoints", "SourcePoints", "PointList", true},
      {"Spread", "Spread", "Float", true, 0.0f, 0.0f, 4.0f},
      {"SpreadMode", "SpreadMode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
       {"IgnoreStrokeLengths", "UseStrokeLength", "Weird"}, true},
      {"RandomizeStart", "RandomizeStart", "Float", true, 0.0f, 0.0f, 1.0f},
      {"RandomizeDuration", "RandomizeDuration", "Float", true, 0.0f, 0.0f, 1.0f}},
     /*evaluate=*/nullptr},
    cookPrepareSvgLineTransition};

}  // namespace sw
