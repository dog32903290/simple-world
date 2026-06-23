// point_ops_forceparams.cpp — cook-core helper leaf: per-force-kind b0 param-fill for cookParticleSim.
// PEELED from point_ops.cpp (which sits at the 400-line ARCHITECTURE rule-4 cap) when the RandomJumpForce
// branch landed: the bulky AxisStepForce / SnapToAnglesForce param-fill blocks (~13 lines each of pure
// cookInputParam reads + TiXL .t3 defaults) are mechanical, file-disjoint glue with no SimState dependency,
// so they move here behind small free functions. cookParticleSim keeps the dispatch (it owns SimState +
// the runForce/runFieldForce lambdas); this leaf owns only the value-param marshaling.
//
// ZONE: runtime (pure param marshaling, no Metal objects). Reads PointCookCtx via cookInputParam (the 2b
// seam, input index 1 = the wired force) and returns the packed params struct. The TiXL .t3 defaults are
// the SAME values that lived inline in cookParticleSim — moving them changes no behavior (byte-identical).
#include "runtime/point_ops_forceparams.h"

#include "runtime/particle_params.h"  // VelForceParams, AxisStepForceParams, SnapAnglesForceParams
#include "runtime/point_graph.h"      // PointCookCtx, cookInputParam

namespace sw {

// VelocityForce — defaults照 VelocityForce.t3: Amount=1, Accelerate=1, MinSpeed=0, MaxSpeed=1000,
// Variation=0, VariationGainAndBias=(0.5,0.5).
VelForceParams fillVelForceParams(const PointCookCtx& c, uint32_t pool) {
  VelForceParams vp{};
  vp.Amount = cookInputParam(c, 1, "Amount", 1.0f);
  vp.Accelerate = cookInputParam(c, 1, "Accelerate", 1.0f);
  vp.MinSpeed = cookInputParam(c, 1, "MinSpeed", 0.0f);
  vp.MaxSpeed = cookInputParam(c, 1, "MaxSpeed", 1000.0f);
  vp.Variation = cookInputParam(c, 1, "Variation", 0.0f);
  vp.GainBiasX = cookInputParam(c, 1, "VariationGainAndBias.x", 0.5f);
  vp.GainBiasY = cookInputParam(c, 1, "VariationGainAndBias.y", 0.5f);
  vp.Count = pool;
  return vp;
}

// AxisStepForce — defaults照 AxisStepForce.t3: ApplyTrigger=true(->1), Strength=1, RandomizeStrength=0,
// SelectRatio=0.1, AxisDistribution=(1,1,1), AddOriginalVelocity=0, StrengthDistribution=(1,1,1),
// AxisSpace=0(ObjectSpace), Seed=0.
AxisStepForceParams fillAxisStepForceParams(const PointCookCtx& c, uint32_t pool) {
  AxisStepForceParams ap{};
  ap.ApplyTrigger = cookInputParam(c, 1, "ApplyTrigger", 1.0f);
  ap.Strength = cookInputParam(c, 1, "Strength", 1.0f);
  ap.RandomizeStrength = cookInputParam(c, 1, "RandomizeStrength", 0.0f);
  ap.SelectRatio = cookInputParam(c, 1, "SelectRatio", 0.1f);
  ap.AxisDistributionX = cookInputParam(c, 1, "AxisDistribution.x", 1.0f);
  ap.AxisDistributionY = cookInputParam(c, 1, "AxisDistribution.y", 1.0f);
  ap.AxisDistributionZ = cookInputParam(c, 1, "AxisDistribution.z", 1.0f);
  ap.AddOriginalVelocity = cookInputParam(c, 1, "AddOriginalVelocity", 0.0f);
  ap.StrengthDistributionX = cookInputParam(c, 1, "StrengthDistribution.x", 1.0f);
  ap.StrengthDistributionY = cookInputParam(c, 1, "StrengthDistribution.y", 1.0f);
  ap.StrengthDistributionZ = cookInputParam(c, 1, "StrengthDistribution.z", 1.0f);
  ap.Seed = cookInputParam(c, 1, "Seed", 0.0f);
  ap.AxisSpace = cookInputParam(c, 1, "AxisSpace", 0.0f);
  ap.Count = pool;
  return ap;
}

// SnapToAnglesForce — defaults照 SnapToAnglesForce.t3: Amount=1, AngleCount=45, Twist=0, Variation=0.2,
// VariationThreshold=0.1, KeepPlanar=0.5, Mode=0(CameraSpace, baked to WorldXY via the named camera fork in
// snaptoanglesforce.metal), Seed=0. RandomSeed (.hlsl b1) is NOT an operator input (the .cs exposes no Seed
// slot; the .t3 feeds b1 from an internal child, not the operator surface) -> baked to a fixed 0 (no invented
// port; the variation hash is still per-particle via the index).
SnapAnglesForceParams fillSnapAnglesForceParams(const PointCookCtx& c, uint32_t pool) {
  SnapAnglesForceParams sp{};
  sp.Amount = cookInputParam(c, 1, "Amount", 1.0f);
  sp.SnapAngle = cookInputParam(c, 1, "AngleCount", 45.0f);
  sp.PhaseAngle = cookInputParam(c, 1, "Twist", 0.0f);
  sp.Variation = cookInputParam(c, 1, "Variation", 0.2f);
  sp.VariationRatio = cookInputParam(c, 1, "VariationThreshold", 0.1f);
  sp.KeepPlanar = cookInputParam(c, 1, "KeepPlanar", 0.5f);
  sp.SpaceAndPlane = cookInputParam(c, 1, "Mode", 0.0f);
  sp.RandomSeed = 0.0f;
  sp.Count = pool;
  return sp;
}

// FieldVolumeForce — defaults照 FieldVolumeForce.t3: Amount=1, Attraction=0.2, AttractionDecay=0,
// Repulsion=0.1, ReflectOnCollision=true, Bounciness=1, RandomizeBounce=0, RandomizeReflection=0,
// InvertVolume=false, NormalSamplingDistance=0.1, ApplyColorOnCollision=false. The .t3 FloatsToBuffer
// routing forks are applied HERE (host-side) so the kernel reads ready values (see
// field_volume_force_template.metal for the trace):
//   Attraction         = (.cs Attraction) * 0.425   (Multiply node B=0.425 on the Attraction path)
//   InvertVolumeFactor = InvertVolume ? -1 : +1      (BoolToFloat node)
//   SpeedFactor        = 1.0                         (fork-FieldVolume-speedfactor: GetParticleComponents
//                                                     .SpeedFactor is a runtime PS value, not an operator
//                                                     input; == every other force's SpeedFactor)
// EnableBounce/ApplyColorOnCollision are .hlsl b2 ints, carried as 0/1 floats (the .t3 routes the bools
// through BoolToInt). cookInputParam returns floats; bools were stored as 1.0/0.0, so >=0.5 == true.
FieldVolumeForceParams fillFieldVolumeForceParams(const PointCookCtx& c, uint32_t pool) {
  FieldVolumeForceParams fp{};
  fp.Amount = cookInputParam(c, 1, "Amount", 1.0f);
  fp.Attraction = cookInputParam(c, 1, "Attraction", 0.2f) * 0.425f;  // Multiply fork
  fp.AttractionDecay = cookInputParam(c, 1, "AttractionDecay", 0.0f);
  fp.Repulsion = cookInputParam(c, 1, "Repulsion", 0.1f);
  fp.Bounciness = cookInputParam(c, 1, "Bounciness", 1.0f);
  fp.RandomizeBounce = cookInputParam(c, 1, "RandomizeBounce", 0.0f);
  fp.RandomizeReflection = cookInputParam(c, 1, "RandomizeReflection", 0.0f);
  fp.InvertVolumeFactor = (cookInputParam(c, 1, "InvertVolume", 0.0f) >= 0.5f) ? -1.0f : 1.0f;  // BoolToFloat fork
  fp.NormalSamplingDistance = cookInputParam(c, 1, "NormalSamplingDistance", 0.1f);
  fp.SpeedFactor = 1.0f;  // fork-FieldVolume-speedfactor
  fp.EnableBounce = (cookInputParam(c, 1, "ReflectOnCollision", 1.0f) >= 0.5f) ? 1.0f : 0.0f;
  fp.ApplyColorOnCollision = (cookInputParam(c, 1, "ApplyColorOnCollision", 0.0f) >= 0.5f) ? 1.0f : 0.0f;
  fp.Count = pool;
  return fp;
}

}  // namespace sw
