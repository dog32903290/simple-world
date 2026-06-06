#include "GraphValidator.hpp"

#include <set>

namespace simple_world::graph {

namespace {

std::string portRefToString(const PortRef& port) {
    return port.nodeId + "." + port.port;
}

bool paramValueMatchesType(const ParamValue& value, const std::string& type) {
    if (type == "float") {
        return std::holds_alternative<double>(value);
    }
    if (type == "bool") {
        return std::holds_alternative<bool>(value);
    }
    if (type == "string") {
        return std::holds_alternative<std::string>(value);
    }
    return true;
}

} // namespace

Diagnostics validateGraphState(
    const GraphState& state,
    const simple_world::nodes::NodeSpecRegistry& registry
) {
    Diagnostics diagnostics;
    std::set<std::string> nodeIds;

    for (const auto& node : state.nodes) {
        if (!nodeIds.insert(node.id).second) {
            diagnostics.push_back({ "graph.node.duplicate_id", "Duplicate node id: " + node.id + "." });
        }

        const auto spec = registry.findSpec(node.type);
        if (!spec.has_value()) {
            diagnostics.push_back({
                "graph.node.unknown_type",
                "Unknown node type for node " + node.id + ": " + node.type + "."
            });
            continue;
        }

        for (const auto& param : node.params) {
            const auto paramSpec = registry.findParam(node.type, param.first);
            if (!paramSpec.has_value()) {
                diagnostics.push_back({
                    "graph.param.unknown",
                    "Unknown param on node " + node.id + ": " + param.first + "."
                });
                continue;
            }

            if (!paramValueMatchesType(param.second, paramSpec->type)) {
                diagnostics.push_back({
                    "graph.param.type_mismatch",
                    "Param type mismatch on node " + node.id + "." + param.first
                        + ": expected " + paramSpec->type + "."
                });
            }
        }

        for (const auto& paramSpec : spec->params) {
            if (!paramSpec.saved) {
                continue;
            }
            if (node.params.count(paramSpec.id) == 0) {
                diagnostics.push_back({
                    "graph.param.missing",
                    "Missing param on node " + node.id + ": " + paramSpec.id + "."
                });
            }
        }
    }

    for (const auto& edge : state.edges) {
        const auto* fromNode = findNode(state, edge.from.nodeId);
        const auto* toNode = findNode(state, edge.to.nodeId);
        if (fromNode == nullptr || toNode == nullptr) {
            diagnostics.push_back({
                "graph.edge.missing_node",
                "Edge endpoint node does not exist for edge " + edge.id + ": "
                    + portRefToString(edge.from) + " -> " + portRefToString(edge.to) + "."
            });
            continue;
        }

        const auto fromPort = registry.findOutput(fromNode->type, edge.from.port);
        const auto toPort = registry.findInput(toNode->type, edge.to.port);
        if (!fromPort.has_value() || !toPort.has_value()) {
            diagnostics.push_back({
                "graph.edge.missing_port",
                "Edge endpoint port does not exist for edge " + edge.id + ": "
                    + portRefToString(edge.from) + " -> " + portRefToString(edge.to) + "."
            });
            continue;
        }

        if (fromPort->type != toPort->type) {
            diagnostics.push_back({
                "graph.edge.type_mismatch",
                "Edge endpoint port types do not match for edge " + edge.id + ": "
                    + portRefToString(edge.from) + " is " + fromPort->type
                    + ", " + portRefToString(edge.to) + " is " + toPort->type + "."
            });
        }
    }

    return diagnostics;
}

} // namespace simple_world::graph
