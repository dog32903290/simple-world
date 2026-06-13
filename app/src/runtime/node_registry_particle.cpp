// runtime/node_registry_particle — NodeSpec table for PARTICLE ops.
// Ops: TurbulenceForce (produces a ParticleForce), ParticleSystem (consumes Points + forces).
// Split from node_registry.cpp (批次16-R, ARCHITECTURE rule 4).
// Phase B "particle-force lane" adds NEW force/system ops here — this file is the extension point.
#include "runtime/node_registry_particle.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& particleSpecs() {
  static const std::vector<NodeSpec> specs = {
      {"TurbulenceForce",
       "TurbulenceForce",
       {{"force", "force", "ParticleForce", false},
        {"Amount", "Amount", "Float", true, 15.0f, 0.0f, 100.0f},
        {"Frequency", "Frequency", "Float", true, 1.2f, 0.0f, 5.0f},
        {"Phase", "Phase", "Float", true, 0.0f, 0.0f, 10.0f}},
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
