#include "GraphDocument.hpp"

namespace simple_world::graph {

GraphState createInitialGraphState(const std::string& graphId) {
    GraphState state;
    state.graphId = graphId;
    return state;
}

GraphDocument serializeGraphDocument(const GraphState& state) {
    GraphDocument document;
    document.version = state.version;
    document.graphId = state.graphId;
    document.nodes = state.nodes;
    document.edges = state.edges;
    return document;
}

GraphState deserializeGraphDocument(const GraphDocument& document) {
    GraphState state;
    state.version = document.version;
    state.graphId = document.graphId;
    state.nodes = document.nodes;
    state.edges = document.edges;
    state.selectedNodeIds.clear();
    state.cableDrag.reset();
    state.runtimeDirty = true;
    return state;
}

NodeInstance* findNode(GraphState& state, const std::string& nodeId) {
    for (auto& node : state.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

const NodeInstance* findNode(const GraphState& state, const std::string& nodeId) {
    for (const auto& node : state.nodes) {
        if (node.id == nodeId) {
            return &node;
        }
    }
    return nullptr;
}

} // namespace simple_world::graph
