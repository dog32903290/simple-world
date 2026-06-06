#pragma once

#include <string>
#include <vector>

namespace simple_world::graph {

struct Diagnostic {
    std::string code;
    std::string message;
    std::string severity = "error";
};

using Diagnostics = std::vector<Diagnostic>;

inline bool hasErrors(const Diagnostics& diagnostics) {
    for (const auto& diagnostic : diagnostics) {
        if (diagnostic.severity == "error") {
            return true;
        }
    }
    return false;
}

} // namespace simple_world::graph
