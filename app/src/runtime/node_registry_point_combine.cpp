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
       nullptr},

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
       nullptr},

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
       nullptr},
  };
  return specs;
}

}  // namespace sw
