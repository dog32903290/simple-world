// directional_force_parity_golden — --selftest-directionalforce-parity. PARITY golden for the
// stateful FORCE DirectionalForce (PARITY_GATE_PLAN.md Stage-3 fan-out, "可手算類 / 真 parity").
// DirectionalForce is the CLEANEST force in the family — the perfect range-finder for the Force class:
//   • pure LINEAR push: Velocity += Direction * Amount * (1 + hash11(i)*RandomAmount) * SpeedFactor
//     (DirectionalForce.hlsl:23-24). With the TiXL .t3 default RandomAmount=0 the hash term VANISHES →
//     offset == Direction*Amount*SpeedFactor, a closed-form constant, fully hand-computable.
//   • NO field binding (unlike FieldDistance/FieldVolume), NO randomness (RandomAmount=0 default),
//     NO Phase / wall-clock (unlike TurbulenceForce). So a deterministic offline render is trivially
//     reproducible — this force introduces zero time dependence.
//
// ── HONEST RED-FIRST CLASSIFICATION (PARITY_GATE_PLAN.md Stage-3 §2) ──────────────────────────────────
// This is a "补验证闸" GREEN case, NOT a "修偏差" case. I scouted DirectionalForce's production state:
//   sw NodeSpec defaults (node_registry_particle.cpp:38-42): Amount=0.007, Direction=(0,-1,0), RandomAmount=0.
//   TiXL DirectionalForce.t3 DefaultValue:                    Amount=0.007, Direction=(0,-1,0), RandomAmount=0.
//   The cook (point_ops.cpp:245-254) reads every param via cookInputParam with TiXL-matching fallbacks and
//   sets SpeedFactor=1.0 (DirectionalForce.t3 has NO SpeedFactor input → the global PS speed=1). The kernel
//   math (directional_force.metal:36-38) is byte-1:1 with DirectionalForce.hlsl:23-24.
// → DirectionalForce was already FAITHFUL; it was merely NAKED (the existing --selftest-directionalforce
//   smoke "meanY<-0.05" is structurally blind — it hand-injects Amount=60 and passes for ANY nonzero push,
//   so it can neither see the NodeSpec default nor an amplitude error). This golden ADDS the missing
//   parity gate; it does NOT fix a deviation, because there is none. NO production edits accompany it.
//   The injectBug tooth still proves teeth: a forced Amount-deviation cook → RED.
//
// ── TOOTH 1 (kernel-math parity, direct-kernel, TiXL closed-form) ─────────────────────────────────────
// Dispatch the PRODUCTION `directional_force` kernel on a synthetic Particle buffer with ZERO initial
// velocity. Because initial velocity is zero, the post-velocity IS the offset. With RandomAmount=0 the
// hash term is exactly 1, so for EVERY particle:
//   Velocity == Direction * Amount * SpeedFactor    (DirectionalForce.hlsl:23-24, RandomAmount=0)
// TiXL GROUND-TRUTH ANCHOR (rule 2): Direction=(0,-1,0), Amount=0.007 (DirectionalForce.t3 DefaultValue),
// SpeedFactor=1.0 → Velocity == (0, -0.007, 0) for all i. We assert |Velocity| == 0.007 AND that it points
// down (meanY == -0.007, meanX==meanZ==0). This is a hand-SET TurbParams analog so it is BLIND to the
// NodeSpec default — TOOTH 2 (cook-through) is what guards the default. (RED-first: this tooth's expected
// value is the TiXL .t3 constant, never an sw snapshot.)
//
// ── TOOTH 2 (NodeSpec-default parity, THROUGH THE REAL COOK — rule 4, the gap-plug) ───────────────────
// Cook the production rig RadialPoints -> ParticleSystem(+DirectionalForce) -> DrawPoints with the
// DirectionalForce node carrying NO Amount override → the cook resolves the production NodeSpec DEFAULT
// (node_registry_particle.cpp → resolveNodeParams → cookInputParam → Amount=0.007). We measure the mean
// displacement off the pristine (no-force) ring and compare it to two REFERENCE cooks: one that explicitly
// sets Amount=0.007 (TiXL .t3) and one that sets a WRONG Amount (15× = 0.105, the kind of deviation that
// drifted into TurbulenceForce). A correct NodeSpec default makes the production cook bit-track the
// Amount=0.007 reference and DIVERGE ~15× from the wrong reference. no-bug expects prod==0.007-ref;
// injectBug flips the expectation to the wrong reference → RED. Because this runs THROUGH the cook, a
// NodeSpec / dead-code-fallback regression (invisible to TOOTH 1) is caught. Expected numbers come only
// from the Amount=0.007 / wrong reference cooks and the TiXL .t3 default — never an absolute sw snapshot.
//
// ── TOOTH 3 (offline-render determinism, through the REAL cook) ───────────────────────────────────────
// DirectionalForce has no Phase / no wall-clock term, so two cooks with IDENTICAL inputs and frame schedule
// but DIFFERENT ctx.time must be bit-identical (the offline-render determinism contract; cf. the Phase=time
// bug that TurbulenceForce had). We cook twice with different time0 and assert maxPosDelta==0. This is a
// property of the cook (independent of injectBug) and is GREEN in both legs — it documents that, unlike
// TurbulenceForce, this force needed no wall-clock un-binding.
//
// injectBug LEG: TOOTH 2 flips its expectation to the WRONG-Amount reference, so the production-default
// cook (correct NodeSpec=0.007) no longer matches → RED (plus the prod/ref ratio probe goes ~15× → RED).
// no-bug → prod==0.007 ref → GREEN. So the gate reads no-bug GREEN ↔ injectBug RED (--bite-collectable).
//
// ZONE: shell tier (app/src/ root, like turbulence_parity_golden.cpp). Crosses runtime (PointGraph cook +
// the kernel). NO production edits — DirectionalForce was already faithful.
#include "runtime/point_graph.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "parity_golden_harness.h"
#include "runtime/force_params.h"  // DirForceParams, FORCE_Particles/FORCE_Params
#include "runtime/graph.h"         // Graph/Node/pinId
#include "runtime/tixl_point.h"    // Particle / SwPoint (64B)

namespace sw {
namespace {

// TiXL DirectionalForce.t3 DefaultValue constants (the parity anchors, rule 2).
constexpr float kTiXLAmount = 0.007f;      // DirectionalForce.t3 Amount DefaultValue
constexpr float kTiXLDirY = -1.0f;         // DirectionalForce.t3 Direction.Y DefaultValue (push -Y)
constexpr float kWrongAmount = 0.105f;     // 15× deviation (the kind that drifted into TurbulenceForce)

// Dispatch the production directional_force kernel on N zero-velocity particles; return post velocities
// (== the offset, since initial velocity is zero). Mirrors dispatchTurb in turbulence_parity_golden.cpp.
std::vector<Particle> dispatchDir(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                  const DirForceParams& P, uint32_t N) {
  std::vector<Particle> out;
  MTL::Function* fn = lib->newFunction(NS::String::string("directional_force", NS::UTF8StringEncoding));
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
    p[i].BirthTime = 0.0f;  // emitted (not NaN)
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

// ---- TOOTH 2/3 cook capture (RadialPoints -> ParticleSystem(+DirectionalForce) -> DrawPoints) ----
std::vector<SwPoint>* g_dirCap = nullptr;
void captureDir(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_dirCap || !pts || c.count == 0) return;
  g_dirCap->assign(c.count, SwPoint{});
  std::memcpy(g_dirCap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

// Cook the directional rig for `steps` frames from a fixed time base time0 + i*dt; return the captured
// positions.
//   • amountOverride < 0  -> DirectionalForce sets NO Amount param → the cook reads the production NodeSpec
//                            DEFAULT (Amount=0.007). This is the production-default path the gate guards.
//   • amountOverride >= 0 -> the node explicitly sets Amount=amountOverride (used to build the 0.007 /
//                            wrong-Amount REFERENCE displacements). amountOverride==0 → no-force baseline.
// Two calls with the SAME inputs but DIFFERENT time0 expose any wall-clock dependence (TOOTH 3).
std::vector<SwPoint> cookDirRig(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                float time0, int steps, float amountOverride) {
  registerBuiltinPointOps();
  std::vector<SwPoint> captured;
  g_dirCap = &captured;
  registerDrawOp("DrawPoints", captureDir);
  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 256.0f; gen.params["Radius"] = 2.0f;  // pinned scene (rule 5): explicit, not cook-default
  Node sim; sim.id = 2; sim.type = "ParticleSystem";
  Node force; force.id = 4; force.type = "DirectionalForce";
  if (amountOverride >= 0.0f) force.params["Amount"] = amountOverride;  // else: NodeSpec default (production)
  Node drw; drw.id = 3; drw.type = "DrawPoints";
  g.nodes.push_back(gen); g.nodes.push_back(sim); g.nodes.push_back(force); g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points -> emit
  g.connections.push_back({102, pinId(4, 0), pinId(2, 1)});  // DirectionalForce.force -> forces
  g.connections.push_back({103, pinId(2, 2), pinId(3, 0)});  // result -> DrawPoints

  const int targetId = pg.defaultDrawTarget(g);
  for (int i = 0; i < steps; ++i) {
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)i;          // SAME frame schedule both calls (cycle buffer identical)
    ctx.time = time0 + 0.05f * (float)i;   // the ONLY difference between the two TOOTH-3 cooks
    ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, targetId);
  }
  g_dirCap = nullptr;
  return captured;
}

// Mean displacement of a force-driven bag away from the pristine (no-force) ring. Larger Amount -> the
// constant -Y push shoves particles further off the ring -> larger mean displacement.
double meanRingDisplacement(const std::vector<SwPoint>& force, const std::vector<SwPoint>& baseline) {
  size_t n = std::min(force.size(), baseline.size());
  if (n == 0) return 0.0;
  double s = 0.0; size_t valid = 0;
  for (size_t i = 0; i < n; ++i) {
    const SwPoint& f = force[i];
    const SwPoint& b = baseline[i];
    if (std::isnan(f.Position.x) || std::isnan(b.Position.x)) continue;
    s += std::sqrt((double)(f.Position.x - b.Position.x) * (f.Position.x - b.Position.x) +
                   (double)(f.Position.y - b.Position.y) * (f.Position.y - b.Position.y) +
                   (double)(f.Position.z - b.Position.z) * (f.Position.z - b.Position.z));
    ++valid;
  }
  return valid ? s / (double)valid : 0.0;
}

}  // namespace

int runDirectionalForceParitySelfTest(bool injectBug) {
  ParityHarness h;
  ParityReport rep("selftest-directionalforce-parity");
  if (!h.ok()) {
    printf("[selftest-directionalforce-parity] FAIL: no metallib\n");
    return 1;
  }

  const uint32_t N = 1024;

  // ── TOOTH 1: KERNEL-MATH parity (direct-kernel, TiXL closed-form) ────────────────────────────────
  // Direction=(0,-1,0), Amount=0.007, RandomAmount=0, SpeedFactor=1 (DirectionalForce.t3) → every
  // particle's post-velocity == (0, -0.007, 0). Asserts magnitude AND direction (down, X/Z zero).
  DirForceParams k{};
  k.DirX = 0.0f; k.DirY = kTiXLDirY; k.DirZ = 0.0f;
  k.Amount = kTiXLAmount; k.RandomAmount = 0.0f; k.SpeedFactor = 1.0f; k.Count = N;
  std::vector<Particle> vel = dispatchDir(h.dev, h.queue, h.lib, k, N);
  // mean velocity components (closed-form: identical for all i since RandomAmount=0).
  double sx = 0, sy = 0, sz = 0, slen = 0; size_t nv = 0;
  for (const Particle& p : vel) {
    sx += p.Velocity.x; sy += p.Velocity.y; sz += p.Velocity.z;
    slen += std::sqrt((double)p.Velocity.x * p.Velocity.x + (double)p.Velocity.y * p.Velocity.y +
                      (double)p.Velocity.z * p.Velocity.z);
    ++nv;
  }
  double mvx = nv ? sx / nv : 0, mvy = nv ? sy / nv : 0, mvz = nv ? sz / nv : 0;
  double mlen = nv ? slen / nv : 0;
  // TiXL closed-form: velocity == (0, -0.007, 0). tol is a tiny GPU-float epsilon (no accumulation here).
  rep.expect("kernelVelY==TiXL(-Amount)", mvy, (double)(-kTiXLAmount), 1e-5);
  rep.expect("kernelVelX==0", mvx, 0.0, 1e-5);
  rep.expect("kernelVelZ==0", mvz, 0.0, 1e-5);
  rep.expect("kernelVelLen==TiXL(Amount)", mlen, (double)kTiXLAmount, 1e-5);

  // ── TOOTH 2: NODESPEC-DEFAULT parity through the REAL COOK (the模板盲區 plug, rule 4) ─────────────
  const int kSteps = 8;
  std::vector<SwPoint> baseline = cookDirRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*amount=*/0.0f);          // no force = pristine ring
  std::vector<SwPoint> refTiXL  = cookDirRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*amount=*/kTiXLAmount);   // TiXL .t3=0.007
  std::vector<SwPoint> refWrong = cookDirRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*amount=*/kWrongAmount);  // 15× deviation
  std::vector<SwPoint> prodDef  = cookDirRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*amount=*/-1.0f);         // NodeSpec DEFAULT

  // Structure: prod-default cook produced a non-empty bag, the SAME size as the reference cooks (same rig,
  // only the force Amount differs → identical pool size). Well-posed per-particle displacement comparison.
  rep.expectTrue("cookBagSize(prod==refs>0)",
                 prodDef.size() > 0 && prodDef.size() == refTiXL.size() &&
                     prodDef.size() == refWrong.size() && prodDef.size() == baseline.size(),
                 (double)prodDef.size());

  double dTiXL  = meanRingDisplacement(refTiXL, baseline);
  double dWrong = meanRingDisplacement(refWrong, baseline);
  double dProd  = meanRingDisplacement(prodDef, baseline);
  // Reference legs must actually move (so the probe is live) and the wrong (15×) Amount must dwarf 0.007.
  rep.expectTrue("refDisp_live(Wrong>>TiXL>0)", dTiXL > 1e-6 && dWrong > 4.0 * dTiXL, dWrong);

  // THE NODESPEC TOOTH: which reference does the PRODUCTION DEFAULT match?
  //   no-bug   → expect prod == Amount=0.007 reference (NodeSpec default IS 0.007 == TiXL .t3).
  //   injectBug→ expect prod == wrong reference (a NodeSpec deviation, like the one TurbulenceForce had).
  double dExpected = injectBug ? dWrong : dTiXL;
  double dTol = 0.05 * dExpected + 1e-7;  // tight: prod cook and the matching ref share identical params
  rep.expect("prodDefaultDisp==Amount(NodeSpec)", dProd, dExpected, dTol);
  // Ratio probe (context + extra tooth): prod/TiXL must be ~1 (no-bug) or ~15× off (injectBug).
  double ratioVsTiXL = dTiXL > 1e-9 ? dProd / dTiXL : 0.0;
  rep.expectTrue("prod/TiXL_ratio(~1 parity)", std::fabs(ratioVsTiXL - 1.0) < 0.1, ratioVsTiXL);

  // ── TOOTH 3: offline-render determinism through the real cook ──────────────────────────────────
  // DirectionalForce has no Phase / no wall-clock term, so identical inputs at different ctx.time must be
  // bit-identical (unlike the pre-fix TurbulenceForce Phase=time). GREEN in both legs.
  std::vector<SwPoint> cookA = cookDirRig(h.dev, h.queue, h.lib, /*time0=*/0.0f, /*steps=*/8, /*amount=*/-1.0f);
  std::vector<SwPoint> cookB = cookDirRig(h.dev, h.queue, h.lib, /*time0=*/10.0f, /*steps=*/8, /*amount=*/-1.0f);
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
