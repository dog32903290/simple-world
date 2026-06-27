// runtime/node_registry_point_combine — NodeSpec table for point COMBINE ops.
// Ops that merge or pair multiple Points bags into one output: CombineBuffers, SnapToPoints.
// Split from node_registry.cpp (批次16-R, ARCHITECTURE rule 4).
#include "runtime/node_registry_point_combine.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& pointCombineSpecs() {
  static const std::vector<NodeSpec> specs = {
      {"CombineBuffers",
       "CombineBuffers",
       // COMBINE op: up to 4 Points inputs concatenated into one output bag (TiXL MultiInput).
       // Output count = sum of wired inputs (PointGraph::nodeCount sumPointsCount contract).
       {{"input0", "input0", "Points", true},
        {"input1", "input1", "Points", true},
        {"input2", "input2", "Points", true},
        {"input3", "input3", "Points", true},
        {"out", "out", "Points", false}},
       nullptr,
       "point.combine"},

      // ---- batch 21: point combine — SnapToPoints --------------------------------
      // TiXL parity: external/tixl .../point/transform/SnapToPoints.cs (slots) +
      //              .../Assets/shaders/points/modify/SnapToPoints.hlsl (math)
      // A 2-input op: each point in Points1 is lerped toward the corresponding
      // (same-index) point in Points2 using a distance-based smoothstep blend.
      // Output count = Points1 count (index-paired, NOT nearest-point search).
      //
      // TiXL .hlsl cbuffer b0 (BlendFactor/Distance/MaxAmount):
      //   blendFactor = smoothstep(BlendFactor+Distance, Distance, dist) * MaxAmount
      //   Position    = lerp(A.Position, SnapPoint.Position, blendFactor)
      //   W (FX1)     = lerp(A.W, SnapPoint.W, BlendFactor)   // raw BlendFactor, not scaled
      //
      // FORK[count-policy]: outCount = Points1 count (NOT sum). SnapToPoints opts into
      //   OpReg.countFromFirstPointsInput (Points2 is a snap target, not concatenated);
      //   locked by the -countpolicy graph golden. See point_ops_snaptopoints.cpp.
      // FORK[count-guard]: Points2 index clamped to (Points2Count-1) when i >= Points2Count.
      //
      // Ports (append after CombineBuffers, do NOT insert before it):
      //   port 0: Points1 — primary bag (input)
      //   port 1: Points2 — snap-target bag (input)
      //   port 2: out     — result bag (output)
      //   port 3: BlendFactor (Float, default 0.0) — smoothstep lower edge + W lerp factor
      //   port 4: Distance    (Float, default 1.0) — smoothstep upper edge (snap radius)
      //   port 5: MaxAmount   (Float, default 1.0) — Position blend scale
      {"SnapToPoints",
       "SnapToPoints",
       {{"Points1", "Points1", "Points", true},   // port 0: primary input bag
        {"Points2", "Points2", "Points", true},   // port 1: snap-target bag (index-paired)
        {"out", "out", "Points", false},           // port 2: snapped output bag
        // TiXL .hlsl cbuffer b0 fields (verbatim names, same order as cbuffer):
        {"BlendFactor", "BlendFactor", "Float", true, 0.0f, 0.0f, 1.0f},  // port 3
        {"Distance",    "Distance",    "Float", true, 1.0f, 0.0f, 5.0f},  // port 4
        {"MaxAmount",   "MaxAmount",   "Float", true, 1.0f, 0.0f, 2.0f}}, // port 5
       nullptr,
       "point.transform"},

      // ---- point lane: MultiUpdatePoints (TiXL _internal fan-in helper) -------------
      // TiXL parity: external/tixl .../point/_internal/MultiUpdatePoints.{cs,t3}
      // A pass-through helper: forces multiple in-place point modifiers (each returning the SAME
      // BufferWithViews) to evaluate, then returns the LAST connected buffer unchanged. .t3ui:
      // "A helper to combine multiple point modifiers like ApplyNoise or ApplyForce to the same
      // Point buffer." No shader, no params (the cook is a single GPU blit of the last wired bag).
      //
      // PORTS: TiXL's ONE MultiInputSlot<BufferWithViews> is modeled as FIXED input0..input3 Points
      //   ports — same convention CombineBuffers uses (the cook driver's buffer-input gather reads one
      //   wire per PORT, it does not expand a MultiInput buffer port). Unwired inputs contribute null.
      // COUNT POLICY: output count = first wired input's count (== last in faithful same-buffer usage;
      //   countFromFirstPointsInput=true). NOT the sum — this op passes a buffer through, never concats.
      {"MultiUpdatePoints",
       "MultiUpdatePoints",
       {{"input0", "input0", "Points", true},
        {"input1", "input1", "Points", true},
        {"input2", "input2", "Points", true},
        {"input3", "input3", "Points", true},
        {"out", "out", "Points", false}},  // port 4: pass-through output bag
       nullptr,
       "point._internal"},

      // ---- count-product seam: RepeatAtPoints (TiXL GPU generate op) ----------------
      // TiXL parity: external/tixl .../point/generate/RepeatAtPoints.{cs,t3} (slots + count graph) +
      //              .../Assets/shaders/points/generate/RepeatAtGPoints.hlsl (the per-point math)
      // Places each SourcePoint into EACH TargetPoint's local frame -> the FULL CARTESIAN PRODUCT.
      // Output count = source.N * target.N — the canonical count-PRODUCT (NOT a sum). The driver's
      // countTransform hook is fed the product via a file-static set by the cook fn (the (B) static-
      // stash, proven by PairPointsForLines; zero driver signature change). See point_ops_repeatatpoints.cpp.
      //
      // Ports (append after MultiUpdatePoints, do NOT insert):
      //   port 0: SourcePoints (input, GPoints) — bag repeated at each target
      //   port 1: TargetPoints (input, GTargets) — destination frames
      //   port 2: out          (output) — the cartesian product bag
      //   port 3..: Float params, names verbatim from RepeatAtPoints.cs (.t3 defaults):
      //     Scale 1.0 / ApplyOrientation 1 (bool) / ApplyPointScale 1 (bool) /
      //     ScaleFactor 0 (UseFSources enum) / SetF1To 5 / SetF2To 6 (UseFSources) /
      //     CombineMode 0 (Linear|Interwoven) / AddSeparators 1 (bool; count fork — see leaf)
      {"RepeatAtPoints",
       "RepeatAtPoints",
       {{"SourcePoints", "SourcePoints", "Points", true},   // port 0: GPoints (repeated bag)
        {"TargetPoints", "TargetPoints", "Points", true},   // port 1: GTargets (destination frames)
        {"out", "out", "Points", false},                     // port 2: product output bag
        {"Scale",            "Scale",            "Float", true, 1.0f, 0.0f, 10.0f},                       // port 3
        {"ApplyOrientation", "ApplyOrientation", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},          // port 4
        {"ApplyPointScale",  "ApplyPointScale",  "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},          // port 5
        {"ScaleFactor",      "ScaleFactor",      "Float", true, 0.0f, 0.0f, 6.0f, Widget::Enum,
         {"None", "Target_F1", "Target_F2", "Source_F1", "Source_F2", "Multiplied_F1", "Multiplied_F2"}},// port 6
        {"SetF1To",          "SetF1To",          "Float", true, 5.0f, 0.0f, 6.0f, Widget::Enum,
         {"None", "Target_F1", "Target_F2", "Source_F1", "Source_F2", "Multiplied_F1", "Multiplied_F2"}},// port 7
        {"SetF2To",          "SetF2To",          "Float", true, 6.0f, 0.0f, 6.0f, Widget::Enum,
         {"None", "Target_F1", "Target_F2", "Source_F1", "Source_F2", "Multiplied_F1", "Multiplied_F2"}},// port 8
        {"CombineMode",      "CombineMode",      "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
         {"Linear", "Interwoven"}},                                                                       // port 9
        {"AddSeparators",    "AddSeparators",    "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool}},         // port 10
       nullptr,
       "point.generate"},

      // ---- count-product + GrowthMap seam: GrowStrains (TiXL experimental sim op) -------------------
      // TiXL parity: external/tixl .../point/sim/experimental/GrowStrains.{cs,t3} +
      //              .../Assets/shaders/points/sim/GrowStrains.hlsl
      // A STATELESS 2-input transform: PointsA × PointsB cartesian product (+1 NaN separator/source loop),
      // gated by a GrowthMap Texture2D (sampled at float2(B.W, 1-A.W)). Output count = (A.N+1)*B.N via the
      // static-stash countTransform. NoiseAmount drives a simplex displacement; the GrowthMap gates strand
      // length (d=saturate(r-0.05)). Ports 1:1 with GrowStrains.cs [Input] order, .t3 defaults.
      // fork[white-growthmap-fallback]: unwired GrowthMap → 1×1 white (live transform; see leaf).
      {"GrowStrains",
       "GrowStrains",
       {{"GPoints", "GPoints", "Points", true},                  // port 0: PointsA (GPoints, t0)
        {"GTargets", "GTargets", "Points", true},                 // port 1: PointsB (GTargets, t1)
        {"out", "out", "Points", false},                          // port 2: ResultPoints
        {"GrowthMap", "GrowthMap", "Texture2D", true},            // port 3: GrowthMap (t2; unwired→white)
        {"Variation", "Variation", "Float", true, 0.0f, 0.0f, 1.0f},                  // port 4 (.t3 0)
        {"NoiseAmount", "NoiseAmount", "Float", true, 0.0f, 0.0f, 10.0f},             // port 5 (.t3 0)
        {"Frequency", "Frequency", "Float", true, 0.0f, 0.0f, 10.0f},                 // port 6 (.t3 0)
        {"NoisePhase", "NoisePhase", "Float", true, 0.0f, -100.0f, 100.0f},           // port 7 (.t3 0)
        {"NoiseDistribution.x", "NoiseDistribution", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 3}, // port 8
        {"NoiseDistribution.y", "NoiseDistribution.y", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1}, // port 9
        {"NoiseDistribution.z", "NoiseDistribution.z", "Float", true, 1.0f, -10.0f, 10.0f, Widget::Vec, {}, true, 1}, // port 10
        {"NoiseRotationLookUp", "NoiseRotationLookUp", "Float", true, 0.5f, 0.0f, 10.0f},  // port 11 (.t3 0.5)
        {"Length", "Length", "Float", true, 0.13f, 0.0f, 10.0f},                      // port 12 (.t3 0.13)
        {"Width", "Width", "Float", true, 0.25f, 0.0f, 10.0f},                        // port 13 (.t3 0.25)
        {"NoiseDensity", "NoiseDensity", "Float", true, 1.0f, 0.0f, 10.0f}},          // port 14 (.t3 1.0)
       nullptr,
       "point.sim"},
  };
  return specs;
}

}  // namespace sw
