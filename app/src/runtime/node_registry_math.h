// runtime/node_registry_math — NodeSpec sub-table for MATH / VALUE ops.
// Split from node_registry.cpp (批次16-R).
#pragma once
#include "runtime/graph.h"
#include <vector>

namespace sw {
const std::vector<NodeSpec>& mathSpecs();
}  // namespace sw
