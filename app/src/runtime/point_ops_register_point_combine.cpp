// runtime/point_ops_register_point_combine — per-family registrar for point COMBINE ops
// (multi-input -> one bag). Split from point_ops.cpp's central registerBuiltinPointOps
// (node_registry.cpp pattern, ARCHITECTURE rule 7). Adding a combine op edits ONLY this file.
//
// Zero behaviour change: op name + cook binding verbatim from the original central function.
#include "runtime/point_graph.h"  // registerPointOp (via leaf fn)

namespace sw {

// Leaf register fn (defined in point_ops_combinebuffers.cpp, no header).
void registerCombineBuffersOp();

void registerPointCombinePointOps() {
  registerCombineBuffersOp();
}

}  // namespace sw
