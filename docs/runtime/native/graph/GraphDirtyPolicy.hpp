#pragma once

#include "GraphCommand.hpp"

namespace simple_world::graph {

bool commandMayChangeRuntimeSemantics(const GraphCommand& command);

} // namespace simple_world::graph
