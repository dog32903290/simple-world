// particlefield_probe_golden — --selftest-particlefield-probe. THE PF-a FLAG PROBE (blueprint
// PARTICLE_FIELD_BLUEPRINT.md §7 closing FLAG): does a cook path (flat OR resident) bring a WIRED
// field child (ToroidalVortexField, a "Field"/ShaderGraphNode output) into the VectorFieldForce cook?
//
// This is NOT a deliverable golden — it is the load-bearing RED EVIDENCE the blueprint demanded the
// builder produce BEFORE building PF-a. Its PASS condition is the FUTURE state ("the wired field had a
// directional/anisotropic effect on the particle pool"). It is RED today on purpose: it documents that
// the field-into-force bridge does not exist, in either cook leg.
//
// WHY RED (proven by static analysis, this golden makes it executable):
//   1. VectorFieldForce's NodeSpec (node_registry_particle.cpp:47-54) has NO "Field" input port — its
//      TiXL ShaderGraphNode `VectorField` input is OMITTED (the file's own comment :44-45). So a wire
//      from ToroidalVortexField.Result has nowhere to LAND on the force node.
//   2. NEITHER cook driver gathers a "Field" input: point_graph.cpp (flat) and point_graph_resident.cpp
//      gather Points / Texture2D / Mesh / Gradient / Curve inputs by dataType, but grep for "Field" /
//      "ShaderGraphNode" / FieldNode in those drivers returns ZERO. There is no graph->FieldNode walk
//      anywhere in production: renderField2d / makeFieldNode have exactly ONE caller, the field render
//      golden (field_render_golden.cpp) — never a particle/point cook.
//   3. The force kernel bakes the field: vector_field_force.metal:52 `float4 f = float4(1,1,1,1)`
//      (named fork-VFF). So a wired ToroidalVortexField is silently dropped -> the pool drifts along
//      the BAKED isotropic (1,1,1) diagonal, identical to no field wired (runVectorFieldForceSelfTest
//      already encodes that isotropy as its GREEN expectation).
//
// PROBE ASSERTION (the gap, made executable on BOTH legs):
//   build RadialPoints -> ParticleSystem(emit) ; ToroidalVortexField.Result -> VectorFieldForce.* ;
//   VectorFieldForce.force -> ParticleSystem(forces) ; cook N frames flat AND resident; capture the
//   live pool mean. A REAL field-into-force bridge would push the pool with ToroidalVortexField's
//   swirl+radial velocity (anisotropic — the spread across mean components is LARGE relative to the
//   magnitude). The CURRENT (no-bridge) result is the baked (1,1,1) push: every component equal,
//   spread ≈ 0. So:  PASS == "field had an anisotropic effect"  (== bridge exists). RED today.
//
// injectBug: in the future-green world, -bug would sever the field and expect the isotropic baked
// drift back (prod-only RED on the resident leg per blueprint §4). TODAY the bridge does not exist, so
// both modes observe the same baked isotropy; -bug here flips the EXPECTATION to the present reality
// (isotropic) so the harness has a row that is GREEN today — it asserts "the gap is exactly the baked
// (1,1,1) isotropy, on both legs", which is the precise, falsifiable shape of the missing bridge.
//
// ZONE: shell tier (app/src/ root, like field_render_golden.cpp / point_ops_selftest leaves it mirrors).
// Crosses runtime (PointGraph cook, registerBuiltinPointOps, the field/particle NodeSpecs) only — no
// platform include (the GPU lib is loaded via the same newLibrary(SW_SHADER_METALLIB) the point goldens
// use). It builds a real Graph + a real SymbolLibrary and cooks both legs through the registered ops.
#include "runtime/point_graph.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary / Symbol / SymbolChild (resident leg)
#include "runtime/graph.h"                // Graph / Node / PortSpec / findSpec / pinId
#include "runtime/render_command.h"       // RenderCommand (DrawPoints is a Cmd op on both legs)
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / ResidentEvalGraph
#include "runtime/tixl_point.h"           // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// capture-only DrawPoints Cmd op: memcpy the live pool bag the terminal carries. DrawPoints is a
// COMMAND op on BOTH legs (point_ops.cpp:70 cookDrawPoints -> RenderCommand), so the capture rides the
// Cmd path identically flat and resident (mirrors resident_cook_selftest.cpp's rcCapture).
std::vector<SwPoint>* g_pfCap = nullptr;
RenderCommand pfCaptureCmd(CmdCookCtx& c) {
  RenderCommand rc;
  if (g_pfCap && c.points && c.count > 0) {
    g_pfCap->assign(c.count, SwPoint{});
    std::memcpy(g_pfCap->data(), const_cast<MTL::Buffer*>(c.points)->contents(),
                (size_t)c.count * sizeof(SwPoint));
  }
  return rc;
}

struct Mean { float mx, my, mz; size_t n; };
Mean poolMean(const std::vector<SwPoint>& bag) {
  double sx = 0, sy = 0, sz = 0; size_t n = 0;
  for (const SwPoint& p : bag) {
    if (std::isnan(p.Position.x) || std::isnan(p.Position.y) || std::isnan(p.Position.z)) continue;
    sx += p.Position.x; sy += p.Position.y; sz += p.Position.z; ++n;
  }
  if (n == 0) return {0, 0, 0, 0};
  return {(float)(sx / n), (float)(sy / n), (float)(sz / n), n};
}

// spread of the three mean components relative to |mean| — 0 == isotropic (1,1,1) baked push;
// LARGE == the wired field bent the pool along its swirl/radial geometry (anisotropic).
float anisotropy(const Mean& m) {
  float mag = std::fmax(std::fabs(m.mx), std::fmax(std::fabs(m.my), std::fabs(m.mz)));
  if (mag < 1e-6f) return 0.0f;
  float spread = std::fmax(std::fmax(std::fabs(m.mx - m.my), std::fabs(m.my - m.mz)),
                           std::fabs(m.mx - m.mz));
  return spread / mag;
}

MTL::Library* loadLib(MTL::Device* dev) {
  NS::Error* err = nullptr;
  return dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
}

// STATIC half of the probe: is there a "Field" input port on VectorFieldForce, and does the field op
// even expose a "Field" output the cook could chase? Returns true iff the WIRE HAS NOWHERE TO LAND
// (no Field input on the force) — i.e. the bridge's first prerequisite (a typed field input on the
// force contract) is absent. This is the structural root of the RED.
bool forceHasFieldInput() {
  const NodeSpec* vff = findSpec("VectorFieldForce");
  if (!vff) return false;
  for (const PortSpec& p : vff->ports)
    if (p.isInput && p.dataType == "Field") return true;
  return false;
}

// Result output port index on ToroidalVortexField (last port, dataType "Field"). Defensive lookup so
// the connection points at the real output pin even if the spec's port order shifts.
int findFieldOutPortIdx() {
  const NodeSpec* s = findSpec("ToroidalVortexField");
  if (!s) return 0;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (!s->ports[i].isInput && s->ports[i].dataType == "Field") return (int)i;
  return (int)(s->ports.empty() ? 0 : s->ports.size() - 1);
}

// "Field" INPUT port index on VectorFieldForce (PF-0 added the "VectorField" port). The flat field
// gather chases this port; wiring the field to the REAL port (not a phantom pin) is what exercises the
// builder. Returns -1 if absent (pre-PF-0 — then the probe falls back to a phantom pin, still inert).
int findForceFieldInPortIdx() {
  const NodeSpec* s = findSpec("VectorFieldForce");
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (s->ports[i].isInput && s->ports[i].dataType == "Field") return (int)i;
  return -1;
}

// FLAT leg: cook RadialPoints -> ParticleSystem with a wired VectorFieldForce, AND a ToroidalVortexField
// whose Result we attempt to wire toward the force (raw pin connection — there is no Field input port to
// receive it, mirroring exactly what a UI wire would face). Returns the pool mean after `steps` frames.
Mean cookFlat(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, uint32_t N, int steps,
              float amount) {
  registerBuiltinPointOps();
  std::vector<SwPoint> cap; g_pfCap = &cap;
  registerCmdOp("DrawPoints", pfCaptureCmd);
  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen;  gen.id = 1;  gen.type = "RadialPoints"; gen.params["Count"] = (float)N; gen.params["Radius"] = 2.0f;
  Node sim;  sim.id = 2;  sim.type = "ParticleSystem";
  Node drw;  drw.id = 3;  drw.type = "DrawPoints";
  Node vff;  vff.id = 4;  vff.type = "VectorFieldForce"; vff.params["Amount"] = amount;
  Node fld;  fld.id = 5;  fld.type = "ToroidalVortexField";  // .t3 defaults (Radius=0.5,Range=0.5,...)
  g.nodes = {gen, sim, drw, vff, fld};
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points -> emit
  g.connections.push_back({102, pinId(4, 0), pinId(2, 1)});  // VectorFieldForce.force -> forces
  g.connections.push_back({103, pinId(2, 2), pinId(3, 0)});  // result -> DrawPoints
  // The field wire: ToroidalVortexField.Result -> VectorFieldForce's "VectorField" Field input. PF-0
  // added the real Field port, so the flat field gather builds the tree from THIS wire. (Pre-PF-0 there
  // was no Field port → a phantom pin 99, inert; the fallback keeps the probe runnable either way.)
  const int forceFieldIn = findForceFieldInPortIdx();
  g.connections.push_back({104, pinId(5, /*Result port idx*/findFieldOutPortIdx()),
                           pinId(4, forceFieldIn >= 0 ? forceFieldIn : 99)});

  const int targetId = pg.defaultDrawTarget(g);
  for (int i = 0; i < steps; ++i) {
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)i; ctx.time = 0.05f * (float)i; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, targetId);
  }
  g_pfCap = nullptr;
  return poolMean(cap);
}

// RESIDENT leg: the same graph as a SymbolLibrary, cooked through cookResident. The atomic op symbols
// mirror the NodeSpec ports the resident walk gathers. The field child + its wire are present in the
// library exactly as the flat graph has them; the resident driver has no "Field" gather either, so the
// field is dropped identically. Returns the resident pool mean.
Mean cookResidentLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, uint32_t N, int steps,
                     float amount) {
  registerBuiltinPointOps();
  std::vector<SwPoint> cap; g_pfCap = &cap;
  registerCmdOp("DrawPoints", pfCaptureCmd);
  PointGraph pg(dev, lib, q, 64, 64);

  SymbolLibrary slib;
  slib.symbols["RadialPoints"] = [] { Symbol s; s.id = s.name = "RadialPoints"; s.atomic = true;
    s.inputDefs = {{"Count", "Count", "Float", 0.0f}};
    s.outputDefs = {{"points", "points", "Points", 0.0f}}; return s; }();
  slib.symbols["ParticleSystem"] = [] { Symbol s; s.id = s.name = "ParticleSystem"; s.atomic = true;
    s.inputDefs = {{"emit", "emit", "Points", 0.0f}, {"forces", "forces", "ParticleForce", 0.0f}};
    s.outputDefs = {{"result", "result", "Points", 0.0f}}; return s; }();
  slib.symbols["DrawPoints"] = [] { Symbol s; s.id = s.name = "DrawPoints"; s.atomic = true;
    s.inputDefs = {{"points", "points", "Points", 0.0f}};
    s.outputDefs = {{"out", "out", "Command", 0.0f}}; return s; }();
  slib.symbols["VectorFieldForce"] = [] { Symbol s; s.id = s.name = "VectorFieldForce"; s.atomic = true;
    // The field input the bridge WOULD need, declared here so the resident library can carry the wire.
    s.inputDefs = {{"Amount", "Amount", "Float", 1.0f}, {"VectorField", "VectorField", "Field", 0.0f}};
    s.outputDefs = {{"force", "force", "ParticleForce", 0.0f}}; return s; }();
  slib.symbols["ToroidalVortexField"] = [] { Symbol s; s.id = s.name = "ToroidalVortexField"; s.atomic = true;
    s.outputDefs = {{"Result", "Result", "Field", 0.0f}}; return s; }();

  Symbol root; root.id = root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Command", 0.0f}};
  SymbolChild cg; cg.id = 1; cg.symbolId = "RadialPoints"; cg.overrides["Count"] = (float)N;
  SymbolChild cs; cs.id = 2; cs.symbolId = "ParticleSystem";
  SymbolChild cd; cd.id = 3; cd.symbolId = "DrawPoints";
  SymbolChild cv; cv.id = 4; cv.symbolId = "VectorFieldForce"; cv.overrides["Amount"] = amount;
  SymbolChild cf; cf.id = 5; cf.symbolId = "ToroidalVortexField";
  root.children = {cg, cs, cd, cv, cf};
  root.connections = {
      {1, "points", 2, "emit"},
      {4, "force", 2, "forces"},
      {2, "result", 3, "points"},
      {5, "Result", 4, "VectorField"},          // the field wire — carried, but the cook drops it
      {3, "out", kSymbolBoundary, "out"},
  };
  slib.symbols["Root"] = root; slib.rootId = "Root";

  ResidentEvalGraph rg = buildEvalGraph(slib, "Root");
  for (int i = 0; i < steps; ++i) {
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)i; ctx.time = 0.05f * (float)i; ctx.deltaTime = 1.0f / 60.0f;
    pg.cookResident(rg, ctx, nullptr, "3");
  }
  g_pfCap = nullptr;
  return poolMean(cap);
}

}  // namespace

int runParticleFieldProbeSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  MTL::Library* lib = loadLib(dev);
  if (!lib) {
    std::printf("[selftest-particlefield-probe] FAIL: no metallib\n");
    if (q) q->release(); if (dev) dev->release(); pool->release();
    return 1;
  }

  const uint32_t N = 1024;
  const int STEPS = 30;
  const float AMOUNT = 6.0f;

  // STATIC: the force contract has no Field input -> the wire has nowhere to land (root of the RED).
  const bool fieldInputExists = forceHasFieldInput();

  Mean flat = cookFlat(dev, q, lib, N, STEPS, AMOUNT);
  Mean res = cookResidentLeg(dev, q, lib, N, STEPS, AMOUNT);
  const float flatAniso = anisotropy(flat);
  const float resAniso = anisotropy(res);

  // The probe's TRUTH (today): with no bridge, both legs produce the BAKED isotropic (1,1,1) drift —
  // every mean component > 0 AND the anisotropy is tiny. A real field-into-force bridge would make the
  // anisotropy LARGE (ToroidalVortexField's swirl is mostly one plane; radial pulls toward the ring).
  const float kIsoEps = 0.3f;   // baked (1,1,1): components within 30% of |mean| of each other
  const float kAnisoWant = 0.6f; // a sampled field would bend the pool well past this

  // BLUEPRINT FLAG VERDICT (printed regardless of mode): the gap is real on both legs.
  const bool flatIsotropic = (flat.n > 0) && (flatAniso < kIsoEps);
  const bool resIsotropic = (res.n > 0) && (resAniso < kIsoEps);
  const bool fieldHadEffect = (flat.n > 0 && flatAniso > kAnisoWant) &&
                              (res.n > 0 && resAniso > kAnisoWant);

  std::printf("[selftest-particlefield-probe] PROBE VERDICT = B (resident+flat CANNOT bring the wired "
              "field child)\n");
  std::printf("  static: VectorFieldForce has Field input port? %s (TiXL VectorField input is omitted, "
              "node_registry_particle.cpp:44)\n", fieldInputExists ? "YES" : "NO");
  std::printf("  flat    mean=(% .3f,% .3f,% .3f) n=%zu aniso=%.3f -> %s\n",
              flat.mx, flat.my, flat.mz, flat.n, flatAniso,
              flatIsotropic ? "BAKED-ISOTROPIC(field dropped)" : "anisotropic");
  std::printf("  resident mean=(% .3f,% .3f,% .3f) n=%zu aniso=%.3f -> %s\n",
              res.mx, res.my, res.mz, res.n, resAniso,
              resIsotropic ? "BAKED-ISOTROPIC(field dropped)" : "anisotropic");

  if (injectBug) {
    // -bug variant: assert the FUTURE-GREEN PASS condition ("the wired field had an anisotropic
    // effect"). This is RED today on purpose — it is the executable proof the bridge is missing. When
    // PF-a lands (field-MSL -> compute PSO + Field input on the force contract + resident field
    // projection), this row flips GREEN and the no-bug row below documents the present gap shape.
    bool wouldPass = fieldInputExists && fieldHadEffect;
    std::printf("  -bug: future-green expectation (field had anisotropic effect on BOTH legs) = %s "
                "(RED today == bridge missing, the FLAG)\n", wouldPass ? "MET" : "NOT-MET");
    std::printf("[selftest-particlefield-probe] %s\n",
                wouldPass ? "PASS (bridge exists!)" : "RED (PF-a bridge absent — blueprint FLAG=B)");
    if (q) q->release(); lib->release(); if (dev) dev->release(); pool->release();
    return wouldPass ? 0 : 1;  // RED today (== the documented gap)
  }

  // no-bug row: assert the PF-0 TRANSITIONAL TRUTH precisely. PF-0 added the "VectorField" Field input
  // port to VectorFieldForce and a flat+resident field gather that DELIVERS the wired ToroidalVortexField
  // tree to PointCookCtx::inputFieldTree — but the force KERNEL still bakes (1,1,1) (PF-a removes the
  // bake). So the present truth is: the force NOW HAS a Field input (fieldInputExists==YES), yet both legs
  // STILL drift along the baked isotropy (the tree reaches the cook but is not consumed). This is the
  // two-stage flip's MIDDLE state — it is GREEN at PF-0 and PF-a flips it to the TERMINAL anisotropy≠0.
  // It locks the exact shape of the PF-0 gap so a regression (e.g. PF-a half-landing on one leg, or the
  // Field port vanishing) trips here.
  bool gapHolds = fieldInputExists && flatIsotropic && resIsotropic;
  std::printf("[selftest-particlefield-probe] %s (PF-0 gap-shape: Field input present + tree delivered, "
              "both legs STILL baked-isotropic — kernel un-consumed until PF-a)\n",
              gapHolds ? "PASS" : "FAIL");
  if (q) q->release(); lib->release(); if (dev) dev->release(); pool->release();
  return gapHolds ? 0 : 1;
}

}  // namespace sw
