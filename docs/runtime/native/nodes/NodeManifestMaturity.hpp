#pragma once

#include <string>

namespace simple_world::nodes {

enum class MaturityLevel {
    AdmissionReady,
    InteractionReady,
    RuntimeReady,
    NativeExecutable
};

struct NodeMaturity {
    MaturityLevel level = MaturityLevel::AdmissionReady;
    std::string evidence;
};

bool canEnterEditorGraph(const NodeMaturity& maturity);
bool canEnterRuntimeGraph(const NodeMaturity& maturity);
bool canExecuteNatively(const NodeMaturity& maturity);
std::string toString(MaturityLevel level);

} // namespace simple_world::nodes
