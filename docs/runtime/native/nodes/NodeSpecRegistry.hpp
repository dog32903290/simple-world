#pragma once

#include "NodeManifestMaturity.hpp"
#include "NodeSpec.hpp"

#include <optional>
#include <string>
#include <vector>

namespace simple_world::nodes {

class NodeSpecRegistry {
public:
    static NodeSpecRegistry createSeedRegistry();

    std::optional<NodeSpec> findSpec(const std::string& type) const;
    std::optional<PortSpec> findInput(const std::string& type, const std::string& port) const;
    std::optional<PortSpec> findOutput(const std::string& type, const std::string& port) const;
    std::optional<ParamSpec> findParam(const std::string& type, const std::string& param) const;
    NodeMaturity maturityFor(const std::string& type) const;

private:
    std::vector<NodeSpec> specs;
};

} // namespace simple_world::nodes
