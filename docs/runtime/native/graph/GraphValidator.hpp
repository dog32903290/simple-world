#pragma once

#include "GraphDiagnostics.hpp"
#include "GraphDocument.hpp"
#include "../nodes/NodeSpecRegistry.hpp"

namespace simple_world::graph {

Diagnostics validateGraphState(
    const GraphState& state,
    const simple_world::nodes::NodeSpecRegistry& registry
);

} // namespace simple_world::graph
