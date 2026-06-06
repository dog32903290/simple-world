#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace simple_world::graph {

using NumericArray = std::vector<double>;
using NumericObject = std::map<std::string, double>;
using ParamValue = std::variant<double, bool, std::string, NumericArray, NumericObject>;

struct Position {
    double x = 0.0;
    double y = 0.0;
};

struct PortRef {
    std::string nodeId;
    std::string port;
};

struct NodeInstance {
    std::string id;
    std::string type;
    Position position;
    std::map<std::string, ParamValue> params;
};

struct Edge {
    std::string id;
    PortRef from;
    PortRef to;
    std::string type;
};

struct CableDragState {
    PortRef from;
    std::optional<PortRef> hover;
};

struct GraphState {
    std::string kind = "GraphState";
    std::string version = "0.1";
    std::string graphId = "graph.interaction";
    std::vector<NodeInstance> nodes;
    std::vector<Edge> edges;
    std::set<std::string> selectedNodeIds;
    std::optional<CableDragState> cableDrag;
    bool runtimeDirty = false;
};

struct GraphDocument {
    std::string kind = "GraphDocument";
    std::string version = "0.1";
    std::string graphId = "graph.interaction";
    std::vector<NodeInstance> nodes;
    std::vector<Edge> edges;
};

GraphState createInitialGraphState(const std::string& graphId);
GraphDocument serializeGraphDocument(const GraphState& state);
GraphState deserializeGraphDocument(const GraphDocument& document);
NodeInstance* findNode(GraphState& state, const std::string& nodeId);
const NodeInstance* findNode(const GraphState& state, const std::string& nodeId);

} // namespace simple_world::graph
