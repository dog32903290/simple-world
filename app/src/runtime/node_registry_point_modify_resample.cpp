// runtime/node_registry_point_modify_resample — Line resample / filter / reorient / subdivide modifiers (count-changing + line ops, TiXL point/modify + generate).
//
// Self-registering point-modify NodeSpec leaf (split from the 852-line node_registry_point_modify.cpp,
// ARCHITECTURE rule 4 + ratchet-debt). Each spec is a PURE NodeSpec CARRIER (cook=nullptr): the real
// cook is dispatched BY TYPE NAME in the point cook driver (point_graph.cpp) + the per-op point_ops_*
// leaves. Every spec below is moved VERBATIM from the old manifest — name / ports / widgets / defaults
// / emission semantics unchanged. Adding a point-modify op here = drop a PointModifyOp registrar; the
// central manifest is never touched again (mirror of the image-filter / value-op / string-op sinks).
#include "runtime/graph.h"                      // NodeSpec, PortSpec, Widget
#include "runtime/point_modify_op_registry.h"   // PointModifyOp / pointModifySpecSink

namespace sw {
namespace {

// ---- batch 15: point modify — FilterPoints -------------------------------------
// TiXL parity: external/tixl .../point/modify/FilterPoints.cs + FilterPoints.hlsl
// Re-samples/re-indexes the input bag into a new fixed-size output buffer (Count port).
// Output COUNT = Count port (not the input bag count) — this op changes count.
// Shader does scatter-copy: ResultPoints[i] = SourcePoints[imod2(StartIndex +
//   (i*StepSize) + scatterOffset, SourceCount)] where scatterOffset = Scatter>0 ?
//   SourceCount*Scatter*hash11u(i+Seed*SourceCount+StartIndex) : 0.
// Defaults from FilterPoints.t3: Count=1, StartIndex=0, Step=1.0, ScatterSelect=0.0, Seed=0
// Fork from TiXL: Count is a Float port (not Int) to match the resolved-param spine
// contract; the shader receives it cast to int. NAMED FORK (refuter-R-PMF3 修帳): TiXL clamps
// to [0,1000000]; we have NO runtime cap — only the port UI max (8192, conservative).
static const PointModifyOp _reg_FilterPoints{
      {"FilterPoints",
       "FilterPoints",
       {{"points", "points", "Points", true},   // input bag (port 0)
        {"out", "out", "Points", false},         // resampled output bag (port 1)
        // Count drives the OUTPUT buffer size (changes point count — not a modifier!).
        {"Count", "Count", "Float", true, 1.0f, 0.0f, 8192.0f},
        {"StartIndex", "StartIndex", "Float", true, 0.0f, 0.0f, 8191.0f},
        {"Step", "Step", "Float", true, 1.0f, 0.0f, 100.0f},
        {"ScatterSelect", "ScatterSelect", "Float", true, 0.0f, 0.0f, 1.0f},
        {"Seed", "Seed", "Float", true, 0.0f, 0.0f, 100.0f}},
       nullptr,
       "point.modify"}
};

// ---- batch 21 (lane point_modify): ReorientLinePoints ----------------------
// TiXL parity: external/tixl .../point/transform/ReorientLinePoints.cs + .hlsl
// A count-preserving MODIFIER: re-orients each live point's Rotation so its +Z forward
// follows the local line tangent (prevLiveNeighbour -> nextLiveNeighbour), blended by
// Amount via qSlerp. Defaults: Amount=1.0.
// FORK: the .cs Center/UpVector/WIsWeight/Flip [Input]s are DEAD in the .hlsl kernel
//       (main() reads only Amount) -> dropped (porting them = inventing dead knobs).
static const PointModifyOp _reg_ReorientLinePoints{
      {"ReorientLinePoints",
       "ReorientLinePoints",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // re-oriented output bag (port 1)
        {"Amount", "Amount", "Float", true, 1.0f, 0.0f, 1.0f}},
       nullptr,
       "point.transform"}
};

// ---- batch 36 (lane point_modify): ResampleLinePoints ----------------------
// TiXL parity: external/tixl .../point/modify/ResampleLinePoints.cs + .hlsl
// A COUNT-CHANGING MODIFIER: resamples the source line bag into `Count` points along the
// source's normalized PARAMETER f in [0,1] (TiXL samples by linear-index parameter, NOT true
// arc-length — see SamplePosAtF). Each output is a SmoothDistance-weighted average over
// (1 + 2*Samples) parameter taps; SEPARATOR points (NaN Scale) break the line into segments.
// Defaults from ResampleLinePoints.t3: Count=100, RangeMode=StartEnd(0), SampleRange=(0,1),
//   SmoothDistance=0.5, Samples=3, Rotation=Interpolate(0), RotationUpVector=(0,0,1).
// Ports in .cs [Input] order: Points, Count, RangeMode, SampleRange, SmoothDistance, Samples,
//   Rotation, RotationUpVector. EVERY port is read by the kernel — no dead port dropped.
// NAMED FORK: Count is a Float port (resolved-param spine), cast to int in the cook (clamp
//   1..100000 per the .t3 ClampInt). Samples clamped 1..10 (.t3 ClampInt). Same Float-port
//   convention as FilterPoints.
static const PointModifyOp _reg_ResampleLinePoints{
      {"ResampleLinePoints",
       "ResampleLinePoints",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // resampled output bag (port 1)
        {"Count", "Count", "Float", true, 100.0f, 1.0f, 8192.0f},  // output point count (.t3 default 100)
        {"RangeMode", "RangeMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"StartEnd", "StartLength"}},
        // SampleRange (TiXL Vector2, default (0,1)) — (start, end-or-length) of the f sweep.
        {"SampleRange.x", "SampleRange", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
        {"SampleRange.y", "SampleRange.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"SmoothDistance", "SmoothDistance", "Float", true, 0.5f, 0.0f, 10.0f},
        {"Samples", "Samples", "Float", true, 3.0f, 1.0f, 10.0f},
        {"Rotation", "Rotation", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"Interpolate", "Recompute"}},
        // RotationUpVector (TiXL Vector3, default (0,0,1)) — up vector for Recompute(qLookAt).
        {"RotationUpVector.x", "RotationUpVector", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"RotationUpVector.y", "RotationUpVector.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"RotationUpVector.z", "RotationUpVector.z", "Float", true, 1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1}},
       nullptr,
       "point.modify"}
};

// ---- batch 37 (lane point_modify): SubdivideLinePoints ----------------------
// TiXL parity: external/tixl .../point/generate/SubdivideLinePoints.cs + .hlsl
// A COUNT-CHANGING MODIFIER: subdivides every line SEGMENT, inserting InsertCount interpolated
// points per segment (subdiv = InsertCount + 1). Open line of SourceCount points ->
// SourceCount * subdiv outputs; ClosedShape adds a closing segment (lastValid -> firstValid)
// and separators (NaN Scale) carve the closed segments. Output count = clamp(sourceCount*subdiv,
// 1, 1000000) via the cook's static-stash countTransform (see point_ops_subdividelinepoints.cpp).
// Defaults from SubdivideLinePoints.t3: Count(InsertCount)=100, ClosedShape=false.
// Ports in .cs [Input] order: Points, Count, ClosedShape.
// NAMED FORK [port-id=InsertCount]: the .cs port is named "Count" but the cook driver hijacks any
//   Float port whose id == "Count" as the OUTPUT point count. SubdivideLinePoints' Count is
//   per-segment InsertCount, NOT the output count, so the port id is "InsertCount" (inspector
//   label "Count" matches TiXL); the driver then uses the source bag count and countTransform
//   multiplies by subdiv. See point_ops_subdividelinepoints.cpp COUNT POLICY.
static const PointModifyOp _reg_SubdivideLinePoints{
      {"SubdivideLinePoints",
       "SubdivideLinePoints",
       {{"points", "points", "Points", true},    // input bag (port 0)
        {"out", "out", "Points", false},          // subdivided output bag (port 1)
        // .cs Count (int, default 100) = InsertCount: points inserted per segment. Port id forked to
        //   "InsertCount" to dodge the driver's "Count"==output-count hijack (see fork note above).
        {"InsertCount", "Count", "Float", true, 100.0f, 0.0f, 1000.0f},
        // .cs ClosedShape (bool, default false) — add a closing segment (last -> first).
        {"ClosedShape", "ClosedShape", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool}},
       nullptr,
       "point.generate"}
};

}  // namespace
}  // namespace sw
