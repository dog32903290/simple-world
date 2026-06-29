// fieldvolume_force_parity_golden — --selftest-fieldvolumeforce-parity. PARITY golden for the stateful FORCE
// FieldVolumeForce (PARITY_GATE_PLAN.md Stage-3 fan-out, "补验证闸" GREEN case — NOT a "修偏差" case).
//
// FieldVolumeForce is the BOUNCE/attract/repel force: it samples a wired SDF Field's distance (.w) at each
// particle's position and, on a boundary crossing this step, REFLECTS the velocity off the surface, else
// attracts (outside) / repels (inside). The WIRED-SDF closed-form behavior is ALREADY guarded by the sibling
// golden fieldvolumeforce_field_golden.cpp (--selftest-fieldvolumeforce-field: SphereSDF + one particle @
// (1,0,0), Attraction=1 → Velocity≈(-0.425·A,0,0), exercising the *0.425 Attraction routing fork). This
// golden guards the OTHER contract the sibling does not: the NodeSpec-default, NO-FIELD baked path THROUGH
// THE REAL COOK — the contract the production node lives on when no Field is wired.
//
// ── HONEST RED-FIRST CLASSIFICATION (PARITY_GATE_PLAN.md Stage-3 §2): GREEN 补闸, production ALREADY faithful ─
// I scouted FieldVolumeForce's production state against TiXL:
//   sw NodeSpec defaults (node_registry_particle.cpp:198-214):     Amount=1, Attraction=0.2, AttractionDecay=0,
//     Repulsion=0.1, ReflectOnCollision=1, Bounciness=1, RandomizeBounce=0, RandomizeReflection=0,
//     InvertVolume=0, NormalSamplingDistance=0.1, ApplyColorOnCollision=0.
//   TiXL FieldVolumeForce.t3 DefaultValue (lines 5-46):            Amount=1.0, Attraction=0.2, AttractionDecay=0,
//     Repulsion=0.1, ReflectOnCollision=true, Bounciness=1.0, RandomizeBounce=0, RandomizeReflection=0,
//     InvertVolume=false, NormalSamplingDistance=0.1, ApplyColorOnCollision=false.  → NodeSpec == .t3 (byte-for-byte).
//   The cook (point_ops.cpp:316-318 → point_ops_forceparams.cpp:86-102 fillFieldVolumeForceParams) reads every
//   param via cookInputParam with TiXL-matching fallbacks AND applies the three .t3 FloatsToBuffer routing forks
//   host-side — Attraction*=0.425 (Multiply node), InvertVolumeFactor=InvertVolume?-1:+1 (BoolToFloat),
//   SpeedFactor=1.0 (fork-FieldVolume-speedfactor) — all faithful. The wired-field kernel
//   (field_volume_force_template.metal) and the no-field BAKED kernel (field_volume_force.metal) are byte-1:1
//   with FieldVolumeForce.hlsl:91-151.
// → FieldVolumeForce was already FAITHFUL. This golden ADDS the missing no-field/cook-through gate; it does NOT
//   fix a deviation, because there is none. NO production edits accompany it. The injectBug tooth still proves
//   teeth (it flips a NodeSpec-default expectation to a false deviation → RED).
//
// ── THE NO-FIELD CONTRACT (fork-FieldVolume-baked, the thing this golden guards) ──────────────────────────────
// FieldVolumeForce's Field input is a procedural SDF (ShaderGraphNode). With NO field wired, runFieldForce
// returns false (point_ops.cpp:219 `if (!c.inputFieldTree ...) return false;`) → the cook falls back to the
// BAKED kernel field_volume_force.metal. There GetField()=float4(1,1,1,1) → distance≡1 EVERYWHERE → a constant
// distance field has ZERO gradient → GetNormal=normalize(0)=NaN → force=NaN → velocity becomes NaN → the final
// `if(!isnan(velocity)...)` guard (FieldVolumeForce.hlsl:149-150) BLOCKS the write. So with no field the force
// is a faithful NO-OP: the particle's velocity is preserved EXACTLY (a constant field has no surface to bounce
// off / attract to). This is load-bearing — a regression that dropped the NaN guard, or that let a degenerate
// normal leak a non-zero/NaN write, would corrupt every no-field FieldVolumeForce graph.
//
// ── TOOTH 1 (baked-kernel no-op contract, direct-kernel, TiXL closed-form) ───────────────────────────────────
// Dispatch the PRODUCTION baked `field_volume_force` kernel on N particles seeded with KNOWN, DISTINCT,
// NON-ZERO velocities (velocity is a TRANSFORM-type input — the force transforms an existing velocity — so a
// zero seed would make the no-op contract vacuous). Params are the NodeSpec/.t3 defaults AFTER the host forks
// (Attraction=0.2*0.425=0.085, InvertVolumeFactor=+1, SpeedFactor=1, ...). TiXL closed-form (baked all-ones
// field, FieldVolumeForce.hlsl:143-150): surfaceN=NaN → force=NaN → velocity=NaN → NaN guard → velocity UNCHANGED.
// We assert EVERY particle's post-velocity equals its seed EXACTLY (maxVelDelta==0) AND that no NaN leaked into
// the buffer (the guard must SUPPRESS the write, not write NaN). This is hand-SET params so it is BLIND to the
// NodeSpec default — TOOTH 2 (cook-through) guards the default.
//
// ── TOOTH 2 (NodeSpec-default, NO-FIELD parity THROUGH THE REAL COOK — rule 4, the gap-plug) ─────────────────
// Cook the production rig RadialPoints -> ParticleSystem(+FieldVolumeForce, NO Field wired) -> DrawPoints with
// the FieldVolumeForce node carrying NO param override → the cook resolves the production NodeSpec DEFAULTS
// (resolveNodeParams → cookInputParam → fillFieldVolumeForceParams, applying the .t3 forks) AND, with no Field
// tree, dispatches the baked no-op kernel. We compare that production cook to a NO-FORCE baseline cook (the SAME
// rig with the FieldVolumeForce node and its connection removed). Because the no-field force is a faithful no-op,
// the two MUST be bit-identical (the force changes NOTHING). no-bug expects prod==baseline (maxPosDelta==0);
// injectBug flips the expectation to "the force MOVED particles off baseline" (asserts maxPosDelta > a real
// threshold) — which is FALSE for the faithful no-op, so it goes RED. Because this runs THROUGH the cook, a
// NodeSpec / dropped-NaN-guard / baked-fallback regression (invisible to a hand-set kernel tooth) is caught.
//
// ── TOOTH 3 (offline-render determinism, through the REAL cook) ──────────────────────────────────────────────
// FieldVolumeForce has no Phase / no wall-clock term, so two cooks with IDENTICAL inputs and frame schedule but
// DIFFERENT ctx.time must be bit-identical (the offline-render determinism contract; cf. the Phase=time bug
// TurbulenceForce had). We cook the prod rig twice with different time0 and assert maxPosDelta==0. GREEN in both
// legs — it documents that, like DirectionalForce, this force needed no wall-clock un-binding.
//
// injectBug LEG: TOOTH 2 flips its expectation from "no-op (prod==baseline)" to "force moved particles", which
// the faithful no-op cannot satisfy → RED. no-bug → prod==baseline → GREEN. So the gate reads no-bug GREEN ↔
// injectBug RED (--bite-collectable).
//
// DEFERRED (named): the WIRED-SDF bounce/attract/repel behavior (the field path, *0.425 fork, reflect math) is
// already guarded by fieldvolumeforce_field_golden.cpp — this golden intentionally does NOT re-cover it; it owns
// the complementary NO-FIELD baked no-op + NodeSpec-default cook-through contract.
//
// ZONE: shell tier (app/src/ root, like directional_force_parity_golden.cpp / turbulence_parity_golden.cpp).
// Crosses runtime (PointGraph cook + the baked kernel). NO production edits — FieldVolumeForce was already faithful.
#include "runtime/point_graph.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "parity_golden_harness.h"
#include "runtime/force_params.h"  // FieldVolumeForceParams, FORCE_Particles/FORCE_Params
#include "runtime/graph.h"         // Graph/Node/pinId
#include "runtime/tixl_point.h"    // Particle / SwPoint (64B)

namespace sw {
namespace {

// TiXL FieldVolumeForce.t3 DefaultValue constants (the parity anchors, rule 2) AFTER the cook's host forks
// (point_ops_forceparams.cpp:86-102). These are the SHADER-ready values the production NodeSpec default cook
// produces with no field wired.
constexpr float kAmount = 1.0f;            // FieldVolumeForce.t3 Amount DefaultValue (line 30)
constexpr float kAttractionRaw = 0.2f;     // FieldVolumeForce.t3 Attraction DefaultValue (line 6)
constexpr float kAttractionFork = 0.425f;  // .t3 FloatsToBuffer Multiply node B on the Attraction path
constexpr float kRepulsion = 0.1f;         // FieldVolumeForce.t3 Repulsion DefaultValue (line 10)
constexpr float kBounciness = 1.0f;        // FieldVolumeForce.t3 Bounciness DefaultValue (line 34)
constexpr float kNormalSampling = 0.1f;    // FieldVolumeForce.t3 NormalSamplingDistance DefaultValue (line 22)

// Dispatch the production BAKED field_volume_force kernel on N particles seeded with the given velocities;
// return the post-particles. Mirrors dispatchDir in directional_force_parity_golden.cpp.
std::vector<Particle> dispatchFieldVolume(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                          const FieldVolumeForceParams& P,
                                          const std::vector<SW_PACKED3>& seedVel) {
  std::vector<Particle> out;
  MTL::Function* fn = lib->newFunction(NS::String::string("field_volume_force", NS::UTF8StringEncoding));
  if (!fn) return out;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return out;

  const uint32_t N = (uint32_t)seedVel.size();
  MTL::Buffer* buf = dev->newBuffer((NS::UInteger)N * sizeof(Particle), MTL::ResourceStorageModeShared);
  Particle* p = static_cast<Particle*>(buf->contents());
  for (uint32_t i = 0; i < N; ++i) {
    p[i] = Particle{};
    p[i].Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
    p[i].BirthTime = 0.0f;  // emitted (not NaN) -> passes the kernel's BirthTime guard
    p[i].Position = SW_PACKED3{0.3f * (float)i, -0.1f * (float)i, 0.05f * (float)i};  // distinct, irrelevant (baked field is constant)
    p[i].Velocity = seedVel[i];  // KNOWN non-zero velocity (TRANSFORM-type: the no-op must PRESERVE it)
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

// ---- TOOTH 2/3 cook capture (RadialPoints -> ParticleSystem(+/-FieldVolumeForce) -> DrawPoints) ----
std::vector<SwPoint>* g_fvCap = nullptr;
void captureFV(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_fvCap || !pts || c.count == 0) return;
  g_fvCap->assign(c.count, SwPoint{});
  std::memcpy(g_fvCap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

// Cook the FieldVolume rig for `steps` frames from a fixed time base time0 + i*dt; return the captured
// positions.
//   • withForce == true  -> RadialPoints -> ParticleSystem(+FieldVolumeForce, NO Field wired, NodeSpec
//                           DEFAULTS) -> DrawPoints. The cook reads the production NodeSpec defaults and, with
//                           no field tree, dispatches the BAKED no-op kernel. This is the production path the
//                           gate guards.
//   • withForce == false -> the SAME rig with the FieldVolumeForce node + its connection REMOVED (no force).
//                           The baseline a faithful no-op must reproduce exactly.
// Two calls with the SAME inputs but DIFFERENT time0 expose any wall-clock dependence (TOOTH 3).
std::vector<SwPoint> cookFVRig(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                               float time0, int steps, bool withForce) {
  registerBuiltinPointOps();
  std::vector<SwPoint> captured;
  g_fvCap = &captured;
  registerDrawOp("DrawPoints", captureFV);
  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 256.0f; gen.params["Radius"] = 2.0f;  // pinned scene (rule 5): explicit, not cook-default
  Node sim; sim.id = 2; sim.type = "ParticleSystem";
  Node drw; drw.id = 3; drw.type = "DrawPoints";
  g.nodes.push_back(gen); g.nodes.push_back(sim);
  if (withForce) {
    Node force; force.id = 4; force.type = "FieldVolumeForce";  // NO param override -> NodeSpec defaults; NO Field wired
    g.nodes.push_back(force);
  }
  g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points -> emit
  if (withForce)
    g.connections.push_back({102, pinId(4, 0), pinId(2, 1)});  // FieldVolumeForce.force -> forces (NO Field input)
  g.connections.push_back({103, pinId(2, 2), pinId(3, 0)});  // result -> DrawPoints

  const int targetId = pg.defaultDrawTarget(g);
  for (int i = 0; i < steps; ++i) {
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)i;          // SAME frame schedule both calls (cycle buffer identical)
    ctx.time = time0 + 0.05f * (float)i;   // the ONLY difference between the two TOOTH-3 cooks
    ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, targetId);
  }
  g_fvCap = nullptr;
  return captured;
}

// Max per-point position delta between two captured bags (L1 over xyz). Used both for the no-op equality
// (prod == baseline) and the determinism check.
double maxPosDelta(const std::vector<SwPoint>& a, const std::vector<SwPoint>& b) {
  size_t n = std::min(a.size(), b.size());
  double m = 0.0;
  for (size_t i = 0; i < n; ++i) {
    if (std::isnan(a[i].Position.x) || std::isnan(b[i].Position.x)) continue;
    double d = std::fabs((double)a[i].Position.x - b[i].Position.x) +
               std::fabs((double)a[i].Position.y - b[i].Position.y) +
               std::fabs((double)a[i].Position.z - b[i].Position.z);
    if (d > m) m = d;
  }
  return m;
}

}  // namespace

int runFieldVolumeForceParitySelfTest(bool injectBug) {
  ParityHarness h;
  ParityReport rep("selftest-fieldvolumeforce-parity");
  if (!h.ok()) {
    printf("[selftest-fieldvolumeforce-parity] FAIL: no metallib\n");
    return 1;
  }

  // ── TOOTH 1: BAKED-KERNEL NO-OP contract (direct-kernel, TiXL closed-form) ────────────────────────
  // No field wired -> baked field_volume_force.metal: constant all-ones field -> GetNormal=normalize(0)=NaN
  // -> force=NaN -> velocity=NaN -> the isnan guard (FieldVolumeForce.hlsl:149-150) BLOCKS the write. So
  // every particle's velocity is PRESERVED EXACTLY. Seed DISTINCT non-zero velocities (TRANSFORM-type input:
  // a zero seed would make the no-op vacuous). Params are the NodeSpec/.t3 defaults AFTER the host forks.
  const uint32_t N = 1024;
  std::vector<SW_PACKED3> seedVel(N);
  for (uint32_t i = 0; i < N; ++i) {
    float a = 6.2831853f * (float)i / (float)N;
    // Distinct, non-zero, well-spread seed velocities so a leaked write of ANY single component shows up.
    seedVel[i] = SW_PACKED3{0.7f * std::cos(a) + 0.11f,
                            0.5f * std::sin(a * 1.3f) - 0.07f,
                            0.3f * std::cos(a * 0.7f) + 0.05f};
  }

  FieldVolumeForceParams k{};
  k.Amount = kAmount;
  k.Attraction = kAttractionRaw * kAttractionFork;  // 0.2 * 0.425 = 0.085 (the cook's Multiply fork)
  k.AttractionDecay = 0.0f;
  k.Repulsion = kRepulsion;
  k.Bounciness = kBounciness;
  k.RandomizeBounce = 0.0f;
  k.RandomizeReflection = 0.0f;
  k.InvertVolumeFactor = 1.0f;  // InvertVolume=false -> +1 (BoolToFloat fork)
  k.NormalSamplingDistance = kNormalSampling;
  k.SpeedFactor = 1.0f;         // fork-FieldVolume-speedfactor
  k.EnableBounce = 1.0f;        // ReflectOnCollision=true (irrelevant: no crossing in a constant field)
  k.ApplyColorOnCollision = 0.0f;
  k.Count = N;

  std::vector<Particle> post = dispatchFieldVolume(h.dev, h.queue, h.lib, k, seedVel);

  // The kernel ran (non-empty readback, same size as the seed).
  rep.expectTrue("kernelRan(post.size==N)", post.size() == (size_t)N, (double)post.size());

  // No-op contract: post-velocity == seed velocity EXACTLY (NaN-guarded no-op), AND no NaN leaked. A dropped
  // NaN guard or a degenerate-normal write would change velocity or stamp NaN -> RED.
  double maxVelDelta = 0.0; bool anyNan = false; size_t cmp = std::min(post.size(), (size_t)N);
  for (size_t i = 0; i < cmp; ++i) {
    const SW_PACKED3& s = seedVel[i];
    const Particle& p = post[i];
    if (std::isnan(p.Velocity.x) || std::isnan(p.Velocity.y) || std::isnan(p.Velocity.z)) { anyNan = true; continue; }
    double d = std::fabs((double)p.Velocity.x - s.x) + std::fabs((double)p.Velocity.y - s.y) +
               std::fabs((double)p.Velocity.z - s.z);
    if (d > maxVelDelta) maxVelDelta = d;
  }
  // FieldVolumeForce.hlsl:149-150 — the guard SUPPRESSES the NaN write, so velocity is bit-preserved.
  rep.expect("bakedNoOp(maxVelDelta==0)", maxVelDelta, 0.0, 1e-6);
  rep.expectTrue("bakedNoOp(no NaN leaked)", !anyNan, anyNan ? 1.0 : 0.0);

  // ── TOOTH 2: NODESPEC-DEFAULT, NO-FIELD parity THROUGH THE REAL COOK (the模板盲區 plug, rule 4) ─────
  // Cook the prod rig (FieldVolumeForce, NO Field wired, NodeSpec defaults) and the no-force baseline; the
  // faithful no-op makes them bit-identical. no-bug expects prod==baseline; injectBug expects the force to
  // have MOVED particles (false for the no-op) -> RED.
  const int kSteps = 8;
  std::vector<SwPoint> baseline = cookFVRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*withForce=*/false);  // no force
  std::vector<SwPoint> prodDef  = cookFVRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*withForce=*/true);   // FieldVolumeForce, no field, NodeSpec defaults

  // Structure: both cooks produced a non-empty bag of the SAME size (same rig/pool; the force only modifies
  // velocity, never the pool size). Well-posed per-particle comparison.
  rep.expectTrue("cookBagSize(prod==baseline>0)",
                 prodDef.size() > 0 && prodDef.size() == baseline.size(), (double)prodDef.size());

  double dProdVsBase = maxPosDelta(prodDef, baseline);
  // THE NO-FIELD NODESPEC TOOTH: a faithful no-op force changes NOTHING, so the prod cook must match the
  // no-force baseline EXACTLY.
  //   no-bug   -> expect prod == baseline (maxPosDelta == 0): the no-field force is a faithful no-op.
  //   injectBug-> expect the force MOVED particles (maxPosDelta > kMoveThresh): FALSE for the no-op -> RED.
  const double kMoveThresh = 1e-3;  // a real displacement the no-op cannot produce
  if (injectBug) {
    // FALSE assertion for the faithful production: "the no-field force visibly moved particles off baseline".
    rep.expectTrue("prodMovedOffBaseline(injectBug)", dProdVsBase > kMoveThresh, dProdVsBase);
  } else {
    // The faithful no-op: prod cook bit-identical to the no-force baseline.
    rep.expect("prodNoField==baseline(no-op)", dProdVsBase, 0.0, 1e-6);
  }

  // ── TOOTH 3: offline-render determinism through the real cook ────────────────────────────────────
  // FieldVolumeForce has no Phase / no wall-clock term, so identical inputs at different ctx.time must be
  // bit-identical (unlike the pre-fix TurbulenceForce Phase=time). GREEN in both legs.
  std::vector<SwPoint> cookA = cookFVRig(h.dev, h.queue, h.lib, /*time0=*/0.0f, /*steps=*/8, /*withForce=*/true);
  std::vector<SwPoint> cookB = cookFVRig(h.dev, h.queue, h.lib, /*time0=*/10.0f, /*steps=*/8, /*withForce=*/true);
  rep.expect("cookDeterministic(maxPosDelta==0)", maxPosDelta(cookA, cookB), 0.0, 1e-5);

  return rep.finish();
}

}  // namespace sw
