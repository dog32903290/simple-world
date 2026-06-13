#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"        // calcDispatchCount
#include "runtime/graph.h"           // Graph/Node/findSpec/pinId
#include "runtime/particle_params.h" // RadialParams, RadialBinding
#include "runtime/point_graph.h"     // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"      // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// RadialPoints generator: dispatch the radial_points kernel into the node's output bag.
// Reads the Float params it has today (Count via ctx.count; Radius/RadiusOffset/StartAngle/
// Cycles from the node) + Center via readVecN (first vector param on the contract). TiXL's
// remaining vector params (Axis/Color) + quat orientation are still baked to TiXL defaults
// in the kernel until they are added the same way.
// NOTE: builds the PSO per cook — fine for the headless golden (one cook). The live loop
// (A1.5) must cache PSOs; flagged there, not here.
void cookRadialPoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  MTL::Function* fn = c.lib->newFunction(NS::String::string("radial_points", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  RadialParams P{};
  P.Count = c.count;
  P.Radius = cookParam(c, "Radius", 2.0f);
  P.RadiusOffset = cookParam(c, "RadiusOffset", 0.0f);
  P.StartAngle = cookParam(c, "StartAngle", 0.0f);
  P.Cycles = cookParam(c, "Cycles", 1.0f);
  P.ScaleBase = 1.0f;
  P.ScaleByF = 0.0f;
  float center[3] = {0.0f, 0.0f, 0.0f};
  cookVecN(c, "Center", center, 3, center);  // TiXL Center (Vector3), per-node
  P.CenterX = center[0]; P.CenterY = center[1]; P.CenterZ = center[2];

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(c.output, 0, RADIAL_Points);
  enc->setBytes(&P, sizeof(P), RADIAL_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// DrawPoints command op (Points → Command): emit a 1-item RenderCommand describing how to
// draw the upstream bag. No render pass here anymore — RenderTarget executes the chain (the
// render pass/pipeline moved to cookRenderTarget). This is TiXL's DrawPoints: Slot<Command>
// out, not pixels. viewExtent is the camera half-extent (== ParticleSystem kRadius*1.75);
// it becomes a DrawPoints param in batch 3's draw-param family.
RenderCommand cookDrawPoints(CmdCookCtx& c) {
  RenderCommand rc;
  if (c.points && c.count > 0)
    rc.items.push_back(RenderDrawItem{c.points, c.count, 3.5f});
  return rc;
}

// Compute PSO from the metallib by function name (the sim op caches its two in per-node
// state so they aren't rebuilt every frame).
MTL::ComputePipelineState* makeComputePSO(MTL::Device* dev, MTL::Library* lib, const char* name) {
  if (!lib) return nullptr;
  MTL::Function* fn = lib->newFunction(NS::String::string(name, NS::UTF8StringEncoding));
  if (!fn) return nullptr;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);
  fn->release();
  return pso;
}

// ParticleSystem sim op state — the stateful part (本質複雜: a GPU sim inside a dataflow
// graph). Persistent particle buffer + the two cached PSOs (turbulence, integrate) + a
// seeded flag. Lives across frames via PointGraph's per-node state.
struct SimState {
  MTL::Buffer* particles = nullptr;  // Particle[poolCount], persists across frames (cycle buffer)
  MTL::ComputePipelineState* psoTurb = nullptr;   // FORCE_KIND_TURBULENCE
  MTL::ComputePipelineState* psoDir = nullptr;    // FORCE_KIND_DIRECTIONAL
  MTL::ComputePipelineState* psoVecField = nullptr;  // FORCE_KIND_VECTORFIELD
  MTL::ComputePipelineState* psoSim = nullptr;
  bool seeded = false;
  uint32_t frame = 0;                // monotonic step head -> CollectCycleIndex
};
void* simStateNew(MTL::Device* dev, MTL::Library* lib, uint32_t count) {
  SimState* s = new SimState();
  s->particles = dev->newBuffer((NS::UInteger)(count > 0 ? count : 1) * sizeof(Particle),
                                MTL::ResourceStorageModeShared);
  // One PSO per force kernel — cached once per node, selected per-frame by the wired force's
  // _ForceKind (a force is an input; the system runs whichever kernel it carries).
  s->psoTurb = makeComputePSO(dev, lib, "turbulence_force");
  s->psoDir = makeComputePSO(dev, lib, "directional_force");
  s->psoVecField = makeComputePSO(dev, lib, "vector_field_force");
  s->psoSim = makeComputePSO(dev, lib, "particle_sim");
  return s;
}
void simStateFree(void* p) {
  SimState* s = static_cast<SimState*>(p);
  if (!s) return;
  if (s->particles) s->particles->release();
  if (s->psoTurb) s->psoTurb->release();
  if (s->psoDir) s->psoDir->release();
  if (s->psoVecField) s->psoVecField->release();
  if (s->psoSim) s->psoSim->release();
  delete s;
}

// ParticleSystem sim op: emit bag (input[0], from RadialPoints) -> persistent particles ->
// per-frame turbulence + integrate -> result bag (output). Faithful to ParticleSystem's
// runTurbulence + runSim (same params/kernels). Speed/Drag come from THIS node's resolved
// params; turbulence params from the node WIRED into the forces input (cookInputParam — the
// 2b seam; replaces the legacy read-by-TYPE evalParam, which breaks under reuse). No force
// wired -> no turbulence pass (TiXL: a force is an input, not an ambient global).
void cookParticleSim(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.state || !c.ctx) return;
  SimState* s = static_cast<SimState*>(c.state);
  if (!s->psoTurb || !s->psoSim || !s->particles) return;
  const MTL::Buffer* emit = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!emit) return;  // no emit bag -> nothing to seed/sim

  const float speed = cookParam(c, "Speed", 1.0f);
  const float drag = cookParam(c, "Drag", 0.02f);
  // forces = buffer input 1 (spec order: emit, forces). Wired iff its params map is present.
  const bool hasForce = c.inputParams && c.inputCount > 1 && c.inputParams[1] != nullptr;
  // _ForceKind tags which kernel the wired force runs (particle_params.h ForceKind). The cook
  // ctx hides node TYPE (ops are graph-agnostic), so the kind travels in the force's param map;
  // absent/0 -> Turbulence (every pre-existing TurbulenceForce graph stays correct).
  const int forceKind = (int)(cookInputParam(c, 1, "_ForceKind", (float)FORCE_KIND_TURBULENCE) + 0.5f);
  const float time = c.ctx->time;
  // pool = this node's (remapped) count; emit ring = the wired emit bag's count. pool > emit
  // is what lets the cycle buffer rotate (particle_params.h: the recycle parity gap).
  const uint32_t pool = c.count;
  const uint32_t emitCount = (c.inputCount > 0 && c.inputCounts) ? c.inputCounts[0] : pool;
  const uint32_t tg = 64;

  auto integrate = [&](bool emitFlag, bool resetFlag) {
    SimParams P{};
    P.Speed = speed; P.Drag = drag; P.InitialVelocity = 0.0f; P.Time = time;
    P.OrientTowardsVelocity = 0.15f; P.RadiusFromW = 0.01f; P.LifeTime = -1.0f;
    // Shared host policy (particle_params.h): per-frame emit + cycle advance + aging.
    SimIntParams I = makeSimIntParams(emitFlag, resetFlag, s->frame, emitCount, pool);
    MTL::CommandBuffer* cmd = c.queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(s->psoSim);
    enc->setBuffer(const_cast<MTL::Buffer*>(emit), 0, SIM_EmitPoints);
    enc->setBuffer(s->particles, 0, SIM_Particles);
    enc->setBuffer(c.output, 0, SIM_ResultPoints);
    enc->setBytes(&P, sizeof(P), SIM_Params);
    enc->setBytes(&I, sizeof(I), SIM_IntParams);
    enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(pool, tg), 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding(); cmd->commit(); cmd->waitUntilCompleted();
  };

  if (!s->seeded) {
    s->frame = 0;
    integrate(/*emit=*/true, /*reset=*/true);  // seed first emit block + reset the pool
    s->seeded = true;
    return;
  }
  ++s->frame;                                  // advance CollectCycleIndex one emit block
  if (hasForce) {  // a force is an input (TiXL): run whichever kernel its _ForceKind selects.
    // Dispatch helper — every force kernel shares the FORCE_Particles/FORCE_Params binding, so
    // only the PSO + the bytes blob differ. SpeedFactor is the system's global speed multiplier
    // (== the legacy turbulence pass's hardcoded 1.0; no field/speed context in the cook).
    auto runForce = [&](MTL::ComputePipelineState* pso, const void* params, size_t size) {
      if (!pso) return;
      MTL::CommandBuffer* cmd = c.queue->commandBuffer();
      MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
      enc->setComputePipelineState(pso);
      enc->setBuffer(s->particles, 0, FORCE_Particles);
      enc->setBytes(params, size, FORCE_Params);
      enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(pool, tg), 1, 1),
                                MTL::Size::Make(tg, 1, 1));
      enc->endEncoding(); cmd->commit(); cmd->waitUntilCompleted();
    };
    if (forceKind == FORCE_KIND_DIRECTIONAL) {
      // Defaults = TiXL DirectionalForce.t3: Direction=(0,-1,0) push down, Amount=0.007.
      DirForceParams dp{};
      dp.DirX = cookInputParam(c, 1, "Direction.x", 0.0f);
      dp.DirY = cookInputParam(c, 1, "Direction.y", -1.0f);
      dp.DirZ = cookInputParam(c, 1, "Direction.z", 0.0f);
      dp.Amount = cookInputParam(c, 1, "Amount", 0.007f);
      dp.RandomAmount = cookInputParam(c, 1, "RandomAmount", 0.0f);
      dp.SpeedFactor = 1.0f; dp.Count = pool;
      runForce(s->psoDir, &dp, sizeof(dp));
    } else if (forceKind == FORCE_KIND_VECTORFIELD) {
      // Defaults = TiXL VectorFieldForce.t3: Amount=1.0, Randomize=0.0.
      VecFieldForceParams vp{};
      vp.Amount = cookInputParam(c, 1, "Amount", 1.0f);
      vp.Variation = cookInputParam(c, 1, "Randomize", 0.0f);  // TiXL slot name "Randomize"
      vp.SpeedFactor = 1.0f; vp.Count = pool;
      runForce(s->psoVecField, &vp, sizeof(vp));
    } else {  // FORCE_KIND_TURBULENCE (default)
      TurbParams tp{};
      tp.Amount = cookInputParam(c, 1, "Amount", 15.0f);
      tp.Frequency = cookInputParam(c, 1, "Frequency", 1.2f);
      tp.Phase = time; tp.Variation = 0.0f;
      tp.SpeedFactor = 1.0f; tp.VariationGroupCount = 0.0f; tp.Count = pool;
      runForce(s->psoTurb, &tp, sizeof(tp));
    }
  }
  integrate(/*emit=*/true, /*reset=*/false);   // per-frame emit + drag/integrate -> result
}

}  // namespace

// Generators ported in their own leaf files (point_ops_<name>.cpp); forward-declared
// here so registerBuiltinPointOps() can wire them. Keeps each operator in one file.
void registerLinePointsOp();
void registerGridPointsOp();
void registerSpherePointsOp();
void registerTransformPointsOp();
void registerOrientPointsOp();
void registerRandomizePointsOp();
void registerSetPointAttributesOp();
void registerCombineBuffersOp();
void registerAddNoiseOp();
void registerFilterPointsOp();
void registerPolarTransformPointsOp();
void registerWrapPointsOp();
void registerBoundPointsOp();
void registerTransformSomePointsOp();
void registerWrapPointPositionOp();
void registerSnapToGridOp();
void registerHexGridPointsOp();
void registerDrawLinesOp();
void registerDrawBillboardsOp();
void registerTintOp();
void registerChromaBAOp();
void registerAdjustColorsOp();

void registerBuiltinPointOps() {
  registerPointOp("RadialPoints", cookRadialPoints);
  // ParticleSystem grows a particle POOL (particlePoolCount) larger than its emit ring so the
  // cycle buffer can rotate and recycle (the batch-6 decay fix). The pool is what its output +
  // persistent particle buffer size to; emit count reaches cook() via inputCounts[0].
  registerPointOp("ParticleSystem", cookParticleSim, simStateNew, simStateFree, &particlePoolCount);
  registerCmdOp("DrawPoints", cookDrawPoints);  // Points → Command (was a draw op)
  registerDrawLinesOp();                        // Points → Command (DrawKind::Lines, lane L)
  registerDrawBillboardsOp();                   // Points → Command (DrawKind::Billboards, lane L)
  registerRenderTargetOp();                     // Command → Texture2D (the resolution pin)
  registerBlurOp();                             // Texture2D → Texture2D (first image filter, lane I)
  registerDisplaceOp();                         // Image + DisplaceMap → Texture2D (lane D2, dual tex in)
  registerTintOp();                             // Texture2D → Texture2D (color tint/remap, lane F3-1)
  registerChromaBAOp();                         // Texture2D → Texture2D (chromatic fringe, lane F3-2)
  registerAdjustColorsOp();                     // Texture2D → Texture2D (color grading, lane F3-3)
  registerLinePointsOp();
  registerGridPointsOp();
  registerSpherePointsOp();
  registerTransformPointsOp();
  registerOrientPointsOp();
  registerRandomizePointsOp();
  registerSetPointAttributesOp();
  registerCombineBuffersOp();
  registerAddNoiseOp();
  registerFilterPointsOp();
  registerPolarTransformPointsOp();  // Points → Points (TRS + cartesian->polar warp, lane P, batch 16)
  registerWrapPointsOp();            // Points → Points (floored-mod box wrap, lane P, batch 16)
  registerBoundPointsOp();           // Points → Points (clamp into AABB, lane P, batch 16)
  registerTransformSomePointsOp();   // Points → Points (TRS weighted by W channel, lane P, batch 18)
  registerWrapPointPositionOp();     // Points → Points (cube-fold box wrap, batch 19)
  registerSnapToGridOp();            // Points → Points (lerp to grid center, batch 19)
  registerHexGridPointsOp();         // (generator) hex tiling grid, batch 19
  // A.2+ register here: more generators / modifiers ...
}

}  // namespace sw
