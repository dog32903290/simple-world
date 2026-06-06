#pragma once

#include "../graph/GraphDocument.hpp"

#include <string>
#include <vector>

namespace simple_world::nodes {

struct PortSpec {
    std::string id;
    std::string type;
    bool required = false;
};

struct ParamSpec {
    std::string id;
    std::string type;
    simple_world::graph::ParamValue defaultValue;
    std::string owner = "NodeInstance";
    std::string affects = "runtime";
    bool saved = true;
};

struct NodeSpec {
    std::string type;
    std::string label;
    std::vector<PortSpec> inputs;
    std::vector<PortSpec> outputs;
    std::vector<ParamSpec> params;
    bool runtimeSemantic = true;
};

} // namespace simple_world::nodes
