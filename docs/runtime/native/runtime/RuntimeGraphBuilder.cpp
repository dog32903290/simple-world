#include "RuntimeGraphBuilder.hpp"

#include <map>
#include <set>

namespace simple_world::runtime {

namespace {

std::string portRefToString(const simple_world::graph::PortRef& port) {
    return port.nodeId + "." + port.port;
}

} // namespace

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
                "Node is not mature enough to enter runtimeGraph: " + node.id
                    + " (" + node.type + ")."
            });
            continue;
        }
        graph.nodes.push_back(RuntimeNode{ node.id, node.type });
    }

    std::set<std::string> runtimeNodeIds;
    std::map<std::string, std::string> runtimeNodeTypes;
    for (const auto& node : graph.nodes) {
        runtimeNodeIds.insert(node.id);
        runtimeNodeTypes[node.id] = node.type;
    }

    for (const auto& edge : state.edges) {
        if (runtimeNodeIds.count(edge.from.nodeId) == 0 || runtimeNodeIds.count(edge.to.nodeId) == 0) {
            diagnostics.push_back({
                "runtime.edge.endpoint_not_runtime_ready",
                "Edge endpoint is not present in runtimeGraph for edge " + edge.id + ": "
                    + portRefToString(edge.from) + " -> " + portRefToString(edge.to) + "."
            });
            continue;
        }

        const auto fromPort = registry.findOutput(runtimeNodeTypes.at(edge.from.nodeId), edge.from.port);
        const auto toPort = registry.findInput(runtimeNodeTypes.at(edge.to.nodeId), edge.to.port);
        if (!fromPort.has_value() || !toPort.has_value()) {
            diagnostics.push_back({
                "runtime.edge.missing_port",
                "Edge endpoint port does not exist for edge " + edge.id + ": "
                    + portRefToString(edge.from) + " -> " + portRefToString(edge.to) + "."
            });
            continue;
        }

        if (fromPort->type != toPort->type) {
            diagnostics.push_back({
                "runtime.edge.type_mismatch",
                "Edge endpoint port types do not match for edge " + edge.id + ": "
                    + portRefToString(edge.from) + " is " + fromPort->type
                    + ", " + portRefToString(edge.to) + " is " + toPort->type + "."
            });
            continue;
        }

        graph.edges.push_back(RuntimeEdge{ edge.from, edge.to, fromPort->type });
    }

    for (const auto& node : graph.nodes) {
        graph.cookOrder.push_back(node.id);
    }

    return RuntimeGraphBuildResult{ graph, diagnostics };
}

} // namespace simple_world::runtime
