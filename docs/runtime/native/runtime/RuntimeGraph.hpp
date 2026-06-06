#pragma once

#include "../graph/GraphDocument.hpp"

#include <string>
#include <vector>

namespace simple_world::runtime {

struct RuntimeNode {
    std::string id;
    std::string type;
};

struct RuntimeEdge {
    simple_world::graph::PortRef from;
    simple_world::graph::PortRef to;
    std::string type;
};

struct RuntimeGraph {
    std::string graphId;
    std::vector<RuntimeNode> nodes;
    std::vector<RuntimeEdge> edges;
    std::vector<std::string> cookOrder;
};

} // namespace simple_world::runtime
