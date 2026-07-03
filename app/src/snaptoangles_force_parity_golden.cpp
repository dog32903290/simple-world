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
// ── VELOCITY-SEED LATCH (the seam that makes a cook-through NodeSpec tooth possible) ───────────────────
// The cook bakes InitialVelocity = 0 on emit (point_ops.cpp cookParticleSim, the named fork: PS motion
// comes from wired forces; particle_sim.metal:55-57) — so a plain cook always feeds SnapToAngles
// zero-velocity particles and the hlsl:120 early-return makes it a faithful NO-OP. The 2026-07-03 oracle
// audit (GOLDEN_ORACLE_AUDIT.md 待修#1) caught what this file used to do about that: TOOTH 1b dispatched
// the SAME hand-built params twice and asserted A==A — production NodeSpec was NEVER read (P5 恆真假錨).
// FIX: the velocity-seed latch simEmitVelocityForTest() (point_ops.cpp) lets this golden seed a non-zero
// emit speed, so particles carry velocity = emitDirection * seed and the snap acts THROUGH THE REAL COOK
// — TOOTH 1b is now a true cook-through NodeSpec discrimination, isomorphic to vectorfield_force_parity
// TOOTH 2 (the audit's correct對照組). The rig authors RadialPoints OrientationAngle=90° about X so emit
// directions (qRotateVec3(+Z, Rotation), particle_sim.metal:57) leave +Z, and the ring's per-point spin
// about the default Axis=(0,0,1) (radial_points.metal:104-110) sweeps them around the FULL XY circle —
// dense OFF-GRID coverage in the default Mode=0 (WorldXY) snap plane; the cook-side analog of fillOffGrid.
//
// ── HONEST RED-FIRST CLASSIFICATION (PARITY_GATE_PLAN.md Stage-3 §2) ──────────────────────────────────
// This is a "补验证闸" GREEN case, NOT a "修偏差" case. Scouted production state: sw NodeSpec defaults
// (node_registry_particle.cpp:121-132) == TiXL SnapToAnglesForce.t3 DefaultValue (lines 4-31): Amount=1,
// AngleCount=45, Twist=0, VariationThreshold=0.1, Variation=0.2, KeepPlanar=0.5, Mode=0 (re-verified
// 2026-07-03 against the TiXL operator catalog, op 7eefe668-d290-4673-b766-39c98b9ba12e). The cook fill
// (point_ops_forceparams.cpp fillSnapAnglesForceParams) reads every param with the SAME TiXL fallbacks and
// bakes RandomSeed=0 (the .cs exposes NO Seed slot — SnapToAnglesForce.cs:10-29). The kernel math
// (snaptoanglesforce.metal) is byte-1:1 with SnapOrientationForce.hlsl:99-156. → already FAITHFUL, merely
// NAKED: this golden ADDS the missing gate; the only production-file edits are the two test-only latches
// (velocity seed + -bug AngleCount corrupt), both 0 in production.
//
// ── TOOTH 1a (kernel-math parity, direct-kernel, TiXL closed-form) ────────────────────────────────────
//   Mode=2 (WorldXZ, camera-free), AngleCount=45 → subdivisions = 360/45 = 8, Amount=1, Variation=0,
//   VariationRatio=0 (hash gate hlsl:129 never fires → seed-independent), Twist=0, KeepPlanar=1. Seed each
//   velocity ON a snap node (planar XZ at aNormalized=k/8) plus non-zero Y. Closed form (hlsl:120-142):
//   snap-node identity (round(k)==k, Amount=1) → out planar dir == in planar dir; pure rotation → planar
//   XZ length preserved (==1); KeepPlanar=1 → Y zeroed. Expected values hlsl-derived, never an sw snapshot.
//
// ── TOOTH 1b (NodeSpec-default discrimination THROUGH THE REAL COOK — the P5 fix, carries injectBug) ──
// Geometry-liveness half (direct-kernel, kept — this half was sound): on the off-grid fillOffGrid sweep,
// hand-filled TiXL .t3 defaults at AngleCount=45 vs WRONG AngleCount=4 must produce measurably different
// kernel output. Both legs hand-filled → GREEN in both legs (a geometry probe, NOT the NodeSpec tooth).
// THE NODESPEC TOOTH (cook-through): with the velocity-seed latch ON, cook the SAME rig three ways —
//   ref45    SnapToAnglesForce node HAND-AUTHORED with the seven TiXL .t3 DefaultValues (rule-2 anchor)
//   refWrong same, but AngleCount=4 (coarse 90° grid → divergent trajectories)
//   prod     NO param overrides → the cook resolves the PRODUCTION NodeSpec defaults
//            (node_registry_particle.cpp spec → resolveNodeParams → fillSnapAnglesForceParams)
// Assert: prod is bit-identical to ref45 (identical resolved params ⇒ identical dispatches), diverges from
// refWrong, and ref45 vs refWrong diverge (probe live). A NodeSpec registration drift now resolves
// different params in the prod cook ONLY → prod leaves ref45 → RED for real.
//
// ── TOOTH 2 (cook-through faithful-no-op, honest to the production seam) ──────────────────────────────
// Cook the rig with NodeSpec DEFAULTS and the velocity-seed latch OFF (the PRODUCTION emit path: velocity
// 0). SnapToAngles correctly early-returns (hlsl:120) → its cook displacement must be bit-identical to a
// no-force baseline cook (non-empty, NaN-free, same pool size). Proves the cook REACHES the SnapToAngles
// kernel path (forceKind routing) and the early-return is faithful. GREEN in both legs.
//
// ── TOOTH 3 (offline-render determinism through the real cook) ────────────────────────────────────────
// No Phase / wall-clock term (Twist static; RandomSeed baked 0 + index): two cooks with IDENTICAL
// inputs/frame-schedule but DIFFERENT ctx.time must be bit-identical (maxPosDelta==0). GREEN in both legs.
//
// injectBug LEG (真注入, not want-flip): snapAnglesBugAngleCountForTest() (point_ops_forceparams.cpp)
// corrupts the REAL cook fill — the resolved AngleCount is REPLACED with 4 during the prod-default cook
// ONLY (a simulated NodeSpec drift; reference cooks stay clean). prod then bit-tracks refWrong and leaves
// ref45 → the same asserts that gate parity go RED; the expectation never flips. If the injection does
// not trip (prod still == ref45 under the latch) the golden returns 0, so --bite's NO-BITE list catches
// the dead tooth (P1, GOLDEN_STANDARD.md). TOOTH 1a / liveness / TOOTH 2 / TOOTH 3 GREEN in both legs.
//
// ZONE: shell tier (like vectorfield_force_parity_golden.cpp). Crosses runtime (PointGraph cook + the
// kernel). Production edits = the two test-only latches only (kernels/cooks untouched).
#include "runtime/point_graph.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "parity_golden_harness.h"
#include "runtime/force_params.h"  // SnapAnglesForceParams, FORCE_Particles/FORCE_Params
#include "runtime/graph.h"         // Graph/Node/pinId
#include "runtime/point_ops_forceparams.h"  // simEmitVelocityForTest + snapAnglesBugAngleCountForTest latches
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
// Emit speed for the TOOTH-1b seeded cooks (velocity-seed latch); 1.0 == TiXL's own EmitVelocity default.
constexpr float kVelocitySeed = 1.0f;

// Hand-built SnapAnglesForceParams carrying the TiXL .t3 defaults, AngleCount overridable. Used ONLY for
// the 1b direct-kernel GEOMETRY probe — the NodeSpec tooth reads production values through the real cook.
SnapAnglesForceParams nodeSpecDefaultParams(uint32_t N, float angleCount) {
  SnapAnglesForceParams P{};
  P.Amount = kTiXLAmount;
  P.SnapAngle = angleCount;             // = AngleCount (.cs); 45 default, 4 = wrong
  P.PhaseAngle = kTiXLTwist;            // = Twist (.cs)
  P.Variation = kTiXLVariation;
  P.VariationRatio = kTiXLVariationThreshold;  // = VariationThreshold (.cs)
  P.KeepPlanar = kTiXLKeepPlanar;
  P.SpaceAndPlane = kTiXLMode;          // = Mode (.cs): 0 = CameraSpace == WorldXY (named camera fork)
  P.RandomSeed = 0.0f;                  // .cs exposes no Seed slot → cook bakes 0 (point_ops_forceparams.cpp)
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

// ---- cook capture (RadialPoints → ParticleSystem(+SnapToAngles or none) → DrawPoints) ----
std::vector<SwPoint>* g_snapCap = nullptr;
void captureSnap(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_snapCap || !pts || c.count == 0) return;
  g_snapCap->assign(c.count, SwPoint{});
  std::memcpy(g_snapCap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

// Which SnapToAnglesForce authoring a cookSnapRig leg carries (TOOTH 1b/2/3 legs).
enum class SnapLeg {
  NoForce,        // pristine baseline: no force node wired
  ProdDefault,    // snap node with NO param overrides → the cook resolves the PRODUCTION NodeSpec defaults
  AuthoredTiXL,   // all seven params HAND-AUTHORED to the TiXL .t3 DefaultValues (the rule-2 anchor leg)
  AuthoredWrong,  // same authored set but AngleCount=kWrongAngleCount (the divergence reference)
};

// Cook the snap rig for `steps` frames from a fixed time base time0 + i*dt; return captured positions.
// `velocitySeed` drives the simEmitVelocityForTest() latch for the duration of the cook (reset after):
// 0 → the PRODUCTION emit path (InitialVelocity=0), SnapToAngles is the faithful no-op (TOOTH 2/3);
// >0 → particles carry velocity = emitDirection * seed → the snap ACTS through the cook and integrate
// accumulates the snapped directions into positions (TOOTH 1b). Two calls with the SAME inputs but
// DIFFERENT time0 expose any wall-clock dependence (TOOTH 3).
std::vector<SwPoint> cookSnapRig(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                 float time0, int steps, SnapLeg leg, float velocitySeed) {
  registerBuiltinPointOps();
  std::vector<SwPoint> captured;
  g_snapCap = &captured;
  registerDrawOp("DrawPoints", captureSnap);
  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 256.0f; gen.params["Radius"] = 2.0f;  // pinned scene (rule 5): explicit, not cook-default
  // Tilt emit directions into the snap plane: OrientationAngle=90° about X maps the emitter's +Z
  // (particle_sim.metal:57) into the XY plane; the ring's own per-point rotation about Axis=(0,0,1)
  // (radial_points.metal:104-110) then sweeps them around the FULL XY circle → dense off-grid directions
  // for the default Mode=0 (WorldXY) plane. Explicit (pinned), harmless when velocitySeed==0.
  gen.params["OrientationAngle"] = 90.0f;
  gen.params["OrientationAxis.x"] = 1.0f;
  gen.params["OrientationAxis.y"] = 0.0f;
  gen.params["OrientationAxis.z"] = 0.0f;
  Node sim; sim.id = 2; sim.type = "ParticleSystem";
  Node drw; drw.id = 3; drw.type = "DrawPoints";
  g.nodes.push_back(gen); g.nodes.push_back(sim);
  Node snap; snap.id = 4; snap.type = "SnapToAnglesForce";
  if (leg == SnapLeg::AuthoredTiXL || leg == SnapLeg::AuthoredWrong) {
    // HAND-AUTHOR the seven TiXL .t3 DefaultValues on the node (rule-2 anchor: values transcribed from
    // TiXL source, never read back from sw). AuthoredWrong deviates ONLY in AngleCount.
    snap.params["Amount"] = kTiXLAmount;
    snap.params["AngleCount"] = (leg == SnapLeg::AuthoredWrong) ? kWrongAngleCount : kTiXLAngleCount;
    snap.params["Twist"] = kTiXLTwist;
    snap.params["VariationThreshold"] = kTiXLVariationThreshold;
    snap.params["Variation"] = kTiXLVariation;
    snap.params["KeepPlanar"] = kTiXLKeepPlanar;
    snap.params["Mode"] = kTiXLMode;
  }  // ProdDefault: NO param overrides → resolveNodeParams falls to the production NodeSpec defaults
  if (leg != SnapLeg::NoForce) g.nodes.push_back(snap);
  g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points → emit
  if (leg != SnapLeg::NoForce)
    g.connections.push_back({102, pinId(4, 0), pinId(2, 1)});  // SnapToAnglesForce.force → forces
  g.connections.push_back({103, pinId(2, 2), pinId(3, 0)});    // result → DrawPoints

  simEmitVelocityForTest() = velocitySeed;  // velocity-seed latch ON for this cook only
  const int targetId = pg.defaultDrawTarget(g);
  for (int i = 0; i < steps; ++i) {
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)i;          // SAME frame schedule both calls (cycle buffer identical)
    ctx.time = time0 + 0.05f * (float)i;   // the ONLY difference between the two TOOTH-3 cooks
    ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, targetId);
  }
  simEmitVelocityForTest() = 0.0f;  // never leak the latch into other selftests
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

// Max per-particle velocity delta between two kernel runs (same N/order). Used by the 1b geometry probe.
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

  // ── TOOTH 1b GEOMETRY PROBE (direct-kernel, both legs hand-filled — NOT the NodeSpec tooth) ───────
  // Velocity OFF the snap grid: TiXL .t3 defaults at AngleCount=45 vs WRONG AngleCount=4 snap the
  // direction to DIFFERENT discrete angles → measurable divergence (the quantizer grid is live here).
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
  rep.expectTrue("kernel1bSawAllParticles", velDefault45.size() == N && velWrong4.size() == N,
                 (double)velDefault45.size());
  double dDefaultVsWrong = maxVelDelta(velDefault45, velWrong4);
  rep.expectTrue("ref1b_live(45≠4 snaps differ)", dDefaultVsWrong > 1e-4, dDefaultVsWrong);

  // ── TOOTH 1b THE NODESPEC TOOTH: prod-default cook vs hand-authored TiXL .t3 cook (velocity seeded) ─
  // The production leg goes THROUGH THE REAL COOK with NO param overrides (resolveNodeParams → production
  // NodeSpec defaults → fillSnapAnglesForceParams → kernel on seeded velocities); the anchor leg
  // hand-authors the TiXL .t3 DefaultValues on the same rig. Identical resolved params ⇒ bit-identical
  // positions; a NodeSpec drift breaks ONLY the prod leg → RED. injectBug corrupts the prod cook's fill.
  const int kSteps = 8;
  std::vector<SwPoint> ref45 =
      cookSnapRig(h.dev, h.queue, h.lib, 0.0f, kSteps, SnapLeg::AuthoredTiXL, kVelocitySeed);
  std::vector<SwPoint> refWrong =
      cookSnapRig(h.dev, h.queue, h.lib, 0.0f, kSteps, SnapLeg::AuthoredWrong, kVelocitySeed);
  if (injectBug) snapAnglesBugAngleCountForTest() = kWrongAngleCount;  // corrupt the REAL cook fill
  std::vector<SwPoint> prod1b =
      cookSnapRig(h.dev, h.queue, h.lib, 0.0f, kSteps, SnapLeg::ProdDefault, kVelocitySeed);
  snapAnglesBugAngleCountForTest() = 0.0f;  // never leak the latch into other selftests

  rep.expectTrue("cook1bBagSize(prod==refs>0)",
                 prod1b.size() > 0 && prod1b.size() == ref45.size() && prod1b.size() == refWrong.size(),
                 (double)prod1b.size());
  // Liveness: the two authored AngleCounts must diverge THROUGH the cook (proves the velocity seed took —
  // un-seeded particles would make both cooks identical no-ops — and that AngleCount reaches the kernel).
  double dRefLive = maxPosDelta(ref45, refWrong);
  rep.expectTrue("cook1b_live(45vs4 cooks differ)", dRefLive > 1e-4, dRefLive);
  // THE TOOTH: the production NodeSpec defaults must equal the TiXL .t3 transcription — bit-identical.
  double dProdVsT3 = maxPosDelta(prod1b, ref45);
  rep.expect("prodCook==TiXL.t3Cook(NodeSpec)", dProdVsT3, 0.0, 1e-5);
  // And the prod cook must NOT be sitting on the wrong-AngleCount trajectory (二重咬合: under injectBug
  // the corrupted prod bit-tracks refWrong, so this goes RED together with the tooth above).
  double dProdVsWrong = maxPosDelta(prod1b, refWrong);
  rep.expectTrue("prodCook!=wrongAngleCook", dProdVsWrong > 1e-4, dProdVsWrong);

  // ── TOOTH 2: cook-through faithful zero-velocity no-op + structure (production emit path, seed 0) ─
  // The PRODUCTION cook's particles are un-seeded (velocity 0), so the prod-default SnapToAngles cook is
  // a faithful no-op → bit-identical to the no-force baseline (hlsl:120). GREEN in both legs.
  std::vector<SwPoint> baseline =
      cookSnapRig(h.dev, h.queue, h.lib, 0.0f, kSteps, SnapLeg::NoForce, /*velocitySeed=*/0.0f);
  std::vector<SwPoint> prodCook =
      cookSnapRig(h.dev, h.queue, h.lib, 0.0f, kSteps, SnapLeg::ProdDefault, /*velocitySeed=*/0.0f);

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
  std::vector<SwPoint> cookA =
      cookSnapRig(h.dev, h.queue, h.lib, /*time0=*/0.0f, kSteps, SnapLeg::ProdDefault, /*velocitySeed=*/0.0f);
  std::vector<SwPoint> cookB =
      cookSnapRig(h.dev, h.queue, h.lib, /*time0=*/10.0f, kSteps, SnapLeg::ProdDefault, /*velocitySeed=*/0.0f);
  double dDet = maxPosDelta(cookA, cookB);
  rep.expect("cookDeterministic(maxPosDelta==0)", dDet, 0.0, 1e-5);

  if (injectBug && dProdVsT3 <= 1e-5) {
    // The -bug latch failed to corrupt the prod cook (prod still bit-tracks the TiXL anchor) — the
    // injection did not trip. A dead tooth MUST exit 0 so --bite lands it on the NO-BITE list (P1).
    printf("[selftest-snaptoanglesforce-parity] -bug injection did not trip -> NO-BITE\n");
    return 0;
  }
  return rep.finish();
}

}  // namespace sw
