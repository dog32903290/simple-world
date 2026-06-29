// axisstep_force_parity_golden — --selftest-axisstep-parity. PARITY golden for the stateful FORCE
// AxisStepForce (PARITY_GATE_PLAN.md Stage-3 fan-out, "可手算類 / 真 parity"). Sibling of
// directional_force_parity_golden.cpp — same 3-tooth + injectBug shape.
//
// AxisStepForce per particle (AxisStepForce.hlsl:33-56): hash41u(gi + Seed*1103515245) gives randForPos;
// hash41u(...+83339) gives randForEffects. `selected = randForPos.w < SelectRatio`. The chosen DIRECTION is
// a UNIT axis vector — abs(randForPos.zyx * AxisDistribution) picks the dominant of {x,y,z} → one of
// (1,0,0)/(0,1,0)/(0,0,1) (hlsl:39-41), then signed by (randForEffects.g<0.5?+1:-1) and scaled by
// StrengthDistribution and f (hlsl:43). With the TiXL .t3 defaults the math collapses to a CLOSED FORM:
//   ApplyTrigger=1, Strength=1, RandomizeStrength=0, AddOriginalVelocity=0, AxisDistribution=(1,1,1),
//   StrengthDistribution=(1,1,1), Seed=0, AxisSpace=0(ObjectSpace), SelectRatio=0.1 (AxisStepForce.t3).
//   → f = selected*1*(1+0*...) = selected ∈ {0,1}.
//   → direction = (±unit axis) * (1,1,1) * f.
//   → Velocity = lerp(origVel, origVel*0 + direction*f, 1*selected).
//   With origVel = 0 and selected∈{0,1} (so f²==f==selected):
//     selected==1 → Velocity = (±1 on exactly ONE axis), |Velocity| == 1.
//     selected==0 → Velocity == 0.
//   So EVERY post-velocity is either the zero vector or a signed UNIT-magnitude axis-aligned vector. This
//   is hash-independent and therefore hand-assertable WITHOUT re-deriving hash41u: magnitude ∈ {0,1},
//   nonzero ones are axis-aligned (exactly one nonzero component, |that|==1), and the SELECTED FRACTION
//   ≈ SelectRatio (the gate randForPos.w<0.1 over a uniform hash). The f² double-apply is a faithful .hlsl
//   quirk (axis_step_force.metal:59-63) — with default f∈{0,1} it is a no-op (1²==1), so it does NOT
//   perturb the closed form; the golden still asserts |Velocity|==1, which would catch an f² that became
//   e.g. f³ or dropped the unit-axis normalization.
//
// ── HONEST CLASSIFICATION (Stage-3 §2): GREEN 补验证闸, NOT 修偏差 ────────────────────────────────────
// I scouted AxisStepForce production: NodeSpec defaults (node_registry_particle.cpp:92-114 +
// fillAxisStepForceParams point_ops_forceparams.cpp:36-53) == AxisStepForce.t3 DefaultValue EXACTLY
// (ApplyTrigger=1/Strength=1/RandomizeStrength=0/SelectRatio=0.1/AxisDistribution=(1,1,1)/
// AddOriginalVelocity=0/StrengthDistribution=(1,1,1)/AxisSpace=0/Seed=0). The kernel (axis_step_force.metal)
// is byte-1:1 with AxisStepForce.hlsl incl. the f² quirk. So AxisStepForce is ALREADY FAITHFUL — it was
// merely NAKED (no parity gate). This golden ADDS the gate; it does NOT fix a deviation. NO production edits.
// The injectBug tooth still proves teeth (see below).
//
// ── TOOTH 1 (kernel-math parity, direct-kernel, TiXL closed-form) ─────────────────────────────────────
// Dispatch the PRODUCTION `axis_step_force` kernel on N zero-velocity particles with TiXL .t3 defaults.
// Assert the closed form: (a) every post-velocity magnitude ∈ {0, 1} (within GPU epsilon); (b) every
// NONZERO velocity is axis-aligned with |component|==1 (one axis exactly ±1, the other two 0); (c) the
// selected fraction (nonzero count / N) ≈ SelectRatio=0.1. Anchor: AxisStepForce.hlsl:36-56 + .t3 defaults.
//
// ── TOOTH 2 (NodeSpec-default parity, THROUGH THE REAL COOK — rule 4, the gap-plug) ───────────────────
// Cook RadialPoints -> ParticleSystem(+AxisStepForce) -> DrawPoints with the AxisStepForce node carrying NO
// param overrides → the cook resolves the production NodeSpec DEFAULTS (incl. SelectRatio=0.1). Off the
// pristine (no-force) ring, count the fraction of particles that MOVED (a particle is hit iff selected).
// The observable is the SelectRatio gate: with the default 0.1, ~10% of particles displace. We assert the
// moved-fraction ≈ SelectRatio=0.1. no-bug expects 0.1 (TiXL .t3); injectBug expects 1.0 (a NodeSpec
// deviation = "every particle hit", the kind of default-drift this gate guards) → RED. This runs THROUGH
// the cook so a NodeSpec / fallback regression invisible to TOOTH 1 is caught. Expected = TiXL .t3 SelectRatio.
//
// ── TOOTH 3 (offline-render determinism, through the REAL cook) ───────────────────────────────────────
// AxisStepForce has no Phase / wall-clock term (Seed is a fixed param, the hash is index-based), so two
// cooks with identical inputs/frame-schedule but DIFFERENT ctx.time must be bit-identical. Assert
// maxPosDelta==0. GREEN in both legs (property of the cook, independent of injectBug).
//
// injectBug LEG: TOOTH 2 flips its expected SelectRatio from 0.1 (TiXL .t3) to 1.0 → the production-default
// cook (correct 0.1 → ~10% moved) no longer matches the 1.0 expectation → RED. no-bug → 0.1 match → GREEN.
// So the gate reads no-bug GREEN ↔ injectBug RED (--bite-collectable).
//
// ZONE: shell tier (app/src/ root, like turbulence_parity_golden.cpp). Crosses runtime (PointGraph cook +
// the kernel). NO production edits — AxisStepForce was already faithful.
#include "runtime/point_graph.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "parity_golden_harness.h"
#include "runtime/force_params.h"  // AxisStepForceParams, FORCE_Particles/FORCE_Params
#include "runtime/graph.h"         // Graph/Node/pinId
#include "runtime/tixl_point.h"    // Particle / SwPoint (64B)

namespace sw {
namespace {

// TiXL AxisStepForce.t3 DefaultValue constants (the parity anchors, rule 2).
constexpr float kTiXLSelectRatio = 0.1f;   // AxisStepForce.t3 SelectRatio DefaultValue
constexpr float kTiXLStrength = 1.0f;      // AxisStepForce.t3 Strength DefaultValue
constexpr float kWrongSelectRatio = 1.0f;  // "every particle hit" deviation (the kind a default-drift causes)

// Dispatch the production axis_step_force kernel on N zero-velocity particles; return post velocities
// (== the offset, since initial velocity is zero). Mirrors dispatchDir in directional_force_parity_golden.cpp.
std::vector<Particle> dispatchAxis(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                   const AxisStepForceParams& P, uint32_t N) {
  std::vector<Particle> out;
  MTL::Function* fn = lib->newFunction(NS::String::string("axis_step_force", NS::UTF8StringEncoding));
  if (!fn) return out;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return out;

  MTL::Buffer* buf = dev->newBuffer((NS::UInteger)N * sizeof(Particle), MTL::ResourceStorageModeShared);
  Particle* p = static_cast<Particle*>(buf->contents());
  for (uint32_t i = 0; i < N; ++i) {
    p[i] = Particle{};
    p[i].Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};  // identity (ObjectSpace anyway, no rotation read)
    p[i].BirthTime = 0.0f;                               // emitted (not NaN)
    p[i].Position = SW_PACKED3{0.0f, 0.0f, 0.0f};
    p[i].Velocity = SW_PACKED3{0.0f, 0.0f, 0.0f};  // zero -> post-velocity IS the offset
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

// ---- TOOTH 2/3 cook capture (RadialPoints -> ParticleSystem(+AxisStepForce) -> DrawPoints) ----
std::vector<SwPoint>* g_axisCap = nullptr;
void captureAxis(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_axisCap || !pts || c.count == 0) return;
  g_axisCap->assign(c.count, SwPoint{});
  std::memcpy(g_axisCap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

// Cook the axis-step rig for `steps` frames from a fixed time base time0 + i*dt; return captured positions.
//   • selectOverride < 0  -> AxisStepForce sets NO param → the cook reads the production NodeSpec DEFAULTS
//                            (SelectRatio=0.1, ...). This is the production-default path the gate guards.
//   • selectOverride == 0 -> baseline: SelectRatio=0 → NO particle is selected → pristine (no-force) ring.
//   • selectOverride > 0  -> explicit SelectRatio override (unused in no-bug; kept for symmetry/baseline).
// Two calls with the SAME inputs but DIFFERENT time0 expose any wall-clock dependence (TOOTH 3).
std::vector<SwPoint> cookAxisRig(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                 float time0, int steps, float selectOverride) {
  registerBuiltinPointOps();
  std::vector<SwPoint> captured;
  g_axisCap = &captured;
  registerDrawOp("DrawPoints", captureAxis);
  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 256.0f; gen.params["Radius"] = 2.0f;  // pinned scene (rule 5): explicit, not cook-default
  Node sim; sim.id = 2; sim.type = "ParticleSystem";
  Node force; force.id = 4; force.type = "AxisStepForce";
  if (selectOverride >= 0.0f) force.params["SelectRatio"] = selectOverride;  // else: NodeSpec default (production)
  Node drw; drw.id = 3; drw.type = "DrawPoints";
  g.nodes.push_back(gen); g.nodes.push_back(sim); g.nodes.push_back(force); g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points -> emit
  g.connections.push_back({102, pinId(4, 0), pinId(2, 1)});  // AxisStepForce.force -> forces
  g.connections.push_back({103, pinId(2, 2), pinId(3, 0)});  // result -> DrawPoints

  const int targetId = pg.defaultDrawTarget(g);
  for (int i = 0; i < steps; ++i) {
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)i;          // SAME frame schedule both calls (cycle buffer identical)
    ctx.time = time0 + 0.05f * (float)i;   // the ONLY difference between the two TOOTH-3 cooks
    ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, targetId);
  }
  g_axisCap = nullptr;
  return captured;
}

// Fraction of particles whose position MOVED off the pristine (no-force) ring beyond eps. With the
// SelectRatio gate, only `selected` particles get a unit-axis velocity kick → only they displace. So the
// moved-fraction tracks SelectRatio. (Particles with NaN position on either side are skipped — well-posed.)
double movedFraction(const std::vector<SwPoint>& force, const std::vector<SwPoint>& baseline, double eps) {
  size_t n = std::min(force.size(), baseline.size());
  if (n == 0) return 0.0;
  size_t moved = 0, valid = 0;
  for (size_t i = 0; i < n; ++i) {
    const SwPoint& f = force[i];
    const SwPoint& b = baseline[i];
    if (std::isnan(f.Position.x) || std::isnan(b.Position.x)) continue;
    double d = std::sqrt((double)(f.Position.x - b.Position.x) * (f.Position.x - b.Position.x) +
                         (double)(f.Position.y - b.Position.y) * (f.Position.y - b.Position.y) +
                         (double)(f.Position.z - b.Position.z) * (f.Position.z - b.Position.z));
    if (d > eps) ++moved;
    ++valid;
  }
  return valid ? (double)moved / (double)valid : 0.0;
}

}  // namespace

int runAxisStepForceParitySelfTest(bool injectBug) {
  ParityHarness h;
  ParityReport rep("selftest-axisstep-parity");
  if (!h.ok()) {
    printf("[selftest-axisstep-parity] FAIL: no metallib\n");
    return 1;
  }

  const uint32_t N = 4096;  // large sample so the SelectRatio≈0.1 frequency assertion is statistically tight

  // ── TOOTH 1: KERNEL-MATH parity (direct-kernel, TiXL closed-form) ────────────────────────────────
  // TiXL .t3 defaults → every post-velocity is the zero vector OR a signed UNIT axis vector; selected
  // fraction ≈ SelectRatio=0.1 (AxisStepForce.hlsl:36-56 + .t3 defaults).
  AxisStepForceParams k{};
  k.ApplyTrigger = 1.0f;
  k.Strength = kTiXLStrength;
  k.RandomizeStrength = 0.0f;
  k.SelectRatio = kTiXLSelectRatio;
  k.AxisDistributionX = 1.0f; k.AxisDistributionY = 1.0f; k.AxisDistributionZ = 1.0f;
  k.AddOriginalVelocity = 0.0f;
  k.StrengthDistributionX = 1.0f; k.StrengthDistributionY = 1.0f; k.StrengthDistributionZ = 1.0f;
  k.Seed = 0.0f; k.AxisSpace = 0.0f; k.Count = N;
  std::vector<Particle> vel = dispatchAxis(h.dev, h.queue, h.lib, k, N);

  bool dispatched = vel.size() == N;
  size_t nSelected = 0;       // count of nonzero (selected) velocities
  bool allMagOk = true;       // every |Velocity| ∈ {0, 1}
  bool allAxisAlignedOk = true;  // every nonzero velocity is axis-aligned with the single |comp|==1
  const float magEps = 1e-4f;
  for (const Particle& p : vel) {
    double vx = p.Velocity.x, vy = p.Velocity.y, vz = p.Velocity.z;
    double mag = std::sqrt(vx * vx + vy * vy + vz * vz);
    if (mag <= magEps) continue;  // zero (unselected) — fine
    ++nSelected;
    // closed form: magnitude must be exactly 1 (signed unit axis).
    if (std::fabs(mag - 1.0) > 1e-3) allMagOk = false;
    // axis-aligned: exactly one component has |comp|≈1, the other two ≈0.
    int nNonzeroComp = (std::fabs(vx) > 1e-3) + (std::fabs(vy) > 1e-3) + (std::fabs(vz) > 1e-3);
    double maxComp = std::max(std::fabs(vx), std::max(std::fabs(vy), std::fabs(vz)));
    if (nNonzeroComp != 1 || std::fabs(maxComp - 1.0) > 1e-3) allAxisAlignedOk = false;
  }
  double selFrac = dispatched ? (double)nSelected / (double)N : 0.0;

  rep.expectTrue("kernelDispatched(N particles)", dispatched, (double)vel.size());
  rep.expectTrue("kernelVelMag∈{0,1}", dispatched && allMagOk, allMagOk ? 1.0 : 0.0);
  rep.expectTrue("kernelVelAxisAligned(|c|==1)", dispatched && allAxisAlignedOk, allAxisAlignedOk ? 1.0 : 0.0);
  // selected fraction ≈ SelectRatio (uniform hash gate randForPos.w<0.1). Loose band for hash sampling noise.
  rep.expect("kernelSelectedFrac≈TiXL(SelectRatio)", selFrac, (double)kTiXLSelectRatio, 0.03);

  // ── TOOTH 2: NODESPEC-DEFAULT parity through the REAL COOK (the模板盲區 plug, rule 4) ─────────────
  const int kSteps = 8;
  const double kMoveEps = 1e-4;  // a kicked particle moves >> this over 8 frames; a still one stays exactly put
  std::vector<SwPoint> baseline = cookAxisRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*select=*/0.0f);  // SelectRatio=0 -> no force
  std::vector<SwPoint> prodDef  = cookAxisRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*select=*/-1.0f); // NodeSpec DEFAULT (0.1)

  // Structure: prod-default cook produced a non-empty bag the SAME size as baseline (same rig, only the
  // force SelectRatio differs). Well-posed per-particle moved comparison.
  rep.expectTrue("cookBagSize(prod==baseline>0)",
                 prodDef.size() > 0 && prodDef.size() == baseline.size(),
                 (double)prodDef.size());

  double fracMoved = movedFraction(prodDef, baseline, kMoveEps);
  // THE NODESPEC TOOTH: the production DEFAULT SelectRatio determines the moved fraction.
  //   no-bug   → expect moved-fraction == SelectRatio=0.1 (NodeSpec default IS 0.1 == TiXL .t3).
  //   injectBug→ expect moved-fraction == 1.0 ("every particle hit", a NodeSpec deviation) → RED.
  double fracExpected = injectBug ? (double)kWrongSelectRatio : (double)kTiXLSelectRatio;
  // tol absorbs hash-gate sampling noise around the 0.1 expectation; a 0.1-vs-1.0 mismatch dwarfs it.
  rep.expect("prodMovedFrac==SelectRatio(NodeSpec)", fracMoved, fracExpected, 0.04);

  // ── TOOTH 3: offline-render determinism through the real cook ──────────────────────────────────
  // AxisStepForce has no Phase / wall-clock term (Seed fixed, hash index-based) → identical inputs at
  // different ctx.time must be bit-identical. GREEN in both legs.
  std::vector<SwPoint> cookA = cookAxisRig(h.dev, h.queue, h.lib, /*time0=*/0.0f, /*steps=*/8, /*select=*/-1.0f);
  std::vector<SwPoint> cookB = cookAxisRig(h.dev, h.queue, h.lib, /*time0=*/10.0f, /*steps=*/8, /*select=*/-1.0f);
  size_t n = std::min(cookA.size(), cookB.size());
  double maxPosDelta = 0.0;
  for (size_t i = 0; i < n; ++i) {
    const SwPoint& a = cookA[i];
    const SwPoint& b = cookB[i];
    if (std::isnan(a.Position.x) || std::isnan(b.Position.x)) continue;
    double d = std::fabs((double)a.Position.x - b.Position.x) +
               std::fabs((double)a.Position.y - b.Position.y) +
               std::fabs((double)a.Position.z - b.Position.z);
    if (d > maxPosDelta) maxPosDelta = d;
  }
  rep.expect("cookDeterministic(maxPosDelta==0)", maxPosDelta, 0.0, 1e-5);

  return rep.finish();
}

}  // namespace sw
