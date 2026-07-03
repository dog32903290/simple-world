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

// Parity-golden -bug latches (family convention: radialBakedBugForceForTest / meshInjectBug). Each
// corrupts the REAL cook path's param marshaling — a value deviation of the historically-plausible
// kind — so the parity goldens' injectBug legs bite the true cook, not a flipped expectation
// (GOLDEN_STANDARD.md 特徵3). Off (false) in production; goldens set + reset around ONE cook.
bool& axisStepSelectRatioBugForTest();  // fillAxisStepForceParams: SelectRatio -> 1.0 ("every particle hit")
bool& dirForceAmountBugForTest();       // cookParticleSim DIRECTIONAL fill: Amount *= 15 (TurbulenceForce-style drift)
bool& turbAmountBugForTest();            // cookParticleSim TURBULENCE fill: Amount *= 15 (NodeSpec drift, same class)
bool& vecFieldAmountBugForTest();       // cookParticleSim VECTORFIELD fill: Amount *= 15 (same drift class)
bool& particleSimDragBugForTest();      // cookParticleSim integrator: Drag -> 0.5 (integrator-param drift)
bool& fieldDistBakedPushBugForTest();   // cookParticleSim FIELDDISTANCE no-field fallback: dispatch a phantom
                                        // directional push instead of the faithful baked no-op (no-op drift)
float& snapAnglesBugAngleCountForTest(); // fillSnapAnglesForceParams: AngleCount -> this value (NodeSpec drift)
// Parity-golden velocity-seed latch (defined in point_ops.cpp next to cookParticleSim): the cook bakes
// InitialVelocity = 0 (PS motion comes from wired forces), so a velocity-TRANSFORM force (SnapToAngles)
// is a structural no-op through the cook. The snaptoangles golden latches a non-zero emit speed so the
// snap becomes observable through the REAL cook. 0.0f in production.
float& simEmitVelocityForTest();
}  // namespace sw
