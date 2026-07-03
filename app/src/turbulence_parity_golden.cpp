// turbulence_parity_golden — --selftest-turbulence-parity. PARITY golden for the stateful FORCE
// TurbulenceForce (PARITY_GATE_PLAN.md Stage-1 pilot, "可手算類 / 真 parity"). This is the template's
// TEST OF STRENGTH: the EXISTING smoke (--selftest-simop "particles moved off the ring > 0.1") passes
// for BOTH Amount=15 (production default) AND Amount=1 (TiXL default) — a threshold assertion is
// structurally blind to a 15× amplitude error. This golden咬住 that exact gap.
//
// THE NOISE MATH IS FAITHFUL, ONLY THE DEFAULTS DRIFT: turbulence_force.metal:28 computes
//   amount = Amount/100 * fieldAmount(=1) * SpeedFactor;  velocity += curlNoise(noiseLookup) * amount
// — byte-identical to TurbulanceForce.hlsl:72-73. The divergence is purely (a) Amount default 15 vs
// TiXL .t3=1.0, (b) Frequency default 1.2 vs TiXL .t3=1.0, and (c) the COOK forcing Phase=wall-clock
// (point_ops.cpp:323 `tp.Phase = time`) over TiXL's Phase=user-input-default-0.
//
// ── TOOTH 1a (kernel-math parity, direct-kernel, TiXL-TRANSCRIBED oracle — P5 fix) ──────────────────
// We dispatch the PRODUCTION `turbulence_force` kernel directly on a fixed synthetic Particle buffer
// (known positions, ZERO initial velocity, FIXED Phase=0), so each particle's post-velocity IS its
// displacement Δv = curlNoise(noiseLookup) * (Amount/100). OLD SHAPE (P5 self-calibration, REMOVED): a
// second dispatch of the SAME kernel at Amount=100 "calibrated" the expectation — that only ever proved
// linearity in Amount (A==A), never that the noise math matches TiXL. NEW SHAPE (true-value anchors):
//   (i)  CLOSED-FORM magnitude — TiXL's curlNoise ends in normalize() (noise-functions.hlsl:282), so
//        |Δv| == Amount/100 exactly; at the TiXL .t3 default Amount=1 the mean speed must be 0.01.
//   (ii) NOISE-MATH parity — per-particle Δv is compared against a HOST transcription of TiXL's
//        snoise/snoiseVec3/curlNoise (noise-functions.hlsl:179-283, transcribed in tixl_noise_oracle.h,
//        NOT sw's noise.metal.h port) driven by the force formula TurbulanceForce.hlsl:64-65,72-73.
// This still HAND-SETS TurbParams, so it is BLIND to the NodeSpec default (the template-gap the refuter
// caught: a wrong NodeSpec default never reaches this tooth) — TOOTH 1b guards that.
//
// ── TOOTH 1b (NodeSpec-default parity, THROUGH THE REAL COOK) ─────────────────────────────────────────
// This is the gap-plug. We COOK the production rig (RadialPoints -> ParticleSystem(+TurbulenceForce) ->
// DrawPoints) with the TurbulenceForce node carrying NO Amount override, so the cook resolves the
// production NodeSpec DEFAULT (node_registry_particle.cpp → resolveNodeParams → cookInputParam). We
// measure the mean displacement off the pristine (no-force) ring and compare it to two REFERENCE cooks
// that explicitly set Amount=1 (TiXL .t3) and Amount=15 (pre-fix bug). A correct NodeSpec default makes
// the production cook track the Amount=1 reference and DIVERGE ~15× from Amount=15. Because this runs
// through the cook, a NodeSpec / dead-code-fallback regression — invisible to TOOTH 1a — is now caught.
// no-bug expects prod==Amount-1; injectBug expects prod==Amount-15 → RED. Expected numbers come only from
// the Amount=1/15 reference cooks and the TiXL .t3 default (Amount=1), never an absolute sw snapshot.
//
// ── TOOTH 2 (Phase determinism probe, through the REAL cook) ────────────────────────────────────────
// The amplitude tooth bypasses the host fill, so it cannot see the Phase=time bug. So we ALSO cook the
// real graph RadialPoints→ParticleSystem(+TurbulenceForce)→capture TWICE with IDENTICAL inputs but
// DIFFERENT ctx.time, and assert the two position readbacks are bit-identical. Because cookParticleSim
// sets tp.Phase = ctx.time unconditionally (point_ops.cpp:323), the noise lookup shifts with wall-clock
// → the two cooks DIFFER. TiXL's Phase is a user input (default 0, only animates if Time is wired), so a
// deterministic offline render MUST be reproducible for fixed inputs; this probe is that contract.
// Stage-3 fixed the cook to read Phase from the inspector param (default 0) instead of binding it to
// wall-clock (point_ops.cpp), so both cooks now match → maxPosDelta==0 → GREEN.
//
// injectBug LEG (standard sw tooth): TOOTH 1b flips its expectation to the Amount=15 reference, so the
// production-default cook (correct NodeSpec=1) no longer matches → RED (plus the prod/A1 ratio probe goes
// ~15× → RED). The no-bug leg expects Amount=1 and the corrected NodeSpec default cooks parity → GREEN.
// So the gate reads no-bug GREEN ↔ injectBug RED (--bite-collectable). TOOTH 2 is a property of the cook
// (independent of injectBug) and is GREEN in both legs.
//
// ZONE: shell tier (app/src/ root). Crosses runtime (PointGraph cook + the kernel). NO production edits.
#include "runtime/point_graph.h"

#include <cmath>
#include <cstring>
#include <map>
#include <vector>

#include "parity_golden_harness.h"
#include "runtime/force_params.h"  // TurbParams, FORCE_Particles/FORCE_Params
#include "runtime/graph.h"         // Graph/Node/pinId
#include "runtime/tixl_point.h"    // Particle / SwPoint (64B)
#include "tixl_noise_oracle.h"     // tixl_noise:: — TiXL noise-functions.hlsl transcription (TOOTH 1a oracle)

namespace sw {
namespace {

// Build N particles at deterministic spread-out positions (zero velocity, emitted), dispatch the
// production turbulence_force kernel with `P`, read back. Returns the post velocities (= the Δv, since
// initial velocity is zero). Self-contained (mirrors point_ops_selftest.cpp::dispatchForce).
std::vector<Particle> dispatchTurb(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                   const TurbParams& P, uint32_t N) {
  std::vector<Particle> out;
  MTL::Function* fn = lib->newFunction(NS::String::string("turbulence_force", NS::UTF8StringEncoding));
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
    // A deterministic, well-spread sample lattice so curlNoise is exercised across distinct lookups
    // (NOT the degenerate (-1,0,0) glitch coord — the kernel's *0.9 already guards it).
    float a = 6.2831853f * (float)i / (float)N;
    p[i].Position = SW_PACKED3{0.7f * std::cos(a), 0.5f * std::sin(a * 1.3f), 0.3f * std::cos(a * 0.7f)};
    p[i].Velocity = SW_PACKED3{0.0f, 0.0f, 0.0f};
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

double meanSpeed(const std::vector<Particle>& v) {
  if (v.empty()) return 0.0;
  double s = 0.0;
  for (const Particle& p : v)
    s += std::sqrt((double)p.Velocity.x * p.Velocity.x + (double)p.Velocity.y * p.Velocity.y +
                   (double)p.Velocity.z * p.Velocity.z);
  return s / (double)v.size();
}


// ---- TOOTH 2 cook capture (RadialPoints -> ParticleSystem(+Turbulence) -> DrawPoints) ----
std::vector<SwPoint>* g_turbCap = nullptr;
void captureTurb(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_turbCap || !pts || c.count == 0) return;
  g_turbCap->assign(c.count, SwPoint{});
  std::memcpy(g_turbCap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

// Cook the turbulence rig (RadialPoints -> ParticleSystem(+TurbulenceForce) -> DrawPoints) for `steps`
// frames using a fixed time base `time0 + i*dt`; return the captured position buffer.
//   • amountOverride < 0  -> the TurbulenceForce node sets NO Amount/Frequency param, so the cook reads
//                            the production NodeSpec DEFAULTS (Amount/Frequency from node_registry_particle
//                            via resolveNodeParams). This is the production-default path the gate guards.
//   • amountOverride >= 0 -> the node explicitly sets Amount=amountOverride (Frequency left default 1.0),
//                            used to build the Amount=1 / Amount=15 REFERENCE displacements.
// Two calls with the SAME inputs but DIFFERENT time0 expose the Phase=wall-clock bug (TOOTH 2).
std::vector<SwPoint> cookTurbRig(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                 float time0, int steps, float amountOverride = -1.0f) {
  registerBuiltinPointOps();
  std::vector<SwPoint> captured;
  g_turbCap = &captured;
  registerDrawOp("DrawPoints", captureTurb);
  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 256.0f; gen.params["Radius"] = 2.0f;
  Node sim; sim.id = 2; sim.type = "ParticleSystem";
  Node turb; turb.id = 4; turb.type = "TurbulenceForce";
  if (amountOverride >= 0.0f) turb.params["Amount"] = amountOverride;  // else: NodeSpec default (production)
  Node drw; drw.id = 3; drw.type = "DrawPoints";
  g.nodes.push_back(gen); g.nodes.push_back(sim); g.nodes.push_back(turb); g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(4, 0), pinId(2, 1)});
  g.connections.push_back({103, pinId(2, 2), pinId(3, 0)});

  const int targetId = pg.defaultDrawTarget(g);
  for (int i = 0; i < steps; ++i) {
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)i;          // SAME frame schedule both calls (cycle buffer identical)
    ctx.time = time0 + 0.05f * (float)i;   // the ONLY difference between the two cooks
    ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, targetId);
  }
  g_turbCap = nullptr;
  return captured;
}

// Mean displacement of a force-driven bag away from the pristine (no-force) ring. Larger Amount -> the
// turbulence shoves particles further off the ring -> larger mean displacement. Used to compare the
// PRODUCTION-DEFAULT cook against the Amount=1 / Amount=15 reference cooks.
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

int runTurbulenceParitySelfTest(bool injectBug) {
  ParityHarness h;
  ParityReport rep("selftest-turbulence-parity");
  if (!h.ok()) {
    printf("[selftest-turbulence-parity] FAIL: no metallib\n");
    return 1;
  }

  const uint32_t N = 1024;

  // ── TOOTH 1a: KERNEL-MATH parity vs the TiXL-TRANSCRIBED host oracle (P5 fix) ────────────────────
  // The old CALIBRATION dispatch (Amount=100 of the same kernel) is GONE — it only proved linearity in
  // Amount (A==A). Expectations now come from TiXL source only:
  //   TurbulanceForce.hlsl:64  pos = Position * 0.9
  //   TurbulanceForce.hlsl:65  noiseLookup = (pos + variationOffset(=0, Variation=0) + Phase(=0)*(1,-1,0)) * Frequency
  //   TurbulanceForce.hlsl:72-73  Δv = curlNoise(noiseLookup) * (Amount/100 * fieldAmount(=1, no field) * SpeedFactor(=1))
  //   noise-functions.hlsl:282    curlNoise is NORMALIZED → |Δv| == Amount/100 exactly.
  // Still hand-sets TurbParams (kernel-math anchor, NOT the cook); TOOTH 1b guards the NodeSpec default.
  TurbParams k1{};
  k1.Amount = 1.0f; k1.Frequency = 1.0f;  // TiXL .t3 defaults
  k1.Phase = 0.0f; k1.Variation = 0.0f; k1.SpeedFactor = 1.0f;
  k1.VariationGroupCount = 0.0f; k1.Count = N;
  std::vector<Particle> got1 = dispatchTurb(h.dev, h.queue, h.lib, k1, N);
  rep.expectTrue("kernelDispatch(bagN)", got1.size() == N, (double)got1.size());

  // (i) CLOSED-FORM magnitude anchor: mean |Δv| == Amount/100 == 0.01 (hlsl:282 normalize + :72-73).
  double meanLenKernel = meanSpeed(got1);
  rep.expect("meanSpeed==Amount/100(hlsl:282)", meanLenKernel, 1.0 / 100.0, 5e-4);

  // (ii) NOISE-MATH parity: per-particle Δv vs the tixl_noise transcription at the particle's own
  // (readback) position — zero initial velocity, so post-velocity IS Δv. ROBUST-FRACTION asserts, and
  // the gate is calibrated to the fp physics of the curl: snoise float jitter between two faithful
  // implementations was MEASURED at ~1e-5 (this transcription vs the independent
  // t3import_displacemeshnoise_oracle lineage over this exact lattice); the finite-difference divides
  // it by 2e=0.002 → raw-curl error ~1e-2 worst → after normalize, |Δv| error can reach ~1e-4..1e-3
  // where the raw curl is small. So the per-particle gate is 1e-3 (10% of |Δv|) with fraction ≥90% —
  // WRONG noise math (offsets/constants/permutation/frequency wiring) decorrelates essentially every
  // direction (typical |Δv-host| ≈ √2·0.01 ≈ 14× the gate; the fraction inside 0.1 rad of a random
  // direction is <1%) → fraction collapses → RED. Wrong/missing normalize spreads per-particle speeds
  // (raw |curl| is O(1..10), not 1) → the magnitude fraction collapses → RED.
  uint32_t nCmp = 0, nDirOk = 0, nMagOk = 0;
  for (uint32_t i = 0; i < (uint32_t)got1.size(); ++i) {
    const Particle& pp = got1[i];
    tixl_noise::V3 posl = {pp.Position.x * 0.9f, pp.Position.y * 0.9f, pp.Position.z * 0.9f};  // hlsl:64
    tixl_noise::V3 lookup = {posl.x * k1.Frequency, posl.y * k1.Frequency, posl.z * k1.Frequency};  // hlsl:65
    tixl_noise::V3 c = tixl_noise::curlNoise(lookup);
    const double amt = (double)k1.Amount / 100.0;  // hlsl:72
    double dx = (double)pp.Velocity.x - (double)c.x * amt;
    double dy = (double)pp.Velocity.y - (double)c.y * amt;
    double dz = (double)pp.Velocity.z - (double)c.z * amt;
    double err = std::sqrt(dx * dx + dy * dy + dz * dz);
    double sp = std::sqrt((double)pp.Velocity.x * pp.Velocity.x +
                          (double)pp.Velocity.y * pp.Velocity.y +
                          (double)pp.Velocity.z * pp.Velocity.z);
    ++nCmp;
    if (err < 1e-3) ++nDirOk;                  // 10% of |Δv| — see gate calibration note above
    if (std::fabs(sp - amt) < 1e-4) ++nMagOk;  // 1% of |Δv| — normalize makes every speed == amt
  }
  double fracDir = nCmp ? (double)nDirOk / nCmp : 0.0;
  double fracMag = nCmp ? (double)nMagOk / nCmp : 0.0;
  rep.expectTrue("noiseParity(|dv-host|<1e-3)>=90%", fracDir >= 0.90, fracDir);
  rep.expectTrue("normalized(|sp-0.01|<1e-4)>=99%", fracMag >= 0.99, fracMag);

  // ── TOOTH 1b: NODESPEC-DEFAULT parity through the REAL COOK (the模板盲區 plug) ────────────────────
  // Build the production rig (RadialPoints -> ParticleSystem(+TurbulenceForce) -> DrawPoints) and COOK it
  // with the TurbulenceForce node carrying NO Amount/Frequency override → the cook reads the production
  // NodeSpec DEFAULTS via resolveNodeParams. We compare that production-default displacement against two
  // REFERENCE cooks that explicitly set Amount=1 (TiXL .t3) and Amount=15 (pre-fix bug). A correct NodeSpec
  // default (Amount=1) makes the production cook bit-track the Amount=1 reference and clearly DIVERGE from
  // Amount=15. The injectBug leg flips the expectation to Amount=15 → RED. Because this goes THROUGH the
  // cook, a NodeSpec/death-code regression that the direct-kernel tooth could not see now gets caught.
  const int kSteps = 6;
  std::vector<SwPoint> baseline = cookTurbRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*amount=*/0.0f);  // no force = pristine ring
  std::vector<SwPoint> refA1    = cookTurbRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*amount=*/1.0f);   // TiXL .t3
  std::vector<SwPoint> refA15   = cookTurbRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*amount=*/15.0f);  // pre-fix bug
  std::vector<SwPoint> prodDef  = cookTurbRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*amount=*/-1.0f);  // NodeSpec DEFAULT

  // Structure: the production-default cook produced a non-empty bag, the SAME size as the reference
  // cooks (the particle pool; same rig, only the force Amount differs → identical pool size). This is
  // the count/structure parity check (not a magic number — the pool size is whatever the rig yields,
  // and all four cooks must agree for the per-particle displacement comparison to be well-posed).
  rep.expectTrue("cookBagSize(prod==refs>0)",
                 prodDef.size() > 0 && prodDef.size() == refA1.size() &&
                     prodDef.size() == refA15.size() && prodDef.size() == baseline.size(),
                 (double)prodDef.size());

  double dA1  = meanRingDisplacement(refA1, baseline);
  double dA15 = meanRingDisplacement(refA15, baseline);
  double dProd = meanRingDisplacement(prodDef, baseline);
  // Reference legs must actually move (so the probe is live) and Amount=15 must dwarf Amount=1.
  rep.expectTrue("refDisp_live(A15>>A1>0)", dA1 > 1e-5 && dA15 > 4.0 * dA1, dA15);

  // THE NODESPEC TOOTH: which reference does the PRODUCTION DEFAULT match?
  //   no-bug   → expect prod == Amount=1 reference (NodeSpec default IS 1.0).
  //   injectBug→ expect prod == Amount=15 reference (the pre-fix NodeSpec deviation).
  double dExpected = injectBug ? dA15 : dA1;
  double dTol = 0.05 * dExpected + 1e-6;   // tight: prod cook and the matching ref share identical params
  rep.expect("prodDefaultDisp==Amount(NodeSpec)", dProd, dExpected, dTol);
  // Ratio probe (context + extra tooth): prod/A1 must be ~1 (no-bug) or ~15× off (injectBug咬住 NodeSpec=15).
  double ratioVsA1 = dA1 > 1e-9 ? dProd / dA1 : 0.0;
  rep.expectTrue("prod/A1_ratio(~1 parity)", std::fabs(ratioVsA1 - 1.0) < 0.1, ratioVsA1);

  // ── TOOTH 2: Phase determinism through the real cook ───────────────────────────────────────────
  // Identical inputs, different wall-clock; post Stage-3 the cook reads Phase from the param (default 0)
  // instead of binding Phase=time, so the two cooks must now be bit-identical (maxPosDelta==0).
  std::vector<SwPoint> cookA = cookTurbRig(h.dev, h.queue, h.lib, /*time0=*/0.0f, /*steps=*/8);
  std::vector<SwPoint> cookB = cookTurbRig(h.dev, h.queue, h.lib, /*time0=*/10.0f, /*steps=*/8);
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
  // PARITY/determinism: two cooks with identical inputs must be identical → maxPosDelta == 0.
  // Post Stage-3 the cook reads Phase from the param (default 0), not wall-clock → GREEN (this is the
  // offline-render determinism contract that Stage-3 made hold).
  rep.expect("cookDeterministic(maxPosDelta==0)", maxPosDelta, 0.0, 1e-5);

  return rep.finish();
}

}  // namespace sw
