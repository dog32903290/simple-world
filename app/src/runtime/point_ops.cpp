#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"        // calcDispatchCount
#include "runtime/field_graph.h"     // assembleFieldMSL, AssembledField (PF-a field-into-force bridge)
#include "runtime/graph.h"           // Graph/Node/findSpec/pinId
#include "runtime/particle_params.h" // RadialParams, RadialBinding
#include "runtime/point_graph.h"     // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/point_ops_forceparams.h"  // fillVel/AxisStep/SnapAngles force param-fill helpers
#include "runtime/tex_op_cache.h"    // cachedSourceComputePSO (PF-a: srcHash-keyed compute PSO)
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
// PF-a: load the field-into-force COMPUTE template (SW_VFF_TEMPLATE) once. The string is a compile-time
// asset path (mirrors field_graph_selftest's SW_FIELD_TEMPLATE read); assembleFieldMSL fills its hooks.
// Cached in a function-static so the file is read at most once per process, not per cook. Empty if the
// define is unset or the file is unreadable (-> the cook falls back to the baked path, byte-identical).
const std::string& vffTemplate() {
  static const std::string tmpl = [] {
#ifdef SW_VFF_TEMPLATE
    std::ifstream f(SW_VFF_TEMPLATE);
    if (!f) return std::string();
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
#else
    return std::string();
#endif
  }();
  return tmpl;
}

// PF field-into-force COMPUTE template for FieldDistanceForce (SW_FIELD_DISTANCE_TEMPLATE). Same once-per-
// process function-static read as vffTemplate(); empty if the define is unset/unreadable (-> baked fallback).
const std::string& fieldDistanceTemplate() {
  static const std::string tmpl = [] {
#ifdef SW_FIELD_DISTANCE_TEMPLATE
    std::ifstream f(SW_FIELD_DISTANCE_TEMPLATE);
    if (!f) return std::string();
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
#else
    return std::string();
#endif
  }();
  return tmpl;
}

// PF field-into-force COMPUTE template for RandomJumpForce (SW_RANDOM_JUMP_TEMPLATE). Same once-per-process
// function-static read; empty if the define is unset/unreadable (-> no field path, but RandomJump has no
// baked fallback PSO — a fieldless RandomJumpForce simply does nothing if the template is missing).
const std::string& randomJumpTemplate() {
  static const std::string tmpl = [] {
#ifdef SW_RANDOM_JUMP_TEMPLATE
    std::ifstream f(SW_RANDOM_JUMP_TEMPLATE);
    if (!f) return std::string();
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
#else
    return std::string();
#endif
  }();
  return tmpl;
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
  MTL::ComputePipelineState* psoVel = nullptr;       // FORCE_KIND_VELOCITY (批次24)
  MTL::ComputePipelineState* psoAxisStep = nullptr;  // FORCE_KIND_AXISSTEP (批次24)
  MTL::ComputePipelineState* psoSnapAngles = nullptr;  // FORCE_KIND_SNAPANGLES (批次24)
  MTL::ComputePipelineState* psoFieldDist = nullptr;   // FORCE_KIND_FIELDDISTANCE (baked fallback)
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
  s->psoFieldDist = makeComputePSO(dev, lib, "field_distance_force"); // FieldDistanceForce baked fallback
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
  if (s->psoFieldDist) s->psoFieldDist->release();
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
    // PF field-into-force bridge (shared by VectorFieldForce + FieldDistanceForce): if a Field tree is
    // wired (PF-0 delivered it on inputFieldTree, identically on flat & resident since cookParticleSim is
    // the single shared cook fn), assemble it into the force's COMPUTE template, compile/cache a PSO keyed
    // on srcHash, bind the force's b0 params at FORCE_Params + the field's packed FloatParams at
    // FORCE_FieldParams, and dispatch — GetField then samples the wired field at each particle's raw
    // Position. Returns true iff the runtime-compiled kernel ran; false -> caller runs the baked fallback
    // (byte-identical for every pre-existing fieldless graph). `kernelName` selects which template kernel.
    auto runFieldForce = [&](const std::string& tmpl, const char* kernelName, const void* params,
                             size_t size) -> bool {
      if (!c.inputFieldTree || tmpl.empty()) return false;
      AssembledField asmField = assembleFieldMSL(c.inputFieldTree, tmpl);
      if (asmField.msl.empty()) return false;
      MTL::ComputePipelineState* fieldPso =
          cachedSourceComputePSO(c.dev, asmField.msl.c_str(), asmField.srcHash, kernelName);
      if (!fieldPso) return false;
      // Field FloatParams buffer (rebuilt per cook; cheap). Metal needs a non-null buffer; >=16 bytes.
      const size_t paramBytes =
          asmField.floatParams.empty() ? 16 : asmField.floatParams.size() * sizeof(float);
      MTL::Buffer* fieldBuf = c.dev->newBuffer(paramBytes, MTL::ResourceStorageModeShared);
      if (!fieldBuf) return false;
      if (!asmField.floatParams.empty())
        std::memcpy(fieldBuf->contents(), asmField.floatParams.data(),
                    asmField.floatParams.size() * sizeof(float));
      MTL::CommandBuffer* cmd = c.queue->commandBuffer();
      MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
      enc->setComputePipelineState(fieldPso);
      enc->setBuffer(s->particles, 0, FORCE_Particles);
      enc->setBytes(params, size, FORCE_Params);
      enc->setBuffer(fieldBuf, 0, FORCE_FieldParams);
      enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(pool, tg), 1, 1),
                                MTL::Size::Make(tg, 1, 1));
      enc->endEncoding(); cmd->commit(); cmd->waitUntilCompleted();
      fieldBuf->release();  // contents consumed by the GPU; not needed past the dispatch
      return true;
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
      // PF-a field-into-force bridge (runFieldForce): a wired field -> the runtime-compiled
      // vector_field_force kernel samples it at each particle's raw Position (VectorFieldForce-sg.hlsl:60-61);
      // no field -> the baked (1,1,1) static PSO (fork-VFF), byte-identical for every pre-existing graph.
      if (!runFieldForce(vffTemplate(), "vector_field_force", &vp, sizeof(vp)))
        runForce(s->psoVecField, &vp, sizeof(vp));  // fork-VFF baked fallback
    } else if (forceKind == FORCE_KIND_VELOCITY) {
      // 批次24 VelocityForce — param-fill peeled to point_ops_forceparams.cpp (cap discipline).
      VelForceParams vp = fillVelForceParams(c, pool);
      runForce(s->psoVel, &vp, sizeof(vp));
    } else if (forceKind == FORCE_KIND_AXISSTEP) {
      // 批次24 AxisStepForce — param-fill peeled to point_ops_forceparams.cpp (cap discipline).
      AxisStepForceParams ap = fillAxisStepForceParams(c, pool);
      runForce(s->psoAxisStep, &ap, sizeof(ap));
    } else if (forceKind == FORCE_KIND_SNAPANGLES) {
      // 批次24 SnapToAnglesForce — param-fill peeled to point_ops_forceparams.cpp (cap discipline).
      SnapAnglesForceParams sp = fillSnapAnglesForceParams(c, pool);
      runForce(s->psoSnapAngles, &sp, sizeof(sp));
    } else if (forceKind == FORCE_KIND_FIELDDISTANCE) {
      // FieldDistanceForce — defaults照 FieldDistanceForce.t3: Amount=1, Attraction=1, Repulsion=1,
      // NormalSamplingDistance=0.01, DecayWithDistance=0.
      FieldDistForceParams fp{};
      fp.Amount = cookInputParam(c, 1, "Amount", 1.0f);
      fp.Attraction = cookInputParam(c, 1, "Attraction", 1.0f);
      fp.Repulsion = cookInputParam(c, 1, "Repulsion", 1.0f);
      fp.NormalSamplingDistance = cookInputParam(c, 1, "NormalSamplingDistance", 0.01f);
      fp.DecayWithDistance = cookInputParam(c, 1, "DecayWithDistance", 0.0f);
      fp.Count = pool;
      // PF field-into-force bridge (runFieldForce): a wired SDF field -> the runtime-compiled
      // field_distance_force kernel samples its distance .w at each particle's raw Position
      // (FieldDistanceForce.hlsl:81) and finite-differences a surface normal to attract/repel; no field ->
      // the baked (.w=1 everywhere) static PSO degenerates to a faithful no-op (fork-FieldDistance-baked).
      if (!runFieldForce(fieldDistanceTemplate(), "field_distance_force", &fp, sizeof(fp)))
        runForce(s->psoFieldDist, &fp, sizeof(fp));  // fork-FieldDistance-baked fallback
    } else if (forceKind == FORCE_KIND_RANDOMJUMP) {
      // RandomJumpForce — defaults照 RandomJumpForce.t3: Amount=1, Frequency=1, Phase=0, Variation=0,
      // DirectionDistribution=(1,1,1). DirectionDistribution(.cs) -> AmountDistribution(template);
      // AmountFromVelocity(.cs) has no template slot -> baked 0 (no invented port).
      RandomJumpForceParams rp{};
      rp.Amount = cookInputParam(c, 1, "Amount", 1.0f);
      rp.Frequency = cookInputParam(c, 1, "Frequency", 1.0f);
      rp.Phase = cookInputParam(c, 1, "Phase", 0.0f);
      rp.Variation = cookInputParam(c, 1, "Variation", 0.0f);
      rp.AmountDistributionX = cookInputParam(c, 1, "DirectionDistribution.x", 1.0f);
      rp.AmountDistributionY = cookInputParam(c, 1, "DirectionDistribution.y", 1.0f);
      rp.AmountDistributionZ = cookInputParam(c, 1, "DirectionDistribution.z", 1.0f);
      rp.Count = pool;
      // PF field-into-force bridge: a wired field -> the runtime-compiled random_jump_force kernel reads
      // its color magnitude to modulate a curlNoise jump and ADDS it to POSITION (fork-RandomJump-position-
      // write, RandomJumpForceTemplate.hlsl:75-77). No field / no template -> no move (no baked fallback PSO).
      runFieldForce(randomJumpTemplate(), "random_jump_force", &rp, sizeof(rp));
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
  // Image-filter ops (Blur, Displace, Tint, ...) self-register via file-scope ImageFilterOp
  // registrars (point_ops_<name>.cpp) during pre-main dynamic init — no central call needed.
}

}  // namespace sw
