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

float paramOr(const Node* n, const char* id, float def) {
  if (!n) return def;
  auto it = n->params.find(id);
  return it != n->params.end() ? it->second : def;
}

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

  const Node* n = c.graph ? c.graph->node(c.nodeId) : nullptr;
  RadialParams P{};
  P.Count = c.count;
  P.Radius = paramOr(n, "Radius", 2.0f);
  P.RadiusOffset = paramOr(n, "RadiusOffset", 0.0f);
  P.StartAngle = paramOr(n, "StartAngle", 0.0f);
  P.Cycles = paramOr(n, "Cycles", 1.0f);
  P.ScaleBase = 1.0f;
  P.ScaleByF = 0.0f;
  float center[3] = {0.0f, 0.0f, 0.0f};
  if (n) readVecN(*n, "Center", center, 3, center);  // TiXL Center (Vector3), per-node
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
  MTL::Buffer* particles = nullptr;  // Particle[count], persists across frames
  MTL::ComputePipelineState* psoTurb = nullptr;
  MTL::ComputePipelineState* psoSim = nullptr;
  bool seeded = false;
};
void* simStateNew(MTL::Device* dev, MTL::Library* lib, uint32_t count) {
  SimState* s = new SimState();
  s->particles = dev->newBuffer((NS::UInteger)(count > 0 ? count : 1) * sizeof(Particle),
                                MTL::ResourceStorageModeShared);
  s->psoTurb = makeComputePSO(dev, lib, "turbulence_force");
  s->psoSim = makeComputePSO(dev, lib, "particle_sim");
  return s;
}
void simStateFree(void* p) {
  SimState* s = static_cast<SimState*>(p);
  if (!s) return;
  if (s->particles) s->particles->release();
  if (s->psoTurb) s->psoTurb->release();
  if (s->psoSim) s->psoSim->release();
  delete s;
}

// ParticleSystem sim op: emit bag (input[0], from RadialPoints) -> persistent particles ->
// per-frame turbulence + integrate -> result bag (output). Faithful to ParticleSystem's
// runTurbulence + runSim (same params/kernels). Speed/Drag + turbulence are read by TYPE via
// evalParam — exactly as the current live loop does (TurbulenceForce as a real ParticleForce
// buffer is a future refinement; reading-by-type matches today = no regression).
void cookParticleSim(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.state || !c.ctx) return;
  SimState* s = static_cast<SimState*>(c.state);
  if (!s->psoTurb || !s->psoSim || !s->particles) return;
  const MTL::Buffer* emit = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!emit) return;  // no emit bag -> nothing to seed/sim

  const float speed = c.graph ? evalParam(*c.graph, "ParticleSystem", "Speed", *c.ctx, 1.0f, c.reg) : 1.0f;
  const float drag = c.graph ? evalParam(*c.graph, "ParticleSystem", "Drag", *c.ctx, 0.02f, c.reg) : 0.02f;
  const float turbAmt = c.graph ? evalParam(*c.graph, "TurbulenceForce", "Amount", *c.ctx, 15.0f, c.reg) : 15.0f;
  const float turbFreq = c.graph ? evalParam(*c.graph, "TurbulenceForce", "Frequency", *c.ctx, 1.2f, c.reg) : 1.2f;
  const float time = c.ctx->time;
  const uint32_t count = c.count;
  const uint32_t tg = 64;

  auto integrate = [&](bool emitFlag, bool resetFlag) {
    SimParams P{};
    P.Speed = speed; P.Drag = drag; P.InitialVelocity = 0.0f; P.Time = time;
    P.OrientTowardsVelocity = 0.15f; P.RadiusFromW = 0.01f; P.LifeTime = -1.0f;
    SimIntParams I{};
    I.TriggerEmit = emitFlag ? 1 : 0; I.TriggerReset = resetFlag ? 1 : 0;
    I.CollectCycleIndex = 0; I.SetFx1To = 0; I.SetFx2To = 0; I.EmitMode = 0;
    I.IsAutoCount = 1; I.EmitVelocityFactor = 0;
    I.EmitCount = (int32_t)count; I.MaxParticleCount = (int32_t)count;
    MTL::CommandBuffer* cmd = c.queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(s->psoSim);
    enc->setBuffer(const_cast<MTL::Buffer*>(emit), 0, SIM_EmitPoints);
    enc->setBuffer(s->particles, 0, SIM_Particles);
    enc->setBuffer(c.output, 0, SIM_ResultPoints);
    enc->setBytes(&P, sizeof(P), SIM_Params);
    enc->setBytes(&I, sizeof(I), SIM_IntParams);
    enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(count, tg), 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding(); cmd->commit(); cmd->waitUntilCompleted();
  };

  if (!s->seeded) {
    integrate(/*emit=*/true, /*reset=*/true);  // seed particles + result from the emit bag
    s->seeded = true;
    return;
  }
  TurbParams tp{};  // turbulence: vel += curlNoise
  tp.Amount = turbAmt; tp.Frequency = turbFreq; tp.Phase = time; tp.Variation = 0.0f;
  tp.SpeedFactor = 1.0f; tp.VariationGroupCount = 0.0f; tp.Count = count;
  {
    MTL::CommandBuffer* cmd = c.queue->commandBuffer();
    MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
    enc->setComputePipelineState(s->psoTurb);
    enc->setBuffer(s->particles, 0, FORCE_Particles);
    enc->setBytes(&tp, sizeof(tp), FORCE_Params);
    enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(count, tg), 1, 1), MTL::Size::Make(tg, 1, 1));
    enc->endEncoding(); cmd->commit(); cmd->waitUntilCompleted();
  }
  integrate(/*emit=*/false, /*reset=*/false);  // drag + integrate -> result
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

void registerBuiltinPointOps() {
  registerPointOp("RadialPoints", cookRadialPoints);
  registerPointOp("ParticleSystem", cookParticleSim, simStateNew, simStateFree);
  registerCmdOp("DrawPoints", cookDrawPoints);  // Points → Command (was a draw op)
  registerRenderTargetOp();                     // Command → Texture2D (the resolution pin)
  registerLinePointsOp();
  registerGridPointsOp();
  registerSpherePointsOp();
  registerTransformPointsOp();
  registerOrientPointsOp();
  registerRandomizePointsOp();
  registerSetPointAttributesOp();
  registerCombineBuffersOp();
  // A.2+ register here: more generators / modifiers ...
}

}  // namespace sw
