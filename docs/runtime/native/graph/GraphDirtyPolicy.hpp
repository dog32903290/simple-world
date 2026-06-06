#pragma once

#include "GraphCommand.hpp"

namespace simple_world::graph {

bool commandChangesRuntimeSemantics(const GraphCommand& command);

} // namespace simple_world::graph
