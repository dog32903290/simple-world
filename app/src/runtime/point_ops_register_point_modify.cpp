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
void registerResampleLinePointsOp();
void registerSubdivideLinePointsOp();
void registerSimNoiseOffsetOp();
void registerSimCentricalOffsetOp();
void registerSimDirectionalOffsetOp();
void registerSimForceOffsetOp();
void registerSamplePointColorAttributesOp();
void registerAttributesFromImageChannelsOp();
void registerLinearSamplePointAttributesOp();
void registerMapPointAttributesOp();
void registerTransformPointsFromClipspaceOp();
void registerSamplePointsByCameraDistanceOp();
void registerSortPointsOp();
void registerMoveToSdfOp();

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
  registerResampleLinePointsOp();    // Points → Points (arc-param resample to Count points, batch 36)
  registerSubdivideLinePointsOp();   // Points → Points (per-segment subdivide, InsertCount inserts, batch 37)
  registerSimNoiseOffsetOp();        // Points → Points (sim (simplex|curl)-noise displacement, batch sw-node-batch)
  registerSimCentricalOffsetOp();    // Points → Points (sim radial inverse-power force, batch sw-node-batch)
  registerSimDirectionalOffsetOp();  // Points → Points (sim directional push / velocity encode, batch sw-node-batch)
  registerSimForceOffsetOp();        // Points → Points (sim radial force + gravity window, batch sw-node-batch)
  registerSamplePointColorAttributesOp();  // Points → Points (sample texture into Color; texture-into-points seam)
  registerAttributesFromImageChannelsOp(); // Points → Points (route texture channels into attributes; texture-into-points seam)
  registerLinearSamplePointAttributesOp(); // Points → Points (sample texture by point index, route channels into attributes; texture-into-points seam)
  registerMapPointAttributesOp();          // Points → Points (bake host Curve/Gradient into scratch tex, sample per point; bake-into-point seam)
  registerTransformPointsFromClipspaceOp();  // Points → Points (unproject via CameraToWorld; camera-matrix-into-points seam)
  registerSamplePointsByCameraDistanceOp();  // Points → Points (scale W by camera-depth WForDistance curve; camera-matrix + bake-into-point seams)
  registerSortPointsOp();                    // Points → Points (reorder by camera-distance; camera-matrix-into-points seam, converged-sort fork)
  registerMoveToSdfOp();                     // Points → Points (raymarch each point to a wired SDF surface; SDF point-modify seam, direct-Field gather)
}

}  // namespace sw
