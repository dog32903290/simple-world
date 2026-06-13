// runtime/node_registry_generators — NodeSpec sub-table for point GENERATOR ops.
// Split from node_registry.cpp (批次16-R). Included by node_registry.cpp builder.
#pragma once
#include "runtime/graph.h"
#include <vector>

namespace sw {
const std::vector<NodeSpec>& generatorSpecs();
}  // namespace sw
