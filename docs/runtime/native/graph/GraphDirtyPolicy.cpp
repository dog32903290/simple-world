#include "GraphDirtyPolicy.hpp"

#include <type_traits>

namespace simple_world::graph {

bool commandMayChangeRuntimeSemantics(const GraphCommand& command) {
    return std::visit([](const auto& typedCommand) -> bool {
        using CommandType = std::decay_t<decltype(typedCommand)>;
        if constexpr (std::is_same_v<CommandType, CreateNodeCommand>) return true;
        if constexpr (std::is_same_v<CommandType, CommitCableDragCommand>) return true;
        if constexpr (std::is_same_v<CommandType, DeleteSelectionCommand>) return true;
        if constexpr (std::is_same_v<CommandType, SetParameterCommand>) return true;
        return false;
    }, command);
}

} // namespace simple_world::graph
