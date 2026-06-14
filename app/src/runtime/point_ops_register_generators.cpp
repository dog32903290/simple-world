// runtime/point_ops_register_generators — per-family registrar for point GENERATOR ops.
// Split from point_ops.cpp's central registerBuiltinPointOps (node_registry.cpp pattern,
// ARCHITECTURE rule 7). Adding a generator op edits ONLY this file: append one
// register<Name>Op() line + forward-declare it. The central builder never changes.
//
// Zero behaviour change: op names + cook bindings are verbatim copies of the original
// central function (RadialPoints inline via point_ops.h; the rest in their leaf files).
#include "runtime/point_graph.h"  // registerPointOp
#include "runtime/point_ops.h"    // cookRadialPoints (inline cook in point_ops.cpp)

namespace sw {

// Leaf register fns (defined in point_ops_<name>.cpp, no header).
void registerLinePointsOp();
void registerGridPointsOp();
void registerSpherePointsOp();
void registerHexGridPointsOp();
void registerDoyleSpiralPointsOp();
void registerRepetitionPointsOp();
void registerCommonPointSetsOp();

void registerGeneratorPointOps() {
  registerPointOp("RadialPoints", cookRadialPoints);
  registerLinePointsOp();
  registerGridPointsOp();
  registerSpherePointsOp();
  registerHexGridPointsOp();  // (generator) hex tiling grid, batch 19
  registerDoyleSpiralPointsOp();  // (generator) Doyle circle-packing spiral
  registerRepetitionPointsOp();  // (generator) GPU fork of CPU RepetitionPoints, batch 36
  registerCommonPointSetsOp();  // (generator) CPU-fill fork of CommonPointSets, batch 37
}

}  // namespace sw
