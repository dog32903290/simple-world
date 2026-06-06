#include "NodeSpecRegistry.hpp"

namespace simple_world::nodes {

NodeSpecRegistry NodeSpecRegistry::createSeedRegistry() {
    NodeSpecRegistry registry;

    registry.specs.push_back(NodeSpec{
        "tixl.field.generate.sdf.SphereSDF",
        "SphereSDF",
        {},
        { PortSpec{ "result", "SdfField", true } },
        { ParamSpec{ "radius", "float", 1.0, "NodeInstance", "runtime", true } },
        true
    });

    registry.specs.push_back(NodeSpec{
        "tixl.field.render.RaymarchField",
        "RaymarchField",
        {
            PortSpec{ "sdfField", "SdfField", true },
            PortSpec{ "color", "Color", false }
        },
        { PortSpec{ "image", "Texture2D", true } },
        {},
        true
    });

    return registry;
}

std::optional<NodeSpec> NodeSpecRegistry::findSpec(const std::string& type) const {
    for (const auto& spec : specs) {
        if (spec.type == type) {
            return spec;
        }
    }
    return std::nullopt;
}

std::optional<PortSpec> NodeSpecRegistry::findInput(const std::string& type, const std::string& port) const {
    const auto spec = findSpec(type);
    if (!spec.has_value()) {
        return std::nullopt;
    }
    for (const auto& input : spec->inputs) {
        if (input.id == port) {
            return input;
        }
    }
    return std::nullopt;
}

std::optional<PortSpec> NodeSpecRegistry::findOutput(const std::string& type, const std::string& port) const {
    const auto spec = findSpec(type);
    if (!spec.has_value()) {
        return std::nullopt;
    }
    for (const auto& output : spec->outputs) {
        if (output.id == port) {
            return output;
        }
    }
    return std::nullopt;
}

std::optional<ParamSpec> NodeSpecRegistry::findParam(const std::string& type, const std::string& param) const {
    const auto spec = findSpec(type);
    if (!spec.has_value()) {
        return std::nullopt;
    }
    for (const auto& paramSpec : spec->params) {
        if (paramSpec.id == param) {
            return paramSpec;
        }
    }
    return std::nullopt;
}

NodeMaturity NodeSpecRegistry::maturityFor(const std::string& type) const {
    if (type == "tixl.field.generate.sdf.SphereSDF" || type == "tixl.field.render.RaymarchField") {
        return NodeMaturity{ MaturityLevel::RuntimeReady, "tests/graph_interaction_contract.test.js" };
    }
    return NodeMaturity{ MaturityLevel::AdmissionReady, "docs/contracts/vuo_node_admission_index.json" };
}

} // namespace simple_world::nodes
