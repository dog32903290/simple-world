// runtime/point_modify_op_registry — the point-modify self-registration sink + RAII registrar.
// Meyers singleton (init-order safe, same as string_op_registry.cpp / image-filter sinks).
#include "runtime/point_modify_op_registry.h"

#include <utility>

namespace sw {

std::vector<NodeSpec>& pointModifySpecSink() {
  static std::vector<NodeSpec> v;
  return v;
}

PointModifyOp::PointModifyOp(NodeSpec spec) { pointModifySpecSink().push_back(std::move(spec)); }

}  // namespace sw
