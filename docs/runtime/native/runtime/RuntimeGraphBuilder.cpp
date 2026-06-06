#include "RuntimeGraphBuilder.hpp"

#include <set>

namespace simple_world::runtime {

RuntimeGraphBuildResult buildRuntimeGraph(
    const simple_world::graph::GraphState& state,
    const simple_world::nodes::NodeSpecRegistry& registry
) {
    RuntimeGraph graph;
    graph.graphId = state.graphId;
    simple_world::graph::Diagnostics diagnostics;

    for (const auto& node : state.nodes) {
        const auto maturity = registry.maturityFor(node.type);
        if (!simple_world::nodes::canEnterRuntimeGraph(maturity)) {
            diagnostics.push_back({
                "runtime.node.not_runtime_ready",
                "Node is not mature enough to enter runtimeGraph."
            });
            continue;
        }
        graph.nodes.push_back(RuntimeNode{ node.id, node.type });
    }

    std::set<std::string> runtimeNodeIds;
    for (const auto& node : graph.nodes) {
        runtimeNodeIds.insert(node.id);
    }

    for (const auto& edge : state.edges) {
        if (runtimeNodeIds.count(edge.from.nodeId) == 0 || runtimeNodeIds.count(edge.to.nodeId) == 0) {
            diagnostics.push_back({
                "runtime.edge.endpoint_not_runtime_ready",
                "Edge endpoint is not present in runtimeGraph."
            });
            continue;
        }
        graph.edges.push_back(RuntimeEdge{ edge.from, edge.to, edge.type });
    }

    for (const auto& node : graph.nodes) {
        graph.cookOrder.push_back(node.id);
    }

    return RuntimeGraphBuildResult{ graph, diagnostics };
}

} // namespace simple_world::runtime
