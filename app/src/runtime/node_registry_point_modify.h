// runtime/node_registry_point_modify — NodeSpec sub-table for point MODIFIER ops.
// Split from node_registry.cpp (批次16-R). Phase B "point-transform lane" extends this file.
#pragma once
#include "runtime/graph.h"
#include <vector>

namespace sw {
const std::vector<NodeSpec>& pointModifySpecs();
}  // namespace sw
