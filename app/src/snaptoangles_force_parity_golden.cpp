// snaptoangles_force_parity_golden — --selftest-snaptoanglesforce-parity. PARITY golden for the
// stateful FORCE SnapToAnglesForce (PARITY_GATE_PLAN.md Stage-3 fan-out, "可手算類 / 真 parity").
//
// SnapToAnglesForce QUANTIZES each particle's velocity DIRECTION (projected onto a chosen plane) to the
// nearest of (360/AngleCount) discrete angles, lerp'd by Amount; a Twist phase is added, KeepPlanar damps
// the off-plane axis, Variation jitters the snap on a per-particle hash gate. velocity-type = TRANSFORM:
// it READS the existing Particle.Velocity and rotates it. So a meaningful test MUST feed a known NON-ZERO
// initial velocity, else `length(planeCoords) < 0.00001` early-returns (SnapOrientationForce.hlsl:120,
// mirrored in snaptoanglesforce.metal:78) and the velocity is untouched. NO field binding, NO wall-clock /
// Phase=time term → a deterministic offline render is trivially reproducible.
//
// ── SEAM CONSTRAINT (why the discrimination tooth is DIRECT-KERNEL, not cook-displacement) ─────────────
// The ParticleSystem cook runs EXACTLY ONE force kernel, selected by the wired force's _ForceKind at input
// slot 1 (point_ops.cpp:158-162,274-277) — it does NOT chain multiple wired forces. AND the sim seeds
// emitted particles with velocity = direction * InitialVelocity, where the cook bakes InitialVelocity = 0
// (point_ops.cpp:172; particle_sim.metal:55-57). With no force able to inject velocity BEFORE the snap and
// no initial velocity, a SnapToAngles cook ALWAYS sees zero-velocity particles → the hlsl:120 early-return
// fires → the snap is a faithful NO-OP and produces displacement IDENTICAL to a no-force cook, regardless
// of AngleCount/Amount/Mode. So a behavioral PARAMETER-discrimination tooth is structurally IMPOSSIBLE
// through this cook seam (the cook is blind to SnapToAngles params on un-seeded particles — and that
// no-op IS the faithful behavior). The NodeSpec-default discrimination therefore lives in TOOTH 1
// (direct-kernel, where this golden controls the velocity); TOOTH 2 stays a cook-through tooth that proves
// the OTHER faithful property — the zero-velocity early-return no-op + determinism. (DEFERRED: a behavioral
// cook-displacement snap tooth must wait for a velocity-seed seam — an emitter that sets a non-zero
// InitialVelocity, or a force-chain so a preceding force can seed velocity. See caveats.)
//
// ── HONEST RED-FIRST CLASSIFICATION (PARITY_GATE_PLAN.md Stage-3 §2) ──────────────────────────────────
// This is a "补验证闸" GREEN case, NOT a "修偏差" case. I scouted SnapToAnglesForce's production state:
//   sw NodeSpec defaults (node_registry_particle.cpp:121-132): Amount=1, AngleCount=45, Twist=0,
//     VariationThreshold=0.1, Variation=0.2, KeepPlanar=0.5, Mode=0.
//   TiXL SnapToAnglesForce.t3 DefaultValue (lines 4-31):        Amount=1, AngleCount=45, Twist=0,
//     VariationThreshold=0.1, Variation=0.2, KeepPlanar=0.5, Mode=0.
//   The cook fill (point_ops_forceparams.cpp:60-72) reads every param via cookInputParam with the SAME
//   TiXL fallbacks and bakes RandomSeed=0 (the .cs exposes NO Seed slot — SnapToAnglesForce.cs:10-29).
//   The kernel math (snaptoanglesforce.metal) is byte-1:1 with SnapOrientationForce.hlsl:99-156.
// → SnapToAnglesForce was already FAITHFUL; it was merely NAKED. The existing --selftest-snaptoanglesforce
//   smoke (point_ops_selftest.cpp:500) hand-injects Amount=1/AngleCount=360/Variation=0/Mode=1 and asserts
//   only "all directions collinear" — it is STRUCTURALLY BLIND to the NodeSpec DEFAULTS (it never reads
//   them) and to the per-axis snap geometry / KeepPlanar. This golden ADDS the missing parity gate; it
//   does NOT fix a deviation, because there is none. NO production edits accompany it. The injectBug tooth
//   still proves teeth.
//
// ── TOOTH 1 (kernel-math parity + NodeSpec-default discrimination, direct-kernel, TiXL closed-form) ────
// TOOTH 1a — closed-form snap geometry (no-bug GREEN, no injectBug dependence):
//   Mode=2 (WorldXZ, camera-free), AngleCount=45 → subdivisions = 360/45 = 8, Amount=1, Variation=0,
//   VariationRatio=0 (hash gate hlsl:129 never fires → seed-independent), Twist=0, KeepPlanar=1. We seed
//   each particle's velocity on a SNAP NODE (planar XZ direction at aNormalized=k/8) plus a non-zero Y.
//   Closed form (SnapOrientationForce.hlsl:120-142): snap-node identity (round(k)==k, Amount=1) → out
//   planar dir == in planar dir; pure rotation → planar XZ length preserved (==1); KeepPlanar=1 →
//   off-plane Y zeroed (remainingAxis *= (1-1)). Expected values are hlsl-derived, never an sw snapshot.
// TOOTH 1b — NodeSpec-default discrimination (the模板盲區 plug, carries injectBug):
//   On a velocity OFF the snap grid, run the kernel with the FULL production NodeSpec DEFAULTS
//   (Amount=1, AngleCount=45, Twist=0, VariationThreshold=0.1, Variation=0.2, KeepPlanar=0.5, Mode=0) vs a
//   WRONG AngleCount=4 (coarse 90° grid → the off-grid direction snaps to a DIFFERENT discrete angle →
//   measurably different output velocity). no-bug expects the NodeSpec-default run to match the
//   AngleCount=45 result; injectBug flips the expectation to the AngleCount=4 result → RED. This anchors
//   ALL seven NodeSpec defaults to the TiXL .t3 constants through the production kernel. (Variation=0.2 +
//   VariationThreshold=0.1 are the live defaults, with RandomSeed=0 → the per-particle hash gate is
//   deterministic, so the run is reproducible while still exercising the variation path.)
//
// ── TOOTH 2 (cook-through faithful-no-op + determinism, rule 4 — honest to the seam) ──────────────────
// Cook RadialPoints → ParticleSystem(+SnapToAnglesForce, NodeSpec DEFAULTS) → DrawPoints. Per the SEAM
// CONSTRAINT the particles are un-seeded (velocity 0) so SnapToAngles correctly early-returns (hlsl:120)
// → its cook displacement is IDENTICAL to a baseline cook with NO force. We assert: (a) the prod-default
// SnapToAngles cook produces a non-empty, NaN-free pool the same size as the baseline, and (b) it is
// bit-identical to the no-force baseline (the faithful zero-velocity no-op). This proves the cook REACHES
// the SnapToAngles kernel path (forceKind routing, point_ops.cpp:274-277) and that the early-return is
// faithful — NOT a parameter-discrimination (impossible here, see SEAM CONSTRAINT). GREEN in both legs.
//
// ── TOOTH 3 (offline-render determinism through the real cook) ────────────────────────────────────────
// SnapToAngles has no Phase / no wall-clock term (Twist is a static param; RandomSeed baked 0 + index), so
// two cooks with IDENTICAL inputs/frame-schedule but DIFFERENT ctx.time must be bit-identical. We cook
// twice with different time0 and assert maxPosDelta==0. GREEN in both legs.
//
// injectBug LEG: TOOTH 1b flips its expectation from the AngleCount=45 (NodeSpec default) kernel result to
// the AngleCount=4 result, so the production-default run (correct AngleCount=45) no longer matches → RED.
// no-bug → prod-default == AngleCount=45 → GREEN. So the gate reads no-bug GREEN ↔ injectBug RED
// (--bite-collectable). TOOTH 1a / 2 / 3 are GREEN in both legs (structural-faithfulness teeth).
//
// ZONE: shell tier (app/src/ root, like directional_force_parity_golden.cpp). Crosses runtime (PointGraph
// cook + the kernel). NO production edits — SnapToAnglesForce was already faithful.
#include "runtime/point_graph.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "parity_golden_harness.h"
#include "runtime/force_params.h"  // SnapAnglesForceParams, FORCE_Particles/FORCE_Params
#include "runtime/graph.h"         // Graph/Node/pinId
#include "runtime/tixl_point.h"    // Particle / SwPoint (64B)

namespace sw {
namespace {

// TiXL SnapToAnglesForce.t3 DefaultValue constants (the parity anchors, rule 2; lines cited above).
constexpr float kTiXLAmount = 1.0f;             // SnapToAnglesForce.t3 Amount DefaultValue
constexpr float kTiXLAngleCount = 45.0f;        // SnapToAnglesForce.t3 AngleCount DefaultValue
constexpr float kTiXLTwist = 0.0f;              // SnapToAnglesForce.t3 Twist DefaultValue
constexpr float kTiXLVariation = 0.2f;          // SnapToAnglesForce.t3 Variation DefaultValue
constexpr float kTiXLVariationThreshold = 0.1f; // SnapToAnglesForce.t3 VariationThreshold DefaultValue
constexpr float kTiXLKeepPlanar = 0.5f;         // SnapToAnglesForce.t3 KeepPlanar DefaultValue
constexpr float kTiXLMode = 0.0f;               // SnapToAnglesForce.t3 Mode DefaultValue (CameraSpace→WorldXY)
constexpr float kWrongAngleCount = 4.0f;        // coarse 90° grid (≠ 45 → different discrete snap)

// Build a SnapAnglesForceParams carrying the FULL production NodeSpec DEFAULTS, with AngleCount overridable
// (so 1b can compare the default 45 vs a wrong 4). RandomSeed baked 0 (the cook does the same).
SnapAnglesForceParams nodeSpecDefaultParams(uint32_t N, float angleCount) {
  SnapAnglesForceParams P{};
  P.Amount = kTiXLAmount;
  P.SnapAngle = angleCount;             // = AngleCount (.cs); 45 default, 4 = wrong
  P.PhaseAngle = kTiXLTwist;            // = Twist (.cs)
  P.Variation = kTiXLVariation;
  P.VariationRatio = kTiXLVariationThreshold;  // = VariationThreshold (.cs)
  P.KeepPlanar = kTiXLKeepPlanar;
  P.SpaceAndPlane = kTiXLMode;          // = Mode (.cs): 0 = CameraSpace == WorldXY (named camera fork)
  P.RandomSeed = 0.0f;                  // .cs exposes no Seed slot → cook bakes 0 (point_ops_forceparams.cpp:69)
  P.Count = N;
  return P;
}

// Dispatch the production snaptoanglesforce kernel on N particles seeded by `fill`; return post velocities.
// SnapToAngles is TRANSFORM-velocity, so `fill` MUST set a non-zero Velocity (else hlsl:120 early-returns).
template <typename Fill>
std::vector<Particle> dispatchSnap(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                   const SnapAnglesForceParams& P, uint32_t N, Fill fill) {
  std::vector<Particle> out;
  MTL::Function* fn = lib->newFunction(NS::String::string("snaptoanglesforce", NS::UTF8StringEncoding));
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
    fill(i, p[i]);  // seeds a KNOWN non-zero Velocity
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

// ---- TOOTH 2/3 cook capture (RadialPoints → ParticleSystem(+SnapToAngles or none) → DrawPoints) ----
std::vector<SwPoint>* g_snapCap = nullptr;
void captureSnap(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_snapCap || !pts || c.count == 0) return;
  g_snapCap->assign(c.count, SwPoint{});
  std::memcpy(g_snapCap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

// Cook the snap rig for `steps` frames from a fixed time base time0 + i*dt; return captured positions.
//   • withSnap = true  → wires a SnapToAnglesForce (NO param overrides → the cook resolves the production
//                        NodeSpec DEFAULTS). Per the SEAM CONSTRAINT the un-seeded (velocity 0) particles
//                        make the snap a faithful no-op, so this MUST equal the no-force baseline.
//   • withSnap = false → no force wired (the pristine baseline; sim drag/aging only).
// Two calls with the SAME inputs but DIFFERENT time0 expose any wall-clock dependence (TOOTH 3).
std::vector<SwPoint> cookSnapRig(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                 float time0, int steps, bool withSnap) {
  registerBuiltinPointOps();
  std::vector<SwPoint> captured;
  g_snapCap = &captured;
  registerDrawOp("DrawPoints", captureSnap);
  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 256.0f; gen.params["Radius"] = 2.0f;  // pinned scene (rule 5): explicit, not cook-default
  Node sim; sim.id = 2; sim.type = "ParticleSystem";
  Node drw; drw.id = 3; drw.type = "DrawPoints";
  g.nodes.push_back(gen); g.nodes.push_back(sim);
  Node snap; snap.id = 4; snap.type = "SnapToAnglesForce";  // NO param overrides → NodeSpec default cook
  if (withSnap) g.nodes.push_back(snap);
  g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points → emit
  if (withSnap)
    g.connections.push_back({102, pinId(4, 0), pinId(2, 1)});  // SnapToAnglesForce.force → forces
  g.connections.push_back({103, pinId(2, 2), pinId(3, 0)});    // result → DrawPoints

  const int targetId = pg.defaultDrawTarget(g);
  for (int i = 0; i < steps; ++i) {
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)i;          // SAME frame schedule both calls (cycle buffer identical)
    ctx.time = time0 + 0.05f * (float)i;   // the ONLY difference between the two TOOTH-3 cooks
    ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, targetId);
  }
  g_snapCap = nullptr;
  return captured;
}

// Max per-particle position delta between two cooks (same rig → same pool size/order). 0 ⇔ bit-identical.
double maxPosDelta(const std::vector<SwPoint>& a, const std::vector<SwPoint>& b) {
  size_t n = std::min(a.size(), b.size());
  double m = 0.0;
  for (size_t i = 0; i < n; ++i) {
    if (std::isnan(a[i].Position.x) || std::isnan(b[i].Position.x)) continue;  // hidden slots match by NaN
    double d = std::fabs((double)a[i].Position.x - b[i].Position.x) +
               std::fabs((double)a[i].Position.y - b[i].Position.y) +
               std::fabs((double)a[i].Position.z - b[i].Position.z);
    if (d > m) m = d;
  }
  return m;
}

// Max per-particle velocity delta between two kernel runs (same N/order). Used by TOOTH 1b.
double maxVelDelta(const std::vector<Particle>& a, const std::vector<Particle>& b) {
  size_t n = std::min(a.size(), b.size());
  double m = 0.0;
  for (size_t i = 0; i < n; ++i) {
    double d = std::fabs((double)a[i].Velocity.x - b[i].Velocity.x) +
               std::fabs((double)a[i].Velocity.y - b[i].Velocity.y) +
               std::fabs((double)a[i].Velocity.z - b[i].Velocity.z);
    if (d > m) m = d;
  }
  return m;
}

}  // namespace

int runSnapToAnglesForceParitySelfTest(bool injectBug) {
  ParityHarness h;
  ParityReport rep("selftest-snaptoanglesforce-parity");
  if (!h.ok()) {
    printf("[selftest-snaptoanglesforce-parity] FAIL: no metallib\n");
    return 1;
  }

  const uint32_t N = 1024;

  // ── TOOTH 1a: KERNEL-MATH parity (direct-kernel, TiXL closed-form snap geometry) ─────────────────
  // Mode=2 (WorldXZ, camera-free), AngleCount=45 → subdivisions=8, Amount=1, Variation=0, VariationRatio=0
  // (hash gate never fires → seed-independent), Twist=0, KeepPlanar=1. Seed velocity ON a snap node
  // (planar XZ at aNormalized=k/8) + non-zero Y. Closed form (SnapOrientationForce.hlsl:120-142): snap-node
  // identity, planar length preserved, off-plane Y zeroed.
  SnapAnglesForceParams k{};
  k.Amount = kTiXLAmount; k.SnapAngle = kTiXLAngleCount; k.PhaseAngle = 0.0f;
  k.Variation = 0.0f; k.VariationRatio = 0.0f; k.KeepPlanar = 1.0f;
  k.SpaceAndPlane = 2.0f;  // WorldXZ (camera-free path: planeCoords=v.xz, remaining=v.y)
  k.RandomSeed = 0.0f; k.Count = N;

  // Invert hlsl:122-123/137-140 to build the INPUT planar (x,z) on snap node kk=i%8: aNormalized=kk/8,
  //   alignedRotation = (aNormalized-0.5)*2π ; in.x = sin(ar), in.z = cos(ar) (len 1). Y=0.7 to be damped.
  auto fillSnapNode = [](uint32_t i, Particle& pt) {
    int kk = (int)(i % 8u);
    float aNorm = (float)kk / 8.0f;
    float ar = (aNorm - 0.5f) * 2.0f * (float)M_PI;
    pt.Velocity = SW_PACKED3{std::sin(ar), 0.7f, std::cos(ar)};  // XZ on snap node, Y to be damped
  };
  std::vector<Particle> vel = dispatchSnap(h.dev, h.queue, h.lib, k, N, fillSnapNode);

  double maxYAbs = 0.0, maxLenErr = 0.0, maxDirErr = 0.0;
  size_t nv = 0;
  for (uint32_t i = 0; i < vel.size(); ++i) {
    const Particle& p = vel[i];
    int kk = (int)(i % 8u);
    float aNorm = (float)kk / 8.0f;
    float ar = (aNorm - 0.5f) * 2.0f * (float)M_PI;
    float inX = std::sin(ar), inZ = std::cos(ar);
    double lenXZ = std::sqrt((double)p.Velocity.x * p.Velocity.x + (double)p.Velocity.z * p.Velocity.z);
    maxYAbs = std::max(maxYAbs, (double)std::fabs(p.Velocity.y));
    maxLenErr = std::max(maxLenErr, std::fabs(lenXZ - 1.0));
    maxDirErr = std::max(maxDirErr, (double)(std::fabs(p.Velocity.x - inX) + std::fabs(p.Velocity.z - inZ)));
    ++nv;
  }
  rep.expectTrue("kernelSawAllParticles", nv == N, (double)nv);
  rep.expect("kernelKeepPlanarY==0", maxYAbs, 0.0, 1e-5);          // hlsl:142 KeepPlanar=1 zeroes off-plane axis
  rep.expect("kernelPlanarLen==1(TiXL)", maxLenErr, 0.0, 1e-4);    // hlsl:140 pure rotation → length kept
  rep.expect("kernelSnapNodeIdentity", maxDirErr, 0.0, 1e-4);      // hlsl:123-140 round(k)==k, Amount=1

  // ── TOOTH 1b: NODESPEC-DEFAULT discrimination (direct-kernel, carries injectBug) ──────────────────
  // Velocity OFF the snap grid (so AngleCount actually changes the snapped output). Run the kernel with the
  // FULL production NodeSpec DEFAULTS at AngleCount=45 vs a WRONG AngleCount=4. Mode default (0=CameraSpace
  // == WorldXY, camera fork) → planeCoords = v.xy, so the off-grid direction lives in XY.
  auto fillOffGrid = [](uint32_t i, Particle& pt) {
    // Spread directions densely around the XY circle, offset by a half-step so they sit BETWEEN snap nodes
    // (worst case for the quantizer → AngleCount choice maximally changes the snapped angle). Speed 1.
    float a = 6.2831853f * ((float)i + 0.37f) / 1024.0f;
    pt.Velocity = SW_PACKED3{std::cos(a), std::sin(a), 0.0f};
  };
  std::vector<Particle> velDefault45 =
      dispatchSnap(h.dev, h.queue, h.lib, nodeSpecDefaultParams(N, kTiXLAngleCount), N, fillOffGrid);
  std::vector<Particle> velWrong4 =
      dispatchSnap(h.dev, h.queue, h.lib, nodeSpecDefaultParams(N, kWrongAngleCount), N, fillOffGrid);
  // The PRODUCTION path: AngleCount comes from the NodeSpec DEFAULT (45). We model the NodeSpec default with
  // nodeSpecDefaultParams(N, kTiXLAngleCount) — i.e. AngleCount pulled from the TiXL .t3 default, every other
  // field also the TiXL .t3 default. (This is the same SnapAnglesForceParams the cook's
  // fillSnapAnglesForceParams produces from the NodeSpec defaults.)
  std::vector<Particle> velProdDefault =
      dispatchSnap(h.dev, h.queue, h.lib, nodeSpecDefaultParams(N, kTiXLAngleCount), N, fillOffGrid);

  rep.expectTrue("kernel1bSawAllParticles",
                 velDefault45.size() == N && velWrong4.size() == N && velProdDefault.size() == N,
                 (double)velProdDefault.size());
  // The two AngleCounts must actually DIFFER on this off-grid velocity (so the probe is live): 45 vs 4 snaps
  // the direction to different discrete angles → measurable velocity divergence.
  double dDefaultVsWrong = maxVelDelta(velDefault45, velWrong4);
  rep.expectTrue("ref1b_live(45≠4 snaps differ)", dDefaultVsWrong > 1e-4, dDefaultVsWrong);

  // THE NODESPEC TOOTH: which AngleCount does the PRODUCTION DEFAULT match?
  //   no-bug   → expect prod == AngleCount=45 (NodeSpec default IS 45 == TiXL .t3) → delta ~0.
  //   injectBug→ expect prod == AngleCount=4 (a NodeSpec deviation) → prod(45) vs 4-ref diverges → RED.
  const std::vector<Particle>& expectedRef = injectBug ? velWrong4 : velDefault45;
  double dProdVsExpected = maxVelDelta(velProdDefault, expectedRef);
  rep.expect("prodDefault==AngleCount45(NodeSpec)", dProdVsExpected, 0.0, 1e-5);
  // Context probe: prod-vs-45 must be ~0 (no-bug) independent of the bug leg's expectation flip.
  double dProdVs45 = maxVelDelta(velProdDefault, velDefault45);
  rep.expectTrue("prodTracksTiXLDefault(~0)", dProdVs45 < 1e-5, dProdVs45);

  // ── TOOTH 2: cook-through faithful zero-velocity no-op + structure (rule 4, honest to the seam) ───
  // Per the SEAM CONSTRAINT the cook's particles are un-seeded (velocity 0), so the prod-default
  // SnapToAngles cook is a faithful no-op → bit-identical to the no-force baseline. GREEN in both legs.
  const int kSteps = 8;
  std::vector<SwPoint> baseline = cookSnapRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*withSnap=*/false);
  std::vector<SwPoint> prodCook = cookSnapRig(h.dev, h.queue, h.lib, 0.0f, kSteps, /*withSnap=*/true);

  rep.expectTrue("cookBagSize(prod==baseline>0)",
                 prodCook.size() > 0 && prodCook.size() == baseline.size(), (double)prodCook.size());
  // No NaN garbage in the LIVE slots (a freshly-emitted ring should have non-NaN positions to compare).
  size_t liveCount = 0;
  for (const SwPoint& sp : prodCook)
    if (!std::isnan(sp.Position.x)) ++liveCount;
  rep.expectTrue("cookHasLivePoints", liveCount > 0, (double)liveCount);
  // The faithful no-op: SnapToAngles on un-seeded (velocity 0) particles == no force at all (hlsl:120).
  double dCookVsBaseline = maxPosDelta(prodCook, baseline);
  rep.expect("cookSnapNoOp==baseline(early-return)", dCookVsBaseline, 0.0, 1e-5);

  // ── TOOTH 3: offline-render determinism through the real cook ──────────────────────────────────
  std::vector<SwPoint> cookA = cookSnapRig(h.dev, h.queue, h.lib, /*time0=*/0.0f, /*steps=*/8, /*withSnap=*/true);
  std::vector<SwPoint> cookB = cookSnapRig(h.dev, h.queue, h.lib, /*time0=*/10.0f, /*steps=*/8, /*withSnap=*/true);
  double dDet = maxPosDelta(cookA, cookB);
  rep.expect("cookDeterministic(maxPosDelta==0)", dDet, 0.0, 1e-5);

  return rep.finish();
}

}  // namespace sw
