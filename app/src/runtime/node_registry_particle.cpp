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
      // TiXL's field is a ShaderGraphNode (no field-graph subsystem here) -> baked f=(1,1,1,1),
      // i.e. a constant (1,1,1) push jittered by Randomize. TiXL's VectorField (ShaderGraphNode)
      // input is OMITTED (no field type on the contract yet). Defaults照 VectorFieldForce.t3:
      // Amount=1.0, Randomize=0.0.
      {"VectorFieldForce",
       "VectorFieldForce",
       {{"force", "force", "ParticleForce", false},
        {"Amount", "Amount", "Float", true, 1.0f, 0.0f, 10.0f},
        {"Randomize", "Randomize", "Float", true, 0.0f, 0.0f, 1.0f},
        {"_ForceKind", "_ForceKind", "Float", true, (float)FORCE_KIND_VECTORFIELD, 0.0f, 2.0f,
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
