#pragma once

#include "GraphDocument.hpp"

#include <string>
#include <variant>

namespace simple_world::graph {

struct CreateNodeCommand {
    std::string nodeId;
    std::string nodeType;
    Position position;
};

struct SelectNodeCommand {
    std::string nodeId;
    std::string mode = "replace";
};

struct MoveNodeCommand {
    std::string nodeId;
    Position position;
};

struct BeginCableDragCommand {
    PortRef from;
};

struct HoverPortCommand {
    PortRef port;
};

struct CommitCableDragCommand {
    PortRef to;
};

struct CancelCableDragCommand {};

struct DeleteSelectionCommand {};

struct SetParameterCommand {
    std::string nodeId;
    std::string param;
    ParamValue value;
};

using GraphCommand = std::variant<
    CreateNodeCommand,
    SelectNodeCommand,
    MoveNodeCommand,
    BeginCableDragCommand,
    HoverPortCommand,
    CommitCableDragCommand,
    CancelCableDragCommand,
    DeleteSelectionCommand,
    SetParameterCommand
>;

} // namespace simple_world::graph
