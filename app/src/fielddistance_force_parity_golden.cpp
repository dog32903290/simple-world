// fielddistance_force_parity_golden — --selftest-fielddistanceforce-parity. PARITY golden for the
// stateful FORCE FieldDistanceForce (PARITY_GATE_PLAN.md Stage-3 fan-out, Force-class). Mirrors the
// directional_force_parity_golden structure (three teeth + injectBug leg + cook-through graph).
//
// ── WHAT FieldDistanceForce IS, AND WHAT THIS GOLDEN GUARDS ──────────────────────────────────────────
// FieldDistanceForce (FieldDistanceForce.hlsl:74-101) samples a WIRED SDF Field's distance .w at each
// particle's RAW Position, finite-differences a surface normal (GetFieldNormal, hlsl:65-72), then pushes
// the particle along it: d>0 attract toward the surface scaled by pow(d+1,-DecayWithDistance); d<=0 repel.
// NaN guards on d and n preserve a no-op for a degenerate field. The Field input is a ShaderGraphNode
// (a procedural SDF assembled at runtime) — so the WIRED behavior runs the runtime-compiled template
// (field_distance_force_template.metal). With NO field wired, sw runs the BAKED fallback kernel
// (field_distance_force.metal, fork-FieldDistance-baked): GetField() returns f=float4(1,1,1,1) so
// GetDistance==f.w==1 EVERYWHERE → a constant distance field has zero gradient → GetFieldNormal =
// normalize(0) = NaN → the isnan(n.x) guard returns with NO velocity change. This is the FAITHFUL no-op:
// no distance field → no surface → no attract/repel (TiXL's GetField seed `float4 f = 1`, hlsl:38, with no
// FIELD_CALL filled does the same — a constant 1 field has no surface).
//
// ── HONEST RED-FIRST CLASSIFICATION (PARITY_GATE_PLAN.md Stage-3 §2): GREEN-补闸, NOT 修偏差 ──────────
// I scouted FieldDistanceForce's production state:
//   sw NodeSpec defaults (node_registry_particle.cpp:144-156): Amount=1, Attraction=1, Repulsion=1,
//                                                               NormalSamplingDistance=0.01, DecayWithDistance=0.
//   TiXL FieldDistanceForce.t3 DefaultValue:                   Amount=1, Attraction=1, Repulsion=1,
//                                                               NormalSamplingDistance=0.01, DecayWithDistance=0.
//   The cook (point_ops.cpp:278-293) reads every param via cookInputParam with TiXL-matching fallbacks; the
//   baked kernel math (field_distance_force.metal:28-60) is byte-1:1 with FieldDistanceForce.hlsl:74-101.
// → FieldDistanceForce is already FAITHFUL. This golden ADDS the missing parity gate for the NO-FIELD
//   BAKED CONTRACT (the contract the existing smoke never asserts: "no field → no force"). It does NOT fix
//   a deviation, because there is none. NO production edits accompany it.
//
// ── DEFERRED (NOT in this batch): wired-SDF attract/repel behavior ───────────────────────────────────
// The WIRED-field path (runtime-compiled template sampling a real SDF, attract outside / repel inside) is
// NOT exercised here. That needs a wired SphereSdf Field node + the assembleFieldMSL bridge and is a
// separate, heavier golden. THIS golden pins the baked NO-FIELD contract + the NodeSpec defaults that the
// kernel reads. The kernel math itself is asserted on the closed-form no-op (T1) — the only behavior the
// baked path can produce. Wired-SDF parity is标 deferred (see caveats in the报告).
//
// ── TOOTH 1 (baked no-field kernel contract, direct-kernel, TiXL closed-form) ────────────────────────
// velocity型 = TRANSFORM: the kernel ADDS to existing Velocity, so a meaningful no-op test must seed a
// KNOWN NON-ZERO initial velocity and prove it survives. We dispatch the PRODUCTION baked
// `field_distance_force` kernel on a synthetic Particle buffer with KNOWN non-zero Velocity v0 and the
// TiXL .t3 NodeSpec defaults (Amount=1, NormalSamplingDistance=0.01, ...). Because the baked field is the
// constant all-ones field, GetFieldNormal = normalize(0) = NaN → the isnan(n.x) guard (kept verbatim,
// hlsl:85) returns BEFORE the velocity write → post-velocity == v0 EXACTLY for every particle.
// TiXL GROUND-TRUTH ANCHOR (rule 2): FieldDistanceForce.hlsl:81-86 — `n=GetFieldNormal(pos)` over a
// constant field is normalize(0)=NaN, `if (isnan(n.x)) return;` → Velocity unchanged. We assert
// maxVelDelta(post,v0)==0 (the no-op contract). This is a hand-SET FieldDistForceParams analog so it is
// BLIND to the NodeSpec default — TOOTH 2 (cook-through) is what guards the default.
//
// ── TOOTH 2 (NodeSpec-default no-field contract, THROUGH THE REAL COOK — rule 4, the gap-plug) ───────
// Cook the production rig RadialPoints -> ParticleSystem(+FieldDistanceForce, NO Field input wired) ->
// DrawPoints with the FieldDistanceForce node carrying NO param override → the cook resolves the
// production NodeSpec DEFAULTS (node_registry_particle.cpp → resolveNodeParams → cookInputParam) AND, with
// no inputFieldTree, runFieldForce returns false → the BAKED fallback no-op kernel runs (point_ops.cpp:292-
// 293). We compare the produced positions to a NO-FORCE baseline cook (the FieldDistanceForce node removed
// entirely). The no-field baked contract says they must be POSITION-IDENTICAL (the force is a pure no-op).
//   no-bug   → expect prodDefault == no-force baseline (the faithful no-op contract holds).
//   injectBug→ expect prodDefault to MOVE off the baseline (a fake deviation: pretend the baked path
//              shoves particles like a wired field would) → the no-op assertion flips RED.
// Because this runs THROUGH the cook, a NodeSpec / baked-fallback / guard regression (e.g. someone breaks
// the isnan guard, or wires a phantom default field) is caught — invisible to TOOTH 1's hand-set params.
//
// ── TOOTH 3 (offline-render determinism, through the REAL cook) ──────────────────────────────────────
// FieldDistanceForce has no Phase / no wall-clock term, so two cooks with IDENTICAL inputs and frame
// schedule but DIFFERENT ctx.time must be bit-identical (the offline-render determinism contract). We cook
// twice with different time0 and assert maxPosDelta==0. This is a property of the cook (independent of
// injectBug) and is GREEN in both legs.
//
// injectBug LEG: TOOTH 2 flips its expectation — it expects the prod-default cook to DIVERGE from the
// no-force baseline (a fake "the baked path pushes"偏差). The faithful production no-op makes prod ==
// baseline, so that flipped expectation fails → RED. no-bug → prod == baseline → GREEN. So the gate reads
// no-bug GREEN ↔ injectBug RED (--bite-collectable).
//
// ZONE: shell tier (app/src/ root, like turbulence_parity_golden.cpp / directional_force_parity_golden.cpp).
// Crosses runtime (PointGraph cook + the kernel). NO production edits — FieldDistanceForce was already faithful.
#include "runtime/point_graph.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "parity_golden_harness.h"
#include "runtime/force_params.h"  // FieldDistForceParams, FORCE_Particles/FORCE_Params
#include "runtime/graph.h"         // Graph/Node/pinId
#include "runtime/tixl_point.h"    // Particle / SwPoint (64B)

namespace sw {
namespace {

// TiXL FieldDistanceForce.t3 DefaultValue constants (the parity anchors, rule 2).
constexpr float kTiXLAmount = 1.0f;        // FieldDistanceForce.t3 Amount DefaultValue
constexpr float kTiXLAttraction = 1.0f;    // FieldDistanceForce.t3 Attraction DefaultValue
constexpr float kTiXLRepulsion = 1.0f;     // FieldDistanceForce.t3 Repulsion DefaultValue
constexpr float kTiXLNormSamp = 0.01f;     // FieldDistanceForce.t3 NormalSamplingDistance DefaultValue
constexpr float kTiXLDecay = 0.0f;         // FieldDistanceForce.t3 DecayWithDistance DefaultValue

// Known NON-ZERO initial velocity (velocity型=TRANSFORM): the kernel ADDS to this; the baked no-op must
// leave it EXACTLY intact. Distinct components so a per-axis corruption stands out.
constexpr float kV0X = 0.5f;
constexpr float kV0Y = -0.3f;
constexpr float kV0Z = 0.2f;

// Dispatch the production baked `field_distance_force` kernel on N particles carrying a KNOWN non-zero
// velocity; return post velocities. Mirrors dispatchDir in directional_force_parity_golden.cpp. The baked
// kernel binds only Particles@0 + Params@1 (no FieldParams — fork-FieldDistance-baked, field_distance_force
// .metal:28-30), so this direct-kernel dispatch matches the no-field production path exactly.
std::vector<Particle> dispatchFieldDist(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                        const FieldDistForceParams& P, uint32_t N) {
  std::vector<Particle> out;
  MTL::Function* fn = lib->newFunction(NS::String::string("field_distance_force", NS::UTF8StringEncoding));
  if (!fn) return out;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return out;

  MTL::Buffer* buf = dev->newBuffer((NS::UInteger)N * sizeof(Particle), MTL::ResourceStorageModeShared);
  Particle* p = static_cast<Particle*>(buf->contents());
  for (uint32_t i = 0; i < N; ++i) {
    p[i] = Particle{};
    p[i].Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
    p[i].BirthTime = 0.0f;  // emitted (not NaN) — the BirthTime guard must NOT trip
    p[i].Position = SW_PACKED3{0.1f * (float)i, -0.2f * (float)i, 0.05f * (float)i};  // varied (irrelevant to no-op)
    p[i].Velocity = SW_PACKED3{kV0X, kV0Y, kV0Z};  // KNOWN non-zero — must survive the baked no-op
  }
  const uint32_t tg = 64;
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(buf, 0, FORCE_Particles);
  enc->setBytes(&P, sizeof(P), FORCE_Params);
  enc->dispatchThreadgroups(MTL::Size::Make((N + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  out.assign(N, Particle{});
  std::memcpy(out.data(), buf->contents(), (size_t)N * sizeof(Particle));
  buf->release();
  pso->release();
  return out;
}

// ---- TOOTH 2/3 cook capture (RadialPoints -> ParticleSystem(+FieldDistanceForce?) -> DrawPoints) ----
std::vector<SwPoint>* g_fdCap = nullptr;
void captureFieldDist(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_fdCap || !pts || c.count == 0) return;
  g_fdCap->assign(c.count, SwPoint{});
  std::memcpy(g_fdCap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

// Cook the field-distance rig for `steps` frames from a fixed time base time0 + i*dt; return captured
// positions.
//   • withForce == false -> NO FieldDistanceForce node at all (the no-force baseline).
//   • withForce == true  -> FieldDistanceForce wired, carrying NO param override and NO Field input → the
//                           cook reads the production NodeSpec DEFAULTS and, with no inputFieldTree,
//                           runFieldForce returns false → the BAKED no-op fallback runs (point_ops.cpp:292).
// Two calls with the SAME inputs but DIFFERENT time0 expose any wall-clock dependence (TOOTH 3).
std::vector<SwPoint> cookFieldDistRig(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                      float time0, int steps, bool withForce) {
  registerBuiltinPointOps();
  std::vector<SwPoint> captured;
  g_fdCap = &captured;
  registerDrawOp("DrawPoints", captureFieldDist);
  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 256.0f; gen.params["Radius"] = 2.0f;  // pinned scene (rule 5): explicit, not cook-default
  Node sim; sim.id = 2; sim.type = "ParticleSystem";
  Node drw; drw.id = 3; drw.type = "DrawPoints";
  g.nodes.push_back(gen); g.nodes.push_back(sim);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points -> emit
  if (withForce) {
    Node force; force.id = 4; force.type = "FieldDistanceForce";  // NO param override -> NodeSpec defaults;
                                                                  // NO Field connection -> baked no-op path
    g.nodes.push_back(force);
    g.connections.push_back({102, pinId(4, 0), pinId(2, 1)});  // FieldDistanceForce.force -> forces
  }
  g.nodes.push_back(drw);
  g.connections.push_back({103, pinId(2, 2), pinId(3, 0)});  // result -> DrawPoints

  const int targetId = pg.defaultDrawTarget(g);
  for (int i = 0; i < steps; ++i) {
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)i;          // SAME frame schedule both calls (cycle buffer identical)
    ctx.time = time0 + 0.05f * (float)i;   // the ONLY difference between the two TOOTH-3 cooks
    ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, targetId);
  }
  g_fdCap = nullptr;
  return captured;
}

// Mean / max position displacement between two equal-size point bags (NaN slots skipped).
struct PosDelta { double mean; double maxv; size_t valid; };
PosDelta posDelta(const std::vector<SwPoint>& a, const std::vector<SwPoint>& b) {
  size_t n = std::min(a.size(), b.size());
  double s = 0.0, mx = 0.0; size_t valid = 0;
  for (size_t i = 0; i < n; ++i) {
    if (std::isnan(a[i].Position.x) || std::isnan(b[i].Position.x)) continue;
    double d = std::sqrt((double)(a[i].Position.x - b[i].Position.x) * (a[i].Position.x - b[i].Position.x) +
                         (double)(a[i].Position.y - b[i].Position.y) * (a[i].Position.y - b[i].Position.y) +
                         (double)(a[i].Position.z - b[i].Position.z) * (a[i].Position.z - b[i].Position.z));
    s += d; if (d > mx) mx = d; ++valid;
  }
  return {valid ? s / (double)valid : 0.0, mx, valid};
}

}  // namespace

int runFieldDistanceForceParitySelfTest(bool injectBug) {
  ParityHarness h;
  ParityReport rep("selftest-fielddistanceforce-parity");
  if (!h.ok()) {
    printf("[selftest-fielddistanceforce-parity] FAIL: no metallib\n");
    return 1;
  }

  const uint32_t N = 1024;

  // ── TOOTH 1: BAKED NO-FIELD kernel contract (direct-kernel, TiXL closed-form no-op) ──────────────
  // velocity型=TRANSFORM: seed KNOWN non-zero velocity v0; the baked all-ones field → normalize(0)=NaN →
  // isnan(n.x) guard returns BEFORE the velocity write → post-velocity == v0 EXACTLY (the no-op contract,
  // FieldDistanceForce.hlsl:81-86). NodeSpec defaults pinned (rule 5).
  FieldDistForceParams k{};
  k.Amount = kTiXLAmount; k.Attraction = kTiXLAttraction; k.Repulsion = kTiXLRepulsion;
  k.NormalSamplingDistance = kTiXLNormSamp; k.DecayWithDistance = kTiXLDecay; k.Count = N;
  std::vector<Particle> vel = dispatchFieldDist(h.dev, h.queue, h.lib, k, N);

  // Did the dispatch run at all? (empty -> pipeline build failed.)
  rep.expectTrue("kernelDispatched(N==out)", vel.size() == (size_t)N, (double)vel.size());

  // Max per-axis deviation of post-velocity from v0. The faithful no-op keeps it EXACTLY zero.
  double maxVelDelta = 0.0;
  for (const Particle& p : vel) {
    double dx = std::fabs((double)p.Velocity.x - kV0X);
    double dy = std::fabs((double)p.Velocity.y - kV0Y);
    double dz = std::fabs((double)p.Velocity.z - kV0Z);
    double d = dx + dy + dz;
    if (d > maxVelDelta) maxVelDelta = d;
  }
  // no-bug: TiXL no-field contract -> velocity unchanged -> maxVelDelta == 0.
  // injectBug: pretend the baked path SHOULD push (a fake偏差) -> expect a nonzero move. The faithful no-op
  //   keeps maxVelDelta==0, so this nonzero expectation fails -> RED.
  double expectedMaxVelDelta = injectBug ? 0.5 : 0.0;  // injectBug: fake "must move ≥0.5" expectation
  rep.expect("bakedNoFieldVel==v0(no-op)", maxVelDelta, expectedMaxVelDelta, 1e-5);

  // ── TOOTH 2: NODESPEC-DEFAULT no-field contract through the REAL COOK (rule 4, the gap-plug) ──────
  const int kSteps = 8;
  std::vector<SwPoint> baseline = cookFieldDistRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*withForce=*/false);
  std::vector<SwPoint> prodDef  = cookFieldDistRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*withForce=*/true);

  // Structure: prod-default cook produced a non-empty bag, the SAME size as the baseline (same rig, only
  // the no-op force node differs → identical pool size). Well-posed per-particle comparison.
  rep.expectTrue("cookBagSize(prod==baseline>0)",
                 prodDef.size() > 0 && prodDef.size() == baseline.size(),
                 (double)prodDef.size());

  PosDelta dd = posDelta(prodDef, baseline);
  rep.expectTrue("cookHadValidPoints", dd.valid > 0, (double)dd.valid);

  // THE NO-FIELD CONTRACT TOOTH: does the production-default FieldDistanceForce (no field wired) leave the
  // ring EXACTLY where the no-force baseline left it (the faithful baked no-op)?
  //   no-bug   → expect prod == baseline (maxPosDelta == 0).
  //   injectBug→ expect prod to MOVE off baseline (maxPosDelta == 0.1, a fake "the baked path pushes"偏差).
  //              The faithful no-op keeps maxPosDelta==0, so this expectation fails -> RED.
  double expectedMaxPosDelta = injectBug ? 0.1 : 0.0;
  rep.expect("prodDefault==noForce(no-op)", dd.maxv, expectedMaxPosDelta, 1e-5);
  // Mean-displacement context probe (same contract, mean form): no-bug ~0, injectBug expectation flips.
  rep.expect("prodDefaultMeanDisp(no-op)", dd.mean, injectBug ? 0.1 : 0.0, 1e-5);

  // ── TOOTH 3: offline-render determinism through the real cook ──────────────────────────────────
  // FieldDistanceForce has no Phase / no wall-clock term, so identical inputs at different ctx.time must be
  // bit-identical. GREEN in both legs (a property of the cook, independent of injectBug).
  std::vector<SwPoint> cookA = cookFieldDistRig(h.dev, h.queue, h.lib, /*time0=*/0.0f, /*steps=*/8, /*withForce=*/true);
  std::vector<SwPoint> cookB = cookFieldDistRig(h.dev, h.queue, h.lib, /*time0=*/10.0f, /*steps=*/8, /*withForce=*/true);
  PosDelta det = posDelta(cookA, cookB);
  rep.expect("cookDeterministic(maxPosDelta==0)", det.maxv, 0.0, 1e-5);

  return rep.finish();
}

}  // namespace sw
