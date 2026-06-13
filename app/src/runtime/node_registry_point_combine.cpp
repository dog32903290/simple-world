// runtime/node_registry_point_combine — NodeSpec table for point COMBINE ops.
// Ops that merge multiple Points bags into one: CombineBuffers.
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
  };
  return specs;
}

}  // namespace sw
