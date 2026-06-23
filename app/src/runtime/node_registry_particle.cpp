// runtime/node_registry_particle — NodeSpec table for PARTICLE ops.
// Ops: TurbulenceForce / DirectionalForce / VectorFieldForce (each produces a ParticleForce),
// ParticleSystem (consumes Points + forces). Split from node_registry.cpp (批次16-R, rule 4).
// Phase B "particle-force lane" adds NEW force/system ops here — this file is the extension point.
#include "runtime/node_registry_particle.h"
#include "runtime/graph.h"
#include "runtime/particle_params.h"  // ForceKind (the _ForceKind discriminator defaults)

namespace sw {

const std::vector<NodeSpec>& particleSpecs() {
  static const std::vector<NodeSpec> specs = {
      {"TurbulenceForce",
       "TurbulenceForce",
       {{"force", "force", "ParticleForce", false},
        {"Amount", "Amount", "Float", true, 15.0f, 0.0f, 100.0f},
        {"Frequency", "Frequency", "Float", true, 1.2f, 0.0f, 5.0f},
        {"Phase", "Phase", "Float", true, 0.0f, 0.0f, 10.0f},
        // _ForceKind: the pinless discriminator cookParticleSim reads (particle_params.h
        // ForceKind). Inspector/canvas hide it (pinless); default 0 = Turbulence. NOT a TiXL
        // input — it replaces TiXL's "the wire's source TYPE is the kernel" with a param the
        // graph-agnostic cook can read. fork-FK (named): see particle_params.h ForceKind.
        {"_ForceKind", "_ForceKind", "Float", true, (float)FORCE_KIND_TURBULENCE, 0.0f, 2.0f,
         Widget::Slider, {}, /*pinless=*/true}},
       nullptr},
      // DirectionalForce — TiXL particle/force/DirectionalForce. Adds a constant directional
      // push (+ optional per-particle RandomAmount jitter) to velocity. Direction is a Vec3
      // (3 pinless Float components Direction.x/.y/.z, the head Widget::Vec) = TiXL's Vector3
      // input. Defaults照 DirectionalForce.t3: Direction=(0,-1,0), Amount=0.007, RandomAmount=0.
      {"DirectionalForce",
       "DirectionalForce",
       {{"force", "force", "ParticleForce", false},
        {"Amount", "Amount", "Float", true, 0.007f, 0.0f, 1.0f},
        {"Direction.x", "Direction", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"Direction.y", "Direction.y", "Float", true, -1.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Direction.z", "Direction.z", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"RandomAmount", "RandomAmount", "Float", true, 0.0f, 0.0f, 1.0f},
        {"_ForceKind", "_ForceKind", "Float", true, (float)FORCE_KIND_DIRECTIONAL, 0.0f, 2.0f,
         Widget::Slider, {}, /*pinless=*/true}},
       nullptr},
      // VectorFieldForce — TiXL particle/force/VectorFieldForce. Samples a vector field at each
      // particle's position and pushes along it. fork-VFF (named, see vector_field_force.metal):
      // PF-0: the TiXL VectorField (ShaderGraphNode) input is now a real "VectorField" Field input
      // port (mirrors VectorFieldForce.cs:9-10 InputSlot<ShaderGraphNode>). The cook drivers gather a
      // wired field op (ToroidalVortexField.Result) into PointCookCtx::inputFieldTree; the FORCE KERNEL
      // still bakes f=(1,1,1,1) (the bake is removed in PF-a). So a wired field reaches the cook but is
      // not yet consumed — byte-identical particle motion. Defaults照 VectorFieldForce.t3: Amount=1.0,
      // Randomize=0.0. isBufferInput() skips a "Field" port (Points/ParticleForce only) → no double-
      // count into ins[]; the dedicated field gather is the sole consumer.
      {"VectorFieldForce",
       "VectorFieldForce",
       {{"force", "force", "ParticleForce", false},
        {"VectorField", "VectorField", "Field", true},  // PF-0: TiXL ShaderGraphNode field input
        {"Amount", "Amount", "Float", true, 1.0f, 0.0f, 10.0f},
        {"Randomize", "Randomize", "Float", true, 0.0f, 0.0f, 1.0f},
        {"_ForceKind", "_ForceKind", "Float", true, (float)FORCE_KIND_VECTORFIELD, 0.0f, 2.0f,
         Widget::Slider, {}, /*pinless=*/true}},
       nullptr},
      // VelocityForce — TiXL particle/force/VelocityForce. Reads each particle's CURRENT velocity,
      // keeps its DIRECTION, rescales its SPEED (speed += Accelerate*0.02*strength, clamp[Min,Max]).
      // Stateless (velocity_force.metal). Defaults照 VelocityForce.t3: Amount=1, Accelerate=1,
      // MinSpeed=0, MaxSpeed=1000, Variation=0, VariationGainAndBias=(0.5,0.5). The Vec2
      // VariationGainAndBias is 2 pinless Float components (head Widget::Vec) = TiXL's Vector2.
      {"VelocityForce",
       "VelocityForce",
       {{"force", "force", "ParticleForce", false},
        {"Amount", "Amount", "Float", true, 1.0f, 0.0f, 10.0f},
        {"Accelerate", "Accelerate", "Float", true, 1.0f, -10.0f, 10.0f},
        {"MinSpeed", "MinSpeed", "Float", true, 0.0f, 0.0f, 100.0f},
        {"MaxSpeed", "MaxSpeed", "Float", true, 1000.0f, 0.0f, 1000.0f},
        {"Variation", "Variation", "Float", true, 0.0f, 0.0f, 1.0f},
        {"VariationGainAndBias.x", "VariationGainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
        {"VariationGainAndBias.y", "VariationGainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"_ForceKind", "_ForceKind", "Float", true, (float)FORCE_KIND_VELOCITY, 0.0f, 5.0f,
         Widget::Slider, {}, /*pinless=*/true}},
       nullptr},
      // AxisStepForce — TiXL particle/force/AxisStepForce. Per-particle hash picks a random dominant
      // axis (weighted by AxisDistribution) + signed strength; SelectRatio gates which particles are
      // hit; velocity lerp'd toward (origVel*AddOriginalVelocity + dir*f) by (ApplyTrigger*selected).
      // Stateless (axis_step_force.metal). Defaults照 AxisStepForce.t3: ApplyTrigger=true(1),
      // Strength=1, RandomizeStrength=0, SelectRatio=0.1, AxisDistribution=(1,1,1),
      // AddOriginalVelocity=0, StrengthDistribution=(1,1,1), AxisSpace=0(ObjectSpace), Seed=0.
      // ApplyTrigger/AxisSpace/Seed are Float here (the cook casts; the kernel reads as float).
      {"AxisStepForce",
       "AxisStepForce",
       {{"force", "force", "ParticleForce", false},
        {"ApplyTrigger", "ApplyTrigger", "Float", true, 1.0f, 0.0f, 1.0f},
        {"Strength", "Strength", "Float", true, 1.0f, 0.0f, 10.0f},
        {"RandomizeStrength", "RandomizeStrength", "Float", true, 0.0f, 0.0f, 1.0f},
        {"SelectRatio", "SelectRatio", "Float", true, 0.1f, 0.0f, 1.0f},
        {"AxisDistribution.x", "AxisDistribution", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
        {"AxisDistribution.y", "AxisDistribution.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"AxisDistribution.z", "AxisDistribution.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"AddOriginalVelocity", "AddOriginalVelocity", "Float", true, 0.0f, 0.0f, 1.0f},
        {"StrengthDistribution.x", "StrengthDistribution", "Float", true, 1.0f, 0.0f, 5.0f, Widget::Vec, {}, true, 3},
        {"StrengthDistribution.y", "StrengthDistribution.y", "Float", true, 1.0f, 0.0f, 5.0f, Widget::Vec, {}, true, 1},
        {"StrengthDistribution.z", "StrengthDistribution.z", "Float", true, 1.0f, 0.0f, 5.0f, Widget::Vec, {}, true, 1},
        {"AxisSpace", "AxisSpace", "Float", true, 0.0f, 0.0f, 1.0f},
        {"Seed", "Seed", "Float", true, 0.0f, 0.0f, 1000.0f},
        {"_ForceKind", "_ForceKind", "Float", true, (float)FORCE_KIND_AXISSTEP, 0.0f, 5.0f,
         Widget::Slider, {}, /*pinless=*/true}},
       nullptr},
      // SnapToAnglesForce — TiXL particle/force/SnapToAnglesForce. Quantizes each particle's velocity
      // DIRECTION (projected on a plane) to the nearest of (360/AngleCount) discrete angles, lerp'd by
      // Amount; Twist adds a per-frame phase, KeepPlanar damps the off-plane axis, Variation jitters
      // via a per-particle hash gate. Stateless (snaptoanglesforce.metal). NAMED FORK snapangles-
      // camera: Mode=0(CameraSpace) needs view matrices not in the cook ctx -> baked identity camera,
      // which reduces CameraSpace EXACTLY to WorldXY (full snap math still runs); modes 1/2/3
      // (WorldXY/XZ/YZ) are 1:1. Defaults照 SnapToAnglesForce.t3: Amount=1, AngleCount=45, Twist=0,
      // VariationThreshold=0.1, Variation=0.2, KeepPlanar=0.5, Mode=0. No Seed port (the .cs exposes
      // none — RandomSeed baked 0 in the cook, see point_ops.cpp).
      {"SnapToAnglesForce",
       "SnapToAnglesForce",
       {{"force", "force", "ParticleForce", false},
        {"Amount", "Amount", "Float", true, 1.0f, 0.0f, 1.0f},
        {"AngleCount", "AngleCount", "Float", true, 45.0f, 1.0f, 360.0f},
        {"Twist", "Twist", "Float", true, 0.0f, -360.0f, 360.0f},
        {"VariationThreshold", "VariationThreshold", "Float", true, 0.1f, 0.0f, 1.0f},
        {"Variation", "Variation", "Float", true, 0.2f, 0.0f, 1.0f},
        {"KeepPlanar", "KeepPlanar", "Float", true, 0.5f, 0.0f, 1.0f},
        {"Mode", "Mode", "Float", true, 0.0f, 0.0f, 3.0f},
        {"_ForceKind", "_ForceKind", "Float", true, (float)FORCE_KIND_SNAPANGLES, 0.0f, 5.0f,
         Widget::Slider, {}, /*pinless=*/true}},
       nullptr},
      // FieldDistanceForce — TiXL particle/force/FieldDistanceForce. Samples a wired SDF Field's distance
      // at each particle's position, finite-differences a surface normal, and pushes the particle along it
      // (attract outside / repel inside). The Field input is a real "Field" port (mirrors
      // FieldDistanceForce.cs:9-10 InputSlot<ShaderGraphNode>); the cook drivers gather the wired SDF op tree
      // into PointCookCtx::inputFieldTree and cookParticleSim assembles + compiles the field_distance template
      // (PF bridge). No field wired -> baked no-op (fork-FieldDistance-baked, see field_distance_force.metal).
      // Defaults照 FieldDistanceForce.t3: Amount=1, Attraction=1, Repulsion=1, NormalSamplingDistance=0.01,
      // DecayWithDistance=0. isBufferInput() skips a "Field" port -> the dedicated field gather is the sole
      // consumer (no double-count into ins[]).
      {"FieldDistanceForce",
       "FieldDistanceForce",
       {{"force", "force", "ParticleForce", false},
        {"Field", "Field", "Field", true},  // PF: TiXL ShaderGraphNode SDF field input
        {"Amount", "Amount", "Float", true, 1.0f, 0.0f, 10.0f},
        {"Attraction", "Attraction", "Float", true, 1.0f, 0.0f, 10.0f},
        {"Repulsion", "Repulsion", "Float", true, 1.0f, 0.0f, 10.0f},
        {"NormalSamplingDistance", "NormalSamplingDistance", "Float", true, 0.01f, 0.0f, 1.0f},
        {"DecayWithDistance", "DecayWithDistance", "Float", true, 0.0f, 0.0f, 10.0f},
        {"_ForceKind", "_ForceKind", "Float", true, (float)FORCE_KIND_FIELDDISTANCE, 0.0f, 6.0f,
         Widget::Slider, {}, /*pinless=*/true}},
       nullptr},
      {"ParticleSystem",
       "ParticleSystem",
       {{"emit", "emit", "Points", true},
        {"forces", "forces", "ParticleForce", true},
        {"result", "result", "Points", false},
        {"Speed", "Speed", "Float", true, 1.0f, 0.0f, 3.0f},
        {"Drag", "Drag", "Float", true, 0.02f, 0.0f, 0.2f},
        {"OrientTowardsVelocity", "OrientTowardsVelocity", "Float", true, 0.15f, 0.0f, 1.0f}},
       nullptr},
  };
  return specs;
}

}  // namespace sw
