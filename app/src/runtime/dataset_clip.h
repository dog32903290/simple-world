// runtime/dataset_clip — SwTimeRangeMapping + SwDataClip: the timeline-placement wrapper around a SwDataSet
// (the DataClip half of the dataset-currency seam). C++ twin of:
//   • TimeRangeMapping.cs (external/tixl/Core/Animation/TimeRangeMapping.cs) — the playhead→source-time math
//     every timeline clip performs: a linear remap TimeRange(bars)→SourceRange(bars), then bars→seconds via
//     the BPM baked in at construction.
//   • DataClip.cs (external/tixl/Core/DataTypes/DataSet/DataClip.cs) — { DataSet Set; TimeRangeMapping?
//     Mapping } — a DataSet placed on the timeline. Mapping is OPTIONAL (untimed sources wrap content in a
//     clip for a uniform output type); a consumer needing the mapping checks hasMapping and skips otherwise.
//
// This is the value SimulateIoData.Clips carries and LoadDataClip.Clip produces. sw already models the
// authored TimeRange/SourceRange as ClipTimeData (runtime/compound_graph.h — the dataset-timeline seam on
// main); SwTimeRangeMapping is the QUERYABLE snapshot form (adds the bars↔secs conversion + baked BPM), so
// the replay consumer can convert ANY timeline point, not just "now" (TimeRangeMapping.cs:11-16).
//
// runtime leaf: pure math (double), no GPU/UI/upward dep.
#pragma once

#include <cmath>

#include "runtime/dataset.h"  // SwDataSet

namespace sw {

// TiXL TimeRangeMapping (readonly struct, TimeRangeMapping.cs:24-90). A snapshot: TimeRange (timeline bars)
// ↔ SourceRange (source-content bars) + the BPM captured at build. All fields value-typed so it is a stable
// per-frame snapshot (a later TimeClip mutation can't leak in).
struct SwTimeRangeMapping {
  double timeStart = 0.0, timeEnd = 0.0;      // TiXL TimeRange (timeline placement, bars)
  double sourceStart = 0.0, sourceEnd = 0.0;  // TiXL SourceRange (exposed source slice, bars)
  double bpm = 120.0;                         // TiXL Bpm (baked at construction — bars→secs anchor)

  // TimeRangeMapping.LocalBarsToSourceBars (:47-56): linear remap timeline-bars → source-bars. When the
  // timeline window is degenerate (|end-start|<=1e-4) the rate collapses (no scaling), matching the .cs
  // guard so a zero-length clip maps every input to SourceRange.Start (+the un-scaled offset).
  double localBarsToSourceBars(double localBars) const {
    double pos = localBars - timeStart;
    if (std::fabs(timeEnd - timeStart) > 0.0001) {
      double rate = (sourceEnd - sourceStart) / (timeEnd - timeStart);
      pos *= rate;
    }
    return pos + sourceStart;
  }

  // TimeRangeMapping.LocalBarsToSourceSecs (:62-63): source-bars → seconds via 240/BPM (4 beats/bar * 60).
  double localBarsToSourceSecs(double localBars) const {
    return localBarsToSourceBars(localBars) * 240.0 / bpm;
  }

  // TimeRangeMapping.IsActive (:88-89): is the playhead inside the timeline window (inclusive both ends)?
  bool isActive(double localBars) const {
    return localBars >= timeStart && localBars <= timeEnd;
  }
};

// TiXL DataClip (DataClip.cs:26-35): a SwDataSet + an OPTIONAL timeline mapping. `hasMapping` is the C++
// twin of TiXL's `Mapping?` null-check — SimulateIoData.cs:105 skips a clip whose Mapping is null.
struct SwDataClip {
  SwDataSet set;
  bool hasMapping = false;       // TiXL DataClip.Mapping != null
  SwTimeRangeMapping mapping;    // valid only when hasMapping (else consumers skip — SimulateIoData.cs:105)
};

}  // namespace sw
