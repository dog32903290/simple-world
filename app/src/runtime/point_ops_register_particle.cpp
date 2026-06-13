// runtime/point_ops_register_particle — per-family registrar for PARTICLE ops (stateful GPU
// sim). Split from point_ops.cpp's central registerBuiltinPointOps (node_registry.cpp pattern,
// ARCHITECTURE rule 7). Adding a particle op edits ONLY this file. Central builder unchanged.
//
// Zero behaviour change: op name + cook/state bindings verbatim from the original central
// function (cookParticleSim/simStateNew/simStateFree are inline in point_ops.cpp, declared in
// point_ops.h; particlePoolCount is the pool-sizing hook).
#include "runtime/particle_params.h"  // particlePoolCount
#include "runtime/point_graph.h"      // registerPointOp
#include "runtime/point_ops.h"        // cookParticleSim / simStateNew / simStateFree

namespace sw {

void registerParticlePointOps() {
  // ParticleSystem grows a particle POOL (particlePoolCount) larger than its emit ring so the
  // cycle buffer can rotate and recycle (the batch-6 decay fix). The pool is what its output +
  // persistent particle buffer size to; emit count reaches cook() via inputCounts[0].
  registerPointOp("ParticleSystem", cookParticleSim, simStateNew, simStateFree, &particlePoolCount);
}

}  // namespace sw
