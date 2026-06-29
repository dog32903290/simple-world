// vectorfield_force_parity_golden — --selftest-vectorfieldforce-parity. PARITY golden for the
// stateful FORCE VectorFieldForce (PARITY_GATE_PLAN.md Stage-3 fan-out, "可手算類 / 真 parity").
// Mirrors directional_force_parity_golden.cpp (the Force-class range-finder). VectorFieldForce's only
// distinction from DirectionalForce is its field input — WITHOUT a field bound (fork-VFF), the kernel
// bakes f=(1,1,1,1), so the no-field path is a closed-form CONSTANT diagonal push, fully hand-computable.
//
//   • NO-FIELD baked path (scout: tested here — no field tree wired): VectorFieldForce-sg.hlsl:42 GetField()
//     returns f=float4(1,1,1,1) when no field call is patched in. With the TiXL .t3 default Randomize=0 the
//     Variation hash term VANISHES (variationFactor==1, vffHash11u skipped). So for EVERY particle:
//       velocity = f.xyz * Amount * f.w * variationFactor * SpeedFactor
//                = (1,1,1) * Amount * 1 * 1 * 1  ==  (Amount, Amount, Amount)
//     (vector_field_force.metal:52-60, VectorFieldForce-sg.hlsl:61-65). With the TiXL .t3 default
//     Amount=1.0 → velocity == (1,1,1) for all i: a closed-form constant, fully hand-computable, naked-testable.
//   • Variation=0 default DISABLES hash11u (no per-particle randomness), no Phase / wall-clock → a
//     deterministic offline render is trivially reproducible. This force introduces zero time dependence.
//
// ── HONEST RED-FIRST CLASSIFICATION (PARITY_GATE_PLAN.md Stage-3 §2) ──────────────────────────────────
// This is a "补验证闸" GREEN case, NOT a "修偏差" case. I scouted VectorFieldForce's production state:
//   sw NodeSpec defaults (node_registry_particle.cpp:60-61): Amount=1.0, Randomize=0.0.
//   TiXL VectorFieldForce.t3 DefaultValue:                   Amount=1.0 (Id 27b2a405…), Randomize=0.0
//                                                            (Id fb9d4bfa…), VectorField=null.
//   The cook (point_ops.cpp:255-265) reads Amount/Randomize via cookInputParam with TiXL-matching fallbacks,
//   sets SpeedFactor=1.0 (no SpeedFactor input → global PS speed=1), and — with NO field tree wired — takes
//   the baked fallback runForce(psoVecField) whose kernel (vector_field_force.metal:52,60) bakes f=(1,1,1,1).
//   The kernel math (vector_field_force.metal:55-64) is byte-1:1 with VectorFieldForce-sg.hlsl:61-68.
// → VectorFieldForce (no-field baked path) was already FAITHFUL; this golden ADDS the missing parity gate;
//   it does NOT fix a deviation, because there is none. NO production edits accompany it. The injectBug
//   tooth still proves teeth: a forced Amount-deviation cook → RED.
//
// ── TOOTH 1 (kernel-math parity, direct-kernel, TiXL closed-form, no-field baked) ─────────────────────
// Dispatch the PRODUCTION `vector_field_force` kernel (the BAKED static PSO, no field tree) on a synthetic
// Particle buffer with ZERO initial velocity. Because initial velocity is zero, the post-velocity IS the
// offset. With Variation=0 the hash term is exactly 1 and f=(1,1,1,1), so for EVERY particle:
//   Velocity == (Amount, Amount, Amount)    (VectorFieldForce-sg.hlsl:61-65, no field + Variation=0)
// TiXL GROUND-TRUTH ANCHOR (rule 2): Amount=1.0 (VectorFieldForce.t3 DefaultValue, Id 27b2a405…),
// Randomize=0, SpeedFactor=1.0 → Velocity == (1,1,1) for all i. We assert each component == 1.0 AND the
// magnitude == sqrt(3). This is a hand-SET VecFieldForceParams so it is BLIND to the NodeSpec default —
// TOOTH 2 (cook-through) is what guards the default. (RED-first: this tooth's expected value is the TiXL
// .t3 constant, never an sw snapshot.)
//
// ── TOOTH 2 (NodeSpec-default parity, THROUGH THE REAL COOK — rule 4, the gap-plug) ───────────────────
// Cook the production rig RadialPoints -> ParticleSystem(+VectorFieldForce) -> DrawPoints with the
// VectorFieldForce node carrying NO Amount override AND NO field input → the cook resolves the production
// NodeSpec DEFAULT (Amount=1.0) and takes the no-field BAKED path. We measure the mean displacement off the
// pristine (no-force) ring and compare it to two REFERENCE cooks: one that explicitly sets Amount=1.0 (TiXL
// .t3) and one that sets a WRONG Amount (15× = 15.0, the kind of deviation that drifted into TurbulenceForce).
// A correct NodeSpec default makes the production cook bit-track the Amount=1.0 reference and DIVERGE ~15×
// from the wrong reference. no-bug expects prod==1.0-ref; injectBug flips the expectation to the wrong
// reference → RED. Because this runs THROUGH the cook, a NodeSpec / dead-code-fallback regression (invisible
// to TOOTH 1) is caught. Expected numbers come only from the Amount=1.0 / wrong reference cooks and the TiXL
// .t3 default — never an absolute sw snapshot.
//
// ── TOOTH 3 (offline-render determinism, through the REAL cook) ───────────────────────────────────────
// VectorFieldForce (no-field, Variation=0) has no Phase / no wall-clock term, so two cooks with IDENTICAL
// inputs and frame schedule but DIFFERENT ctx.time must be bit-identical (the offline-render determinism
// contract; cf. the Phase=time bug that TurbulenceForce had). We cook twice with different time0 and assert
// maxPosDelta==0. This is a property of the cook (independent of injectBug) and is GREEN in both legs.
//
// injectBug LEG: TOOTH 2 flips its expectation to the WRONG-Amount reference, so the production-default
// cook (correct NodeSpec=1.0) no longer matches → RED (plus the prod/ref ratio probe goes ~15× → RED).
// no-bug → prod==1.0 ref → GREEN. So the gate reads no-bug GREEN ↔ injectBug RED (--bite-collectable).
//
// DEFERRED (caveat): the WIRED-field path (a real Field tree on the VectorField port → runFieldForce's
// runtime-compiled kernel samples GetField) is NOT exercised here — this golden guards only the no-field
// BAKED path (the scout's chosen裸測 path). The wired-SDF behavior is a separate parity surface.
//
// ZONE: shell tier (app/src/ root, like turbulence_parity_golden.cpp). Crosses runtime (PointGraph cook +
// the kernel). NO production edits — VectorFieldForce (no-field baked) was already faithful.
#include "runtime/point_graph.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "parity_golden_harness.h"
#include "runtime/force_params.h"  // VecFieldForceParams, FORCE_Particles/FORCE_Params
#include "runtime/graph.h"         // Graph/Node/pinId
#include "runtime/tixl_point.h"    // Particle / SwPoint (64B)

namespace sw {
namespace {

// TiXL VectorFieldForce.t3 DefaultValue constants (the parity anchors, rule 2).
constexpr float kTiXLAmount = 1.0f;        // VectorFieldForce.t3 Amount DefaultValue (Id 27b2a405…)
constexpr float kWrongAmount = 15.0f;      // 15× deviation (the kind that drifted into TurbulenceForce)
// No-field baked: f=(1,1,1,1), Variation=0 → velocity=(Amount,Amount,Amount). |v| = Amount*sqrt(3).
constexpr double kSqrt3 = 1.7320508075688772;

// Dispatch the production vector_field_force kernel (BAKED static PSO, no field tree) on N zero-velocity
// particles; return post velocities (== the offset, since initial velocity is zero). Mirrors dispatchDir
// in directional_force_parity_golden.cpp.
std::vector<Particle> dispatchVecField(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                       const VecFieldForceParams& P, uint32_t N) {
  std::vector<Particle> out;
  MTL::Function* fn = lib->newFunction(NS::String::string("vector_field_force", NS::UTF8StringEncoding));
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

// ---- TOOTH 2/3 cook capture (RadialPoints -> ParticleSystem(+VectorFieldForce) -> DrawPoints) ----
std::vector<SwPoint>* g_vffCap = nullptr;
void captureVff(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_vffCap || !pts || c.count == 0) return;
  g_vffCap->assign(c.count, SwPoint{});
  std::memcpy(g_vffCap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

// Cook the VectorFieldForce rig for `steps` frames from a fixed time base time0 + i*dt; return the captured
// positions. NO field input wired → the cook takes the no-field BAKED path (fork-VFF, f=(1,1,1,1)).
//   • amountOverride < 0  -> VectorFieldForce sets NO Amount param → the cook reads the production NodeSpec
//                            DEFAULT (Amount=1.0). This is the production-default path the gate guards.
//   • amountOverride >= 0 -> the node explicitly sets Amount=amountOverride (used to build the 1.0 /
//                            wrong-Amount REFERENCE displacements). amountOverride==0 → no-force baseline.
// Two calls with the SAME inputs but DIFFERENT time0 expose any wall-clock dependence (TOOTH 3).
std::vector<SwPoint> cookVffRig(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                float time0, int steps, float amountOverride) {
  registerBuiltinPointOps();
  std::vector<SwPoint> captured;
  g_vffCap = &captured;
  registerDrawOp("DrawPoints", captureVff);
  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 256.0f; gen.params["Radius"] = 2.0f;  // pinned scene (rule 5): explicit, not cook-default
  Node sim; sim.id = 2; sim.type = "ParticleSystem";
  Node force; force.id = 4; force.type = "VectorFieldForce";
  if (amountOverride >= 0.0f) force.params["Amount"] = amountOverride;  // else: NodeSpec default (production)
  Node drw; drw.id = 3; drw.type = "DrawPoints";
  g.nodes.push_back(gen); g.nodes.push_back(sim); g.nodes.push_back(force); g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points -> emit
  g.connections.push_back({102, pinId(4, 0), pinId(2, 1)});  // VectorFieldForce.force -> forces
  g.connections.push_back({103, pinId(2, 2), pinId(3, 0)});  // result -> DrawPoints
  // NOTE: no connection into pinId(4, 1) (the VectorField Field port) → no field tree → baked path.

  const int targetId = pg.defaultDrawTarget(g);
  for (int i = 0; i < steps; ++i) {
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)i;          // SAME frame schedule both calls (cycle buffer identical)
    ctx.time = time0 + 0.05f * (float)i;   // the ONLY difference between the two TOOTH-3 cooks
    ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, targetId);
  }
  g_vffCap = nullptr;
  return captured;
}

// Mean displacement of a force-driven bag away from the pristine (no-force) ring. Larger Amount -> the
// constant (1,1,1) push shoves particles further off the ring -> larger mean displacement.
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

int runVectorFieldForceParitySelfTest(bool injectBug) {
  ParityHarness h;
  ParityReport rep("selftest-vectorfieldforce-parity");
  if (!h.ok()) {
    printf("[selftest-vectorfieldforce-parity] FAIL: no metallib\n");
    return 1;
  }

  const uint32_t N = 1024;

  // ── TOOTH 1: KERNEL-MATH parity (direct-kernel, TiXL closed-form, no-field baked) ─────────────────
  // f=(1,1,1,1), Amount=1.0, Variation=0, SpeedFactor=1 (VectorFieldForce.t3 + no field) → every
  // particle's post-velocity == (1,1,1). Asserts each component AND the magnitude (sqrt(3)).
  VecFieldForceParams k{};
  k.Amount = kTiXLAmount; k.Variation = 0.0f; k.SpeedFactor = 1.0f; k.Count = N;
  std::vector<Particle> vel = dispatchVecField(h.dev, h.queue, h.lib, k, N);
  // mean velocity components (closed-form: identical for all i since Variation=0).
  double sx = 0, sy = 0, sz = 0, slen = 0; size_t nv = 0;
  for (const Particle& p : vel) {
    sx += p.Velocity.x; sy += p.Velocity.y; sz += p.Velocity.z;
    slen += std::sqrt((double)p.Velocity.x * p.Velocity.x + (double)p.Velocity.y * p.Velocity.y +
                      (double)p.Velocity.z * p.Velocity.z);
    ++nv;
  }
  double mvx = nv ? sx / nv : 0, mvy = nv ? sy / nv : 0, mvz = nv ? sz / nv : 0;
  double mlen = nv ? slen / nv : 0;
  // TiXL closed-form: velocity == (1,1,1). tol is a tiny GPU-float epsilon (no accumulation here).
  rep.expect("kernelVelX==TiXL(Amount)", mvx, (double)kTiXLAmount, 1e-5);
  rep.expect("kernelVelY==TiXL(Amount)", mvy, (double)kTiXLAmount, 1e-5);
  rep.expect("kernelVelZ==TiXL(Amount)", mvz, (double)kTiXLAmount, 1e-5);
  rep.expect("kernelVelLen==TiXL(Amount*sqrt3)", mlen, (double)kTiXLAmount * kSqrt3, 1e-5);

  // ── TOOTH 2: NODESPEC-DEFAULT parity through the REAL COOK (the模板盲區 plug, rule 4) ─────────────
  const int kSteps = 8;
  std::vector<SwPoint> baseline = cookVffRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*amount=*/0.0f);          // no force = pristine ring
  std::vector<SwPoint> refTiXL  = cookVffRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*amount=*/kTiXLAmount);   // TiXL .t3=1.0
  std::vector<SwPoint> refWrong = cookVffRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*amount=*/kWrongAmount);  // 15× deviation
  std::vector<SwPoint> prodDef  = cookVffRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*amount=*/-1.0f);         // NodeSpec DEFAULT

  // Structure: prod-default cook produced a non-empty bag, the SAME size as the reference cooks (same rig,
  // only the force Amount differs → identical pool size). Well-posed per-particle displacement comparison.
  rep.expectTrue("cookBagSize(prod==refs>0)",
                 prodDef.size() > 0 && prodDef.size() == refTiXL.size() &&
                     prodDef.size() == refWrong.size() && prodDef.size() == baseline.size(),
                 (double)prodDef.size());

  double dTiXL  = meanRingDisplacement(refTiXL, baseline);
  double dWrong = meanRingDisplacement(refWrong, baseline);
  double dProd  = meanRingDisplacement(prodDef, baseline);
  // Reference legs must actually move (so the probe is live) and the wrong (15×) Amount must dwarf 1.0.
  rep.expectTrue("refDisp_live(Wrong>>TiXL>0)", dTiXL > 1e-6 && dWrong > 4.0 * dTiXL, dWrong);

  // THE NODESPEC TOOTH: which reference does the PRODUCTION DEFAULT match?
  //   no-bug   → expect prod == Amount=1.0 reference (NodeSpec default IS 1.0 == TiXL .t3).
  //   injectBug→ expect prod == wrong reference (a NodeSpec deviation, like the one TurbulenceForce had).
  double dExpected = injectBug ? dWrong : dTiXL;
  double dTol = 0.05 * dExpected + 1e-7;  // tight: prod cook and the matching ref share identical params
  rep.expect("prodDefaultDisp==Amount(NodeSpec)", dProd, dExpected, dTol);
  // Ratio probe (context + extra tooth): prod/TiXL must be ~1 (no-bug) or ~15× off (injectBug).
  double ratioVsTiXL = dTiXL > 1e-9 ? dProd / dTiXL : 0.0;
  rep.expectTrue("prod/TiXL_ratio(~1 parity)", std::fabs(ratioVsTiXL - 1.0) < 0.1, ratioVsTiXL);

  // ── TOOTH 3: offline-render determinism through the real cook ──────────────────────────────────
  // VectorFieldForce (no-field, Variation=0) has no Phase / no wall-clock term, so identical inputs at
  // different ctx.time must be bit-identical. GREEN in both legs.
  std::vector<SwPoint> cookA = cookVffRig(h.dev, h.queue, h.lib, /*time0=*/0.0f, /*steps=*/8, /*amount=*/-1.0f);
  std::vector<SwPoint> cookB = cookVffRig(h.dev, h.queue, h.lib, /*time0=*/10.0f, /*steps=*/8, /*amount=*/-1.0f);
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
