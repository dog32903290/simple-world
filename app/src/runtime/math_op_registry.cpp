// runtime/math_op_registry — the math self-registration sink + RAII registrar.
// Meyers singleton (init-order safe, same as point_modify_op_registry.cpp / image-filter sinks).
#include "runtime/math_op_registry.h"

#include <utility>

namespace sw {

std::vector<NodeSpec>& mathSpecSink() {
  static std::vector<NodeSpec> v;
  return v;
}

MathOp::MathOp(NodeSpec spec) { mathSpecSink().push_back(std::move(spec)); }

}  // namespace sw
