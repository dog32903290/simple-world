#pragma once
// point_ops_forceparams.h — cook-core internal API for cookParticleSim's per-force-kind b0 param-fill.
// Implemented in point_ops_forceparams.cpp; consumed only by point_ops.cpp (cookParticleSim). Peeled out
// of point_ops.cpp/point_ops.h when RandomJumpForce landed, to keep both under their line-count caps
// (ARCHITECTURE.md rule 4). Each helper is pure value marshaling: cookInputParam on input 1 (the wired
// force) + the TiXL .t3 defaults, byte-identical to the formerly-inline blocks.
#include <cstdint>

#include "runtime/particle_params.h"  // Vel/AxisStep/SnapAngles/FieldVolumeForceParams (via force_params.h)
#include "runtime/point_graph.h"      // PointCookCtx

namespace sw {
VelForceParams fillVelForceParams(const PointCookCtx& c, uint32_t pool);
AxisStepForceParams fillAxisStepForceParams(const PointCookCtx& c, uint32_t pool);
SnapAnglesForceParams fillSnapAnglesForceParams(const PointCookCtx& c, uint32_t pool);
FieldVolumeForceParams fillFieldVolumeForceParams(const PointCookCtx& c, uint32_t pool);
}  // namespace sw
