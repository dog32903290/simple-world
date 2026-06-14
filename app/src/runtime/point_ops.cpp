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

namespace {
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
  MTL::ComputePipelineState* psoVel = nullptr;       // FORCE_KIND_VELOCITY (批次24)
  MTL::ComputePipelineState* psoAxisStep = nullptr;  // FORCE_KIND_AXISSTEP (批次24)
  MTL::ComputePipelineState* psoSnapAngles = nullptr;  // FORCE_KIND_SNAPANGLES (批次24)
  MTL::ComputePipelineState* psoSim = nullptr;
  bool seeded = false;
  uint32_t frame = 0;                // monotonic step head -> CollectCycleIndex
};
}  // namespace (file-local sim helpers: makeComputePSO, SimState)

void* simStateNew(MTL::Device* dev, MTL::Library* lib, uint32_t count) {
  SimState* s = new SimState();
  s->particles = dev->newBuffer((NS::UInteger)(count > 0 ? count : 1) * sizeof(Particle),
                                MTL::ResourceStorageModeShared);
  // One PSO per force kernel — cached once per node, selected per-frame by the wired force's
  // _ForceKind (a force is an input; the system runs whichever kernel it carries).
  s->psoTurb = makeComputePSO(dev, lib, "turbulence_force");
  s->psoDir = makeComputePSO(dev, lib, "directional_force");
  s->psoVecField = makeComputePSO(dev, lib, "vector_field_force");
  s->psoVel = makeComputePSO(dev, lib, "velocity_force");          // 批次24
  s->psoAxisStep = makeComputePSO(dev, lib, "axis_step_force");    // 批次24
  s->psoSnapAngles = makeComputePSO(dev, lib, "snaptoanglesforce"); // 批次24
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
  if (s->psoVel) s->psoVel->release();
  if (s->psoAxisStep) s->psoAxisStep->release();
  if (s->psoSnapAngles) s->psoSnapAngles->release();
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
    } else if (forceKind == FORCE_KIND_VELOCITY) {
      // 批次24 VelocityForce — defaults照 VelocityForce.t3: Amount=1, Accelerate=1, MinSpeed=0,
      // MaxSpeed=1000, Variation=0, VariationGainAndBias=(0.5,0.5).
      VelForceParams vp{};
      vp.Amount = cookInputParam(c, 1, "Amount", 1.0f);
      vp.Accelerate = cookInputParam(c, 1, "Accelerate", 1.0f);
      vp.MinSpeed = cookInputParam(c, 1, "MinSpeed", 0.0f);
      vp.MaxSpeed = cookInputParam(c, 1, "MaxSpeed", 1000.0f);
      vp.Variation = cookInputParam(c, 1, "Variation", 0.0f);
      vp.GainBiasX = cookInputParam(c, 1, "VariationGainAndBias.x", 0.5f);
      vp.GainBiasY = cookInputParam(c, 1, "VariationGainAndBias.y", 0.5f);
      vp.Count = pool;
      runForce(s->psoVel, &vp, sizeof(vp));
    } else if (forceKind == FORCE_KIND_AXISSTEP) {
      // 批次24 AxisStepForce — defaults照 AxisStepForce.t3: ApplyTrigger=true(->1), Strength=1,
      // RandomizeStrength=0, SelectRatio=0.1, AxisDistribution=(1,1,1), AddOriginalVelocity=0,
      // StrengthDistribution=(1,1,1), AxisSpace=0(ObjectSpace), Seed=0.
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
      runForce(s->psoAxisStep, &ap, sizeof(ap));
    } else if (forceKind == FORCE_KIND_SNAPANGLES) {
      // 批次24 SnapToAnglesForce — defaults照 SnapToAnglesForce.t3: Amount=1, AngleCount=45,
      // Twist=0, Variation=0.2, VariationThreshold=0.1, KeepPlanar=0.5, Mode=0(CameraSpace, baked
      // to WorldXY via the named camera fork in snaptoanglesforce.metal), Seed=0.
      SnapAnglesForceParams sp{};
      sp.Amount = cookInputParam(c, 1, "Amount", 1.0f);
      sp.SnapAngle = cookInputParam(c, 1, "AngleCount", 45.0f);
      sp.PhaseAngle = cookInputParam(c, 1, "Twist", 0.0f);
      sp.Variation = cookInputParam(c, 1, "Variation", 0.2f);
      sp.VariationRatio = cookInputParam(c, 1, "VariationThreshold", 0.1f);
      sp.KeepPlanar = cookInputParam(c, 1, "KeepPlanar", 0.5f);
      sp.SpaceAndPlane = cookInputParam(c, 1, "Mode", 0.0f);
      // RandomSeed (.hlsl b1) is NOT an operator input on SnapToAnglesForce (the .cs exposes no
      // Seed slot; the .t3 feeds b1 from an internal child, not the operator surface). No invented
      // port — baked to a fixed 0 (the variation hash is still per-particle via the index).
      sp.RandomSeed = 0.0f;
      sp.Count = pool;
      runForce(s->psoSnapAngles, &sp, sizeof(sp));
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

// Central builder — FROZEN (node_registry.cpp pattern, ARCHITECTURE rule 7). Each line wires
// one per-family registrar (point_ops_register_<family>.cpp). Adding an op to a family touches
// ONLY that family's registrar — never this central function. Family order mirrors
// node_registry.cpp's registry() so any registration-order assumption stays aligned. The three
// inline cooks above (cookRadialPoints / cookParticleSim / cookDrawPoints) are passed by the
// generators / particle / draw registrars via point_ops.h declarations.
void registerBuiltinPointOps() {
  registerGeneratorPointOps();    // RadialPoints, LinePoints, GridPoints, SpherePoints, HexGridPoints
  registerPointModifyPointOps();  // TransformPoints, OrientPoints, RandomizePoints, SetPointAttributes,
                                  // AddNoise, FilterPoints, PolarTransform, Wrap, Bound,
                                  // TransformSomePoints, WrapPointPosition, SnapToGrid
  registerPointCombinePointOps(); // CombineBuffers
  registerParticlePointOps();     // ParticleSystem
  registerDrawPointOps();         // DrawPoints, DrawLines, DrawBillboards, RenderTarget
  registerImageFilterPointOps();  // Blur, Displace, Tint, ChromaticAbberation, AdjustColors
}

}  // namespace sw
