#include "GraphCommandDispatcher.hpp"

#include <algorithm>
#include <set>
#include <type_traits>

namespace simple_world::graph {

namespace {

std::string makeEdgeId(const PortRef& from, const PortRef& to) {
    return from.nodeId + "." + from.port + "->" + to.nodeId + "." + to.port;
}

bool removeAttachedEdges(GraphState& state, const std::set<std::string>& removedNodeIds) {
    const auto before = state.edges.size();
    state.edges.erase(
        std::remove_if(state.edges.begin(), state.edges.end(), [&](const Edge& edge) {
            return removedNodeIds.count(edge.from.nodeId) > 0 || removedNodeIds.count(edge.to.nodeId) > 0;
        }),
        state.edges.end()
    );
    return state.edges.size() != before;
}

} // namespace

DispatchResult dispatchGraphCommand(
    const GraphState& state,
    const GraphCommand& command,
    const simple_world::nodes::NodeSpecRegistry& registry
) {
    GraphState next = state;
    Diagnostics diagnostics;

    std::visit([&](const auto& typedCommand) {
        using CommandType = std::decay_t<decltype(typedCommand)>;

        if constexpr (std::is_same_v<CommandType, CreateNodeCommand>) {
            if (findNode(next, typedCommand.nodeId) != nullptr) {
                diagnostics.push_back({ "graph.node.duplicate_id", "Node id already exists." });
                return;
            }

            const auto spec = registry.findSpec(typedCommand.nodeType);
            if (!spec.has_value()) {
                diagnostics.push_back({ "graph.node.unknown_type", "Node type is not registered." });
                return;
            }

            const auto maturity = registry.maturityFor(typedCommand.nodeType);
            if (!simple_world::nodes::canEnterEditorGraph(maturity)
                && maturity.level != simple_world::nodes::MaturityLevel::RuntimeReady) {
                diagnostics.push_back({ "graph.node.not_interaction_ready", "Node is not ready for editor graph creation." });
                return;
            }

            NodeInstance node;
            node.id = typedCommand.nodeId;
            node.type = typedCommand.nodeType;
            node.position = typedCommand.position;
            for (const auto& paramSpec : spec->params) {
                node.params[paramSpec.id] = paramSpec.defaultValue;
            }

            next.nodes.push_back(node);
            next.runtimeDirty = true;
        } else if constexpr (std::is_same_v<CommandType, SelectNodeCommand>) {
            if (findNode(next, typedCommand.nodeId) == nullptr) {
                diagnostics.push_back({ "graph.selection.missing_node", "Selected node does not exist." });
                return;
            }

            if (typedCommand.mode == "replace") {
                next.selectedNodeIds.clear();
            }
            next.selectedNodeIds.insert(typedCommand.nodeId);
        } else if constexpr (std::is_same_v<CommandType, MoveNodeCommand>) {
            auto* node = findNode(next, typedCommand.nodeId);
            if (node == nullptr) {
                diagnostics.push_back({ "graph.move.missing_node", "Moved node does not exist." });
                return;
            }

            node->position = typedCommand.position;
        } else if constexpr (std::is_same_v<CommandType, BeginCableDragCommand>) {
            next.cableDrag = CableDragState{ typedCommand.from, std::nullopt };
        } else if constexpr (std::is_same_v<CommandType, HoverPortCommand>) {
            if (next.cableDrag.has_value()) {
                next.cableDrag->hover = typedCommand.port;
            }
        } else if constexpr (std::is_same_v<CommandType, CommitCableDragCommand>) {
            if (!next.cableDrag.has_value()) {
                diagnostics.push_back({ "graph.edge.no_cable_drag", "No cable drag is active." });
                return;
            }

            const auto from = next.cableDrag->from;
            const auto to = typedCommand.to;
            const auto* fromNode = findNode(next, from.nodeId);
            const auto* toNode = findNode(next, to.nodeId);
            if (fromNode == nullptr || toNode == nullptr) {
                diagnostics.push_back({ "graph.edge.missing_node", "Cable endpoint node does not exist." });
                next.cableDrag.reset();
                return;
            }

            const auto fromPort = registry.findOutput(fromNode->type, from.port);
            const auto toPort = registry.findInput(toNode->type, to.port);
            if (!fromPort.has_value() || !toPort.has_value()) {
                diagnostics.push_back({ "graph.edge.missing_port", "Cable endpoint port does not exist." });
                next.cableDrag.reset();
                return;
            }

            if (fromPort->type != toPort->type) {
                diagnostics.push_back({ "graph.edge.type_mismatch", "Cable endpoint port types do not match." });
                next.cableDrag.reset();
                return;
            }

            next.edges.push_back(Edge{ makeEdgeId(from, to), from, to, fromPort->type });
            next.cableDrag.reset();
            next.runtimeDirty = true;
        } else if constexpr (std::is_same_v<CommandType, CancelCableDragCommand>) {
            next.cableDrag.reset();
        } else if constexpr (std::is_same_v<CommandType, DeleteSelectionCommand>) {
            const auto removedNodeIds = next.selectedNodeIds;
            const auto beforeNodes = next.nodes.size();

            next.nodes.erase(
                std::remove_if(next.nodes.begin(), next.nodes.end(), [&](const NodeInstance& node) {
                    return removedNodeIds.count(node.id) > 0;
                }),
                next.nodes.end()
            );
            const bool removedNode = next.nodes.size() != beforeNodes;
            const bool removedEdge = removeAttachedEdges(next, removedNodeIds);

            next.selectedNodeIds.clear();
            if (removedNode || removedEdge) {
                next.runtimeDirty = true;
            }
        } else if constexpr (std::is_same_v<CommandType, SetParameterCommand>) {
            auto* node = findNode(next, typedCommand.nodeId);
            if (node == nullptr) {
                diagnostics.push_back({ "graph.param.missing_node", "Parameter target node does not exist." });
                return;
            }

            if (!registry.findParam(node->type, typedCommand.param).has_value()) {
                diagnostics.push_back({ "graph.param.unknown", "Parameter is not registered for node type." });
                return;
            }

            node->params[typedCommand.param] = typedCommand.value;
            next.runtimeDirty = true;
        }
    }, command);

    return DispatchResult{ next, diagnostics };
}

} // namespace simple_world::graph
