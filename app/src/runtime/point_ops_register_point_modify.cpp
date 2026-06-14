// runtime/point_ops_register_point_modify — per-family registrar for point MODIFY ops
// (in-bag -> out-bag transforms). Split from point_ops.cpp's central registerBuiltinPointOps
// (node_registry.cpp pattern, ARCHITECTURE rule 7). Adding a point-modify op edits ONLY this
// file: append one register<Name>Op() line + forward-declare it. Central builder unchanged.
//
// Zero behaviour change: op names + cook bindings are verbatim copies of the original central
// function. This is the point-transform lane's hot file (Phase B parallel production).
#include "runtime/point_graph.h"  // registerPointOp (via leaf fns)

namespace sw {

// Leaf register fns (defined in point_ops_<name>.cpp, no header).
void registerTransformPointsOp();
void registerOrientPointsOp();
void registerRandomizePointsOp();
void registerSetPointAttributesOp();
void registerAddNoiseOp();
void registerFilterPointsOp();
void registerPolarTransformPointsOp();
void registerWrapPointsOp();
void registerBoundPointsOp();
void registerTransformSomePointsOp();
void registerWrapPointPositionOp();
void registerSnapToGridOp();
void registerClearSomePointsOp();
void registerReorientLinePointsOp();
void registerSelectPointsOp();
void registerSoftTransformPointsOp();
void registerOffsetPointsOp();
void registerPointAttributeFromNoiseOp();

void registerPointModifyPointOps() {
  registerTransformPointsOp();
  registerOrientPointsOp();
  registerRandomizePointsOp();
  registerSetPointAttributesOp();
  registerAddNoiseOp();
  registerFilterPointsOp();
  registerPolarTransformPointsOp();  // Points → Points (TRS + cartesian->polar warp, lane P, batch 16)
  registerWrapPointsOp();            // Points → Points (floored-mod box wrap, lane P, batch 16)
  registerBoundPointsOp();           // Points → Points (clamp into AABB, lane P, batch 16)
  registerTransformSomePointsOp();   // Points → Points (TRS weighted by W channel, lane P, batch 18)
  registerWrapPointPositionOp();     // Points → Points (cube-fold box wrap, batch 19)
  registerSnapToGridOp();            // Points → Points (lerp to grid center, batch 19)
  registerClearSomePointsOp();       // Points → Points (per-point hash kill, batch 20)
  registerReorientLinePointsOp();    // Points → Points (align rotation to line tangent, batch 21)
  registerSelectPointsOp();          // Points → Points (volume selection -> FX1/FX2, batch 21)
  registerSoftTransformPointsOp();   // Points → Points (volume falloff soft transform, batch 21)
  registerOffsetPointsOp();          // Points → Points (offset along Dir*Dist rotated by point Rotation, batch 24)
  registerPointAttributeFromNoiseOp();// Points → Points (3D noise -> position/rotation attributes, batch 24)
}

}  // namespace sw
