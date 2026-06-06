#include "NodeSpecRegistry.hpp"

namespace simple_world::nodes {

NodeSpecRegistry NodeSpecRegistry::createSeedRegistry() {
    NodeSpecRegistry registry;
    using simple_world::graph::NumericObject;

    registry.specs.push_back(NodeSpec{
        "tixl.field.generate.sdf.SphereSDF",
        "SphereSDF",
        {},
        { PortSpec{ "result", "ShaderGraphNode", true } },
        {
            ParamSpec{ "center", "object", NumericObject{ { "x", 0.0 }, { "y", 0.0 }, { "z", 0.0 } }, "NodeInstance", "runtime", true },
            ParamSpec{ "radius", "float", 0.5, "NodeInstance", "runtime", true }
        },
        true
    });

    registry.specs.push_back(NodeSpec{
        "tixl.field.render.RaymarchField",
        "RaymarchField",
        {
            PortSpec{ "sdfField", "ShaderGraphNode", true },
            PortSpec{ "color", "Color", false }
        },
        { PortSpec{ "shaderCode", "ShaderGraph", true } },
        {
            ParamSpec{ "writeDepth", "bool", true, "NodeInstance", "runtime", true },
            ParamSpec{ "minDistance", "float", 0.002, "NodeInstance", "runtime", true },
            ParamSpec{ "distToColor", "float", 0.15, "NodeInstance", "runtime", true },
            ParamSpec{ "maxSteps", "float", 100.0, "NodeInstance", "runtime", true },
            ParamSpec{ "uvMapping", "float", 1.0, "NodeInstance", "runtime", true },
            ParamSpec{ "stepSize", "float", 1.0, "NodeInstance", "runtime", true },
            ParamSpec{ "textureScale", "float", 1.0, "NodeInstance", "runtime", true },
            ParamSpec{ "specularAA", "float", 0.5, "NodeInstance", "runtime", true },
            ParamSpec{ "color", "object", NumericObject{ { "r", 1.0 }, { "g", 1.0 }, { "b", 1.0 }, { "a", 1.0 } }, "NodeInstance", "runtime", true },
            ParamSpec{ "maxDistance", "float", 300.0, "NodeInstance", "runtime", true },
            ParamSpec{ "ambientOcclusion", "object", NumericObject{ { "x", 0.000001 }, { "y", 0.000001 }, { "z", 0.000001 }, { "w", 1.0 } }, "NodeInstance", "runtime", true },
            ParamSpec{ "normalSamplingD", "float", 0.002, "NodeInstance", "runtime", true },
            ParamSpec{ "aoDistance", "float", 1.0, "NodeInstance", "runtime", true }
        },
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
