#include "GraphValidator.hpp"

#include <set>

namespace simple_world::graph {

Diagnostics validateGraphState(
    const GraphState& state,
    const simple_world::nodes::NodeSpecRegistry& registry
) {
    Diagnostics diagnostics;
    std::set<std::string> nodeIds;

    for (const auto& node : state.nodes) {
        if (!nodeIds.insert(node.id).second) {
            diagnostics.push_back({ "graph.node.duplicate_id", "Duplicate node id." });
        }
        if (!registry.findSpec(node.type).has_value()) {
            diagnostics.push_back({ "graph.node.unknown_type", "Unknown node type." });
        }
    }

    for (const auto& edge : state.edges) {
        const auto* fromNode = findNode(state, edge.from.nodeId);
        const auto* toNode = findNode(state, edge.to.nodeId);
        if (fromNode == nullptr || toNode == nullptr) {
            diagnostics.push_back({ "graph.edge.missing_node", "Edge endpoint node does not exist." });
            continue;
        }

        const auto fromPort = registry.findOutput(fromNode->type, edge.from.port);
        const auto toPort = registry.findInput(toNode->type, edge.to.port);
        if (!fromPort.has_value() || !toPort.has_value()) {
            diagnostics.push_back({ "graph.edge.missing_port", "Edge endpoint port does not exist." });
            continue;
        }

        if (fromPort->type != toPort->type) {
            diagnostics.push_back({ "graph.edge.type_mismatch", "Edge endpoint port types do not match." });
        }
    }

    return diagnostics;
}

} // namespace simple_world::graph
