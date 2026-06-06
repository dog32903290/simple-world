#include "NodeManifestMaturity.hpp"

namespace simple_world::nodes {

bool canEnterEditorGraph(const NodeMaturity& maturity) {
    return maturity.level == MaturityLevel::InteractionReady
        || maturity.level == MaturityLevel::RuntimeReady
        || maturity.level == MaturityLevel::NativeExecutable;
}

bool canEnterRuntimeGraph(const NodeMaturity& maturity) {
    return maturity.level == MaturityLevel::RuntimeReady
        || maturity.level == MaturityLevel::NativeExecutable;
}

bool canExecuteNatively(const NodeMaturity& maturity) {
    return maturity.level == MaturityLevel::NativeExecutable;
}

std::string toString(MaturityLevel level) {
    switch (level) {
        case MaturityLevel::AdmissionReady: return "admissionReady";
        case MaturityLevel::InteractionReady: return "interactionReady";
        case MaturityLevel::RuntimeReady: return "runtimeReady";
        case MaturityLevel::NativeExecutable: return "nativeExecutable";
    }
    return "admissionReady";
}

} // namespace simple_world::nodes
