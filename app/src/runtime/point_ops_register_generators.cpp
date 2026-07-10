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
void registerBoundingBoxPointsOp();
// (MeshVerticesToPoints flat atom RETIRED 2026-07-10, 廢棄節點退場 — the .t3 compound (guid 2467e1ed…)
//  provides it now; mathv verifies the kernel, t3-meshverticestopoints-retire the takeover. §5.)
void registerPointsOnMeshOp();
void registerPointTrailFastOp();
void registerPointTrailOp();

void registerGeneratorPointOps() {
  registerPointOp("RadialPoints", cookRadialPoints);
  registerLinePointsOp();
  registerGridPointsOp();
  registerSpherePointsOp();
  registerHexGridPointsOp();  // (generator) hex tiling grid, batch 19
  registerDoyleSpiralPointsOp();  // (generator) Doyle circle-packing spiral
  registerRepetitionPointsOp();  // (generator) GPU fork of CPU RepetitionPoints, batch 36
  registerCommonPointSetsOp();  // (generator) CPU-fill fork of CommonPointSets, batch 37
  registerBoundingBoxPointsOp();  // (generator) CPU-readback AABB fork; reads Points -> 1 point, batch 38
  // MeshVerticesToPoints flat atom RETIRED (廢棄節點退場) — the .t3 compound provides it now.
  registerPointsOnMeshOp();  // (generator+Mesh+Texture2D) ★area-weighted surface scatter (consumes meshIdx)
  registerPointTrailFastOp();  // (generate+STATEFUL) ★cross-frame fixed-size trail ring (single kernel)
  registerPointTrailOp();      // (generate+STATEFUL) ★cross-frame trail, 3-pass Clear/Collect/Copy variant
}

}  // namespace sw
