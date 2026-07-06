// particle_sim_integrate_parity_golden — --selftest-particlesim-integrate-parity. PARITY golden for the
// stateful ParticleSystem INTEGRATOR core (骨5, the genuinely-unknown bone: the sim was only smoke-tested).
//
// WHY THIS EXISTS (the gap this plugs) ────────────────────────────────────────────────────────────────
// ParticleSystem's per-step integrator (ParticleSystem.hlsl:102-113) is the load-bearing math of the whole
// particle world, yet NOTHING asserted its closed-form update. The existing coverage is all AGGREGATE /
// SMOKE, structurally blind to a per-step formula drift:
//   • runParticleFlowSelfTest (particle_system.cpp): "maxDevFromRing > 0.1" — passes for ANY motion.
//   • runParticleDecaySelfTest: recycle-band survival over 5 min — a POOL-lifecycle metric, not the step.
//   • directional_force_parity_golden: asserts the FORCE kernel velocity + a mean-ring-DISPLACEMENT scalar
//     — it never checks the integrator's own `velocity *= pow(1-Drag,Speed)` drag or `pos += velocity *
//     Speed * 0.01` position step. A drag/step-scale drift would slide right through its aggregate mean.
// This golden pins the integrator's EXACT per-particle position delta to the TiXL closed-form.
//
// THE CLOSED FORM (three TiXL .cs/.hlsl formulas, hand-computed — anchor discipline rule 2) ─────────────
// Rig: RadialPoints -> ParticleSystem(+DirectionalForce) -> DrawPoints, cooked THROUGH the production ops
// (blood-proof: no direct kernel poke). DirectionalForce is the cleanest deterministic force (no field, no
// noise, no wall-clock). We drive it with Direction=(1,0,0), Amount=A, RandomAmount=0 so the force term is
// a pure constant D = Direction*Amount (DirectionalForce.hlsl:23-24, hash term == 1 when RandomAmount=0).
//
// A particle emitted on the SEED step has InitialVelocity forked to 0 in the production cook
// (point_ops.cpp:171 — the named fork: PS drives motion by wired forces, not the emitter's InitialVelocity),
// so after the seed step its Velocity == 0 and its Position == its emit position P0 (integrate is a no-op on
// zero velocity). On the NEXT step, for a slot that is NOT re-emitted (the cycle write head has advanced one
// emit block past it — CollectCycleIndex = frame*emitCount, EmitMode=0, so addIndex = (gi+emitCount)%pool >=
// emitCount for gi in [0,emitCount) → skips emit), the shader runs, in order:
//   1. DirectionalForce pass:  Velocity += D                          (DirectionalForce.hlsl:24)   → V = D
//   2. integrator drag:        Velocity *= pow(1 - Drag, Speed)       (ParticleSystem.hlsl:106)     → V = D·k
//   3. integrator position:    Position += Velocity * Speed * 0.01    (ParticleSystem.hlsl:111)
// So the per-particle position DELTA between seed and step-1 is the CLOSED-FORM CONSTANT (same for every
// clean slot, independent of emit geometry — the emit position cancels in the subtraction):
//   Δpos = D · pow(1 - Drag, Speed) · Speed · 0.01
// With Speed=1.0 (== ParticleSystem.t3 Speed DefaultValue) and Drag=0.02 (a CHOSEN test pin — the real
// .t3 Drag DefaultValue is 0.005; the registry was drifted at 0.02 until the 2026-07-06 audit aligned it),
// Direction=(1,0,0), Amount=0.5:  k = pow(0.98, 1) = 0.98 ; Δpos = (0.5·0.98·1·0.01, 0, 0) = (0.0049, 0, 0).
// We read the delta straight off the captured ResultPoints (Velocity is internal to the Particle buffer and
// never copied to the result; the position delta EXACTLY encodes both the drag and the step-scale, so it is
// the faithful observable for the integrator core).
//
// RED-FIRST TEETH (three states) ───────────────────────────────────────────────────────────────────────
//   • no-bug              → GREEN: measured Δpos == TiXL closed form (drag=pow, step-scale=0.01 both right).
//   • injectBug (REAL cook injection, GOLDEN_STANDARD 特徵3 — was a P3 want-flip, fixed) → RED: the -bug
//     leg flips particleSimDragBugForTest() (point_ops_forceparams.h, the family's *BugForceForTest latch
//     convention) around the MAIN cook ONLY. The latch corrupts the REAL integrator param path
//     (cookParticleSim: Drag → 0.5, an integrator-param drift), so the measured production Δpos becomes
//     closedForm(Drag=0.5) = 0.5·0.5·0.01 = 0.0025 while the assert still pins the TiXL closed form
//     0.0049 (tol 5e-5) → RED (and the dragProbe ratio collapses to ~1 → RED too). If the latch ever
//     dies (injection does not trip), the cook stays faithful, the asserts PASS and the leg exits 0 →
//     run_all_selftests --bite's NO-BITE list catches the dead tooth. NOTE: the shader-side stale form a
//     naive port would write (linear drag / missing 0.01) is not host-injectable without a shader test
//     seam; the SPEED PROBE (Speed=2) is the tooth that pins the pow-form against exactly that drift.
//   • dragProbe (both legs, context tooth) → an independent Drag=0.5 cook must scale Δpos by pow(0.5,Speed)
//     relative to the Drag=0.02 cook — proves the drag term is drag-DEPENDENT (not ignored). At Speed=1 it
//     canNOT distinguish pow from linear (pow(x,1)==x — the identity the 2026-07-06 audit caught); the
//     Speed=2 probe below owns the FORM.
//
// NAMED FORK NOTE (pool-recycle, 柏為 拍板 kept sw-fork): MaxParticleCount recycle (particle_params.h
// kPoolLifeFrames) is NOT under test here — we deliberately measure only the [0,emitCount) clean cohort on
// step 1, which the recycle write-head provably has NOT yet revisited (see the addIndex trace above). The
// re-emitted / not-yet-emitted pool tail (Δ≠constant, Scale=NaN) is EXCLUDED, not asserted. So a recycle
// policy change cannot spuriously flip this golden — it guards the integrator formula ONLY.
//
// ZONE: shell tier (app/src/ root, like directional_force_parity_golden.cpp). Crosses runtime (PointGraph
// cook + the registered ops). NO production edits — the integrator was already faithful; this ADDS the gate.
#include "runtime/point_graph.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "parity_golden_harness.h"
#include "runtime/graph.h"                  // Graph / Node / pinId
#include "runtime/point_ops_forceparams.h"  // particleSimDragBugForTest (-bug real-cook latch)
#include "runtime/tixl_point.h"             // SwPoint (64B)

namespace sw {
namespace {

// TiXL anchors (rule 2 — cited constants, never sw snapshots).
constexpr float kSpeed = 1.0f;    // ParticleSystem.t3 Speed DefaultValue (t3:41-42)
constexpr float kDrag = 0.02f;    // CHOSEN test pin (NOT the .t3 default — that is 0.005, t3:33-34)
constexpr float kDirAmount = 0.5f;  // our chosen DirectionalForce.Amount (RandomAmount=0 → hash term == 1)
constexpr uint32_t kEmitCount = 256;  // RadialPoints ring size (== newPointCount, the clean cohort width)
constexpr float kStepScale = 0.01f;   // ParticleSystem.hlsl:111 `pos += velocity * Speed * 0.01`

// Closed-form per-step position delta for a clean (seed, not-re-emitted) slot, faithful to
// ParticleSystem.hlsl:106+111 combined with DirectionalForce.hlsl:24 (RandomAmount=0):
//   Δ = Direction*Amount * pow(1-Drag, Speed) * Speed * 0.01
double closedFormDeltaX(float amount, float drag, float speed) {
  return (double)amount * std::pow(1.0 - (double)drag, (double)speed) * (double)speed * (double)kStepScale;
}

// ---- cook capture (RadialPoints -> ParticleSystem(+DirectionalForce) -> DrawPoints) --------------------
// Same shape as directional_force_parity_golden's captureDir: DrawPoints is a Draw op on the terminal, so
// its captured buffer IS the ResultPoints bag. We swap the target vector between cooks to grab each step.
std::vector<SwPoint>* g_cap = nullptr;
void captureSim(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_cap || !pts || c.count == 0) return;
  g_cap->assign(c.count, SwPoint{});
  std::memcpy(g_cap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

// Cook the rig for exactly 2 steps on ONE PointGraph (state persists so the cycle buffer advances). Captures
// the ResultPoints after the SEED step into `p0` and after the FORCE+INTEGRATE step into `p1`. Speed/Drag/
// Amount are pinned explicitly (rule 5 — no reliance on cook fallbacks).
void cookTwoSteps(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, float amount, float drag,
                  float speed, std::vector<SwPoint>& p0, std::vector<SwPoint>& p1) {
  registerBuiltinPointOps();
  registerDrawOp("DrawPoints", captureSim);
  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)kEmitCount; gen.params["Radius"] = 2.0f;  // pinned scene
  Node sim; sim.id = 2; sim.type = "ParticleSystem";
  sim.params["Speed"] = speed; sim.params["Drag"] = drag;               // pinned integrator params
  Node force; force.id = 4; force.type = "DirectionalForce";
  force.params["Amount"] = amount;
  force.params["Direction.x"] = 1.0f; force.params["Direction.y"] = 0.0f; force.params["Direction.z"] = 0.0f;
  force.params["RandomAmount"] = 0.0f;  // kill the hash jitter → offset == Direction*Amount, closed-form
  Node drw; drw.id = 3; drw.type = "DrawPoints";
  g.nodes = {gen, sim, force, drw};
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points -> emit
  g.connections.push_back({102, pinId(4, 0), pinId(2, 1)});  // DirectionalForce.force -> forces
  g.connections.push_back({103, pinId(2, 2), pinId(3, 0)});  // result -> DrawPoints

  const int targetId = pg.defaultDrawTarget(g);
  auto cookStep = [&](int i, std::vector<SwPoint>& into) {
    g_cap = &into;
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)i; ctx.time = 0.05f * (float)i; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, targetId);
    g_cap = nullptr;
  };
  cookStep(0, p0);  // SEED: emit + reset (InitialVelocity forked 0 → V=0, Pos=emitPos)
  cookStep(1, p1);  // step: DirectionalForce += D, drag, integrate — the formula under test
}

// Mean per-particle X delta over the CLEAN cohort [0,emitCount): slots emitted on the seed step and NOT
// re-emitted on step 1 (provably so — see the addIndex trace in the file header). Requires both endpoints
// alive (non-NaN) and the seed X to match (same slot). Y/Z deltas returned for the sideways-zero check.
struct Delta { double mx, my, mz; size_t n; };
Delta cleanCohortDelta(const std::vector<SwPoint>& p0, const std::vector<SwPoint>& p1) {
  double sx = 0, sy = 0, sz = 0; size_t n = 0;
  const size_t lim = kEmitCount < p0.size() ? kEmitCount : p0.size();
  for (size_t i = 0; i < lim && i < p1.size(); ++i) {
    const SwPoint& a = p0[i];
    const SwPoint& b = p1[i];
    if (std::isnan(a.Position.x) || std::isnan(b.Position.x)) continue;
    // Guard: the clean cohort must not have been re-emitted (a re-emit resets Position to a NEW emit point,
    // producing a wild Δ). A re-emitted slot's Δ would dwarf the ~0.005 integrator step; skip any |Δ|>0.1.
    double dx = (double)b.Position.x - a.Position.x;
    double dy = (double)b.Position.y - a.Position.y;
    double dz = (double)b.Position.z - a.Position.z;
    if (std::fabs(dx) > 0.1 || std::fabs(dy) > 0.1 || std::fabs(dz) > 0.1) continue;
    sx += dx; sy += dy; sz += dz; ++n;
  }
  if (n == 0) return {0, 0, 0, 0};
  return {sx / (double)n, sy / (double)n, sz / (double)n, n};
}

}  // namespace

int runParticleSimIntegrateParitySelfTest(bool injectBug) {
  ParityHarness h;
  ParityReport rep("selftest-particlesim-integrate-parity");
  if (!h.ok()) {
    printf("[selftest-particlesim-integrate-parity] FAIL: no metallib\n");
    return 1;
  }

  // ── MAIN: production cook, measure the integrator's per-step position delta ────────────────────────
  // -bug REAL injection: the latch corrupts the integrator's Drag read (0.02 → 0.5) on the REAL cook path
  // for THIS cook only (the drag probe below cooks clean). Scoped set+reset, never leaks.
  particleSimDragBugForTest() = injectBug;
  std::vector<SwPoint> p0, p1;
  cookTwoSteps(h.dev, h.queue, h.lib, kDirAmount, kDrag, kSpeed, p0, p1);
  particleSimDragBugForTest() = false;
  Delta d = cleanCohortDelta(p0, p1);

  // The clean cohort must be non-empty (the [0,emitCount) slots survived the seed and were not re-emitted).
  rep.expectTrue("cleanCohortAlive(n>0)", d.n > 0, (double)d.n);

  // THE INTEGRATOR TOOTH: measured Δx == the TiXL closed form (drag=pow(1-Drag,Speed), step-scale=0.01) —
  // the expected value in BOTH legs (never flipped). In the -bug leg the latch corrupted the real cook
  // (Drag→0.5 → measured Δx = 0.5·0.5·0.01 = 0.0025), so THIS SAME assert diverges (48× the tol) → RED;
  // if the injection did not trip, it passes and the leg exits 0 (NO-BITE list catches the dead tooth).
  const double expClosed = closedFormDeltaX(kDirAmount, kDrag, kSpeed);   // (0.5·0.98·1·0.01) = 0.0049
  // Tolerance: GPU float epsilon on a ~5e-3 quantity; the faithful form sits well inside this band.
  rep.expect("integratorDeltaX==TiXL(pow-drag,*0.01)", d.mx, expClosed, 5e-5);

  // Sideways components: the push is pure +X, so Δy == Δz == 0 (DirectionalForce Direction=(1,0,0)).
  rep.expect("integratorDeltaY==0", d.my, 0.0, 5e-5);
  rep.expect("integratorDeltaZ==0", d.mz, 0.0, 5e-5);

  // ── DRAG PROBE (both legs, context tooth): the drag term is pow(1-Drag,Speed), NOT linear / ignored ──
  // A second cook with Drag=0.5 must scale Δx by pow(0.5,Speed)/pow(0.98,Speed) relative to the Drag=0.02
  // cook. If drag were ignored the ratio would be 1; if linear it would be (1-0.5)/(1-0.02)=0.51 — both
  // differ from the pow ratio pow(0.5,1)/pow(0.98,1)=0.5102... actually distinct enough to pin the FORM.
  // (This probe cooks with the latch OFF; in the -bug leg the corrupted main cook makes the measured
  // ratio collapse to ~1 → this tooth ALSO bites there, a second independent diverging assert.)
  std::vector<SwPoint> q0, q1;
  cookTwoSteps(h.dev, h.queue, h.lib, kDirAmount, /*drag=*/0.5f, kSpeed, q0, q1);
  Delta dq = cleanCohortDelta(q0, q1);
  const double measuredRatio = (d.mx != 0.0) ? dq.mx / d.mx : 0.0;
  const double expectedRatio = closedFormDeltaX(kDirAmount, 0.5f, kSpeed) / expClosed;  // pow-drag ratio
  rep.expect("dragProbe(Δ@0.5 / Δ@0.02 == pow-ratio)", measuredRatio, expectedRatio, 1e-3);

  // ── SPEED PROBE (both legs): Speed=2 leaves the pow/linear IDENTITY POINT Speed=1 ─────────────────────
  // pow(1-Drag, Speed) vs a stale linear `velocity *= (1-Drag)` (the commented-out ParticleSystem.hlsl:105
  // form a naive port would copy) are the SAME NUMBER at Speed=1 (pow(x,1)==x) — every cook above sits on
  // that identity, so the FORM itself had no tooth until here (2026-07-06 audit, P2). At Drag=0.5, Speed=2:
  // faithful Δx = 0.5·pow(0.5,2)·2·0.01 = 0.0025; the stale linear form gives 0.5·0.5·2·0.01 = 0.005 —
  // 50× the tolerance apart.
  std::vector<SwPoint> r0, r1;
  cookTwoSteps(h.dev, h.queue, h.lib, kDirAmount, /*drag=*/0.5f, /*speed=*/2.0f, r0, r1);
  Delta dr = cleanCohortDelta(r0, r1);
  rep.expect("speedProbe(Δx@Drag0.5,Speed2==pow-form)", dr.mx,
             closedFormDeltaX(kDirAmount, 0.5f, 2.0f), 5e-5);

  return rep.finish();
}

}  // namespace sw
