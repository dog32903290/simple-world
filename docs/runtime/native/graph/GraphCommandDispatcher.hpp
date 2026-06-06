#pragma once

#include "GraphCommand.hpp"
#include "GraphDiagnostics.hpp"
#include "../nodes/NodeSpecRegistry.hpp"

namespace simple_world::graph {

struct DispatchResult {
    GraphState state;
    Diagnostics diagnostics;
};

DispatchResult dispatchGraphCommand(
    const GraphState& state,
    const GraphCommand& command,
    const simple_world::nodes::NodeSpecRegistry& registry
);

} // namespace simple_world::graph
