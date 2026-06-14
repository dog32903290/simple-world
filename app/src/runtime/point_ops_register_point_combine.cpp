// runtime/point_ops_register_point_combine — per-family registrar for point COMBINE ops
// (multi-input -> one bag). Split from point_ops.cpp's central registerBuiltinPointOps
// (node_registry.cpp pattern, ARCHITECTURE rule 7). Adding a combine op edits ONLY this file.
//
// Zero behaviour change: op name + cook binding verbatim from the original central function.
#include "runtime/point_graph.h"  // registerPointOp (via leaf fn)

namespace sw {

// Leaf register fns (defined in their respective leaf cpp files, no shared header).
void registerCombineBuffersOp();
void registerSnapToPointsOp();      // batch 21: index-paired Points1->Points2 lerp
void registerPairPointsForLinesOp();  // batch 24: pair A+B with NaN divider -> DrawLines
void registerPickPointListOp();       // batch 24: multi-input select by Index

void registerPointCombinePointOps() {
  registerCombineBuffersOp();
  registerSnapToPointsOp();
  registerPairPointsForLinesOp();
  registerPickPointListOp();
}

}  // namespace sw
