// runtime/node_registry_image_filter — NodeSpec sub-table for IMAGE FILTER ops.
// Split from node_registry.cpp (批次16-R).
#pragma once
#include "runtime/graph.h"
#include <vector>

namespace sw {
const std::vector<NodeSpec>& imageFilterSpecs();
}  // namespace sw
