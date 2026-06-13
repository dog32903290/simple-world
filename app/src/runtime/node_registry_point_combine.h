// runtime/node_registry_point_combine — NodeSpec sub-table for point COMBINE ops.
// Split from node_registry.cpp (批次16-R).
#pragma once
#include "runtime/graph.h"
#include <vector>

namespace sw {
const std::vector<NodeSpec>& pointCombineSpecs();
}  // namespace sw
