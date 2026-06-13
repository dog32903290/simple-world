// runtime/node_registry_draw — NodeSpec sub-table for DRAW/RENDER ops.
// Split from node_registry.cpp (批次16-R).
#pragma once
#include "runtime/graph.h"
#include <vector>

namespace sw {
const std::vector<NodeSpec>& drawSpecs();
}  // namespace sw
