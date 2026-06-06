#pragma once

#include "RuntimeGraph.hpp"
#include "../graph/GraphDiagnostics.hpp"
#include "../nodes/NodeSpecRegistry.hpp"

namespace simple_world::runtime {

struct RuntimeGraphBuildResult {
    RuntimeGraph runtimeGraph;
    simple_world::graph::Diagnostics diagnostics;
};

RuntimeGraphBuildResult buildRuntimeGraph(
    const simple_world::graph::GraphState& state,
    const simple_world::nodes::NodeSpecRegistry& registry
);

} // namespace simple_world::runtime
