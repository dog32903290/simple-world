// runtime/node_registry_particle — NodeSpec sub-table for PARTICLE ops.
// Split from node_registry.cpp (批次16-R). Phase B "particle-force lane" extends this file.
#pragma once
#include "runtime/graph.h"
#include <vector>

namespace sw {
const std::vector<NodeSpec>& particleSpecs();
}  // namespace sw
