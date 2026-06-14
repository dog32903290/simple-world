// Golden proofs for the built-in point operators (point_ops.cpp). Split out so point_ops.cpp
// stays focused on the ops as more land (mirrors graph.cpp / graph_selftest.cpp). Each proof
// cooks a small graph THROUGH the real PointGraph + registered ops and asserts against TiXL's
// behavior; the bug variant injects a real degeneracy so it FAILs (teeth, not a flipped assert).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"            // Graph/Node/pinId
#include "runtime/particle_params.h"  // particlePoolCount (ParticleSystem pool sizing)
#include "runtime/point_graph.h"      // PointCookCtx, registerBuiltinPointOps/DrawOp, PointGraph
#include "runtime/tixl_point.h"       // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {
std::vector<SwPoint>* g_cap = nullptr;
void captureDraw(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_cap || !pts || c.count == 0) return;
  g_cap->assign(c.count, SwPoint{});
  std::memcpy(g_cap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}
MTL::Library* loadLib(MTL::Device* dev) {
  NS::Error* err = nullptr;
  return dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
}
}  // namespace

int runRadialOpSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 64;
  const float R = 2.0f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  MTL::Library* lib = loadLib(dev);
  if (!lib) {
    printf("[selftest-radialop] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();  // registers RadialPoints
  std::vector<SwPoint> captured;
  g_cap = &captured;
  registerDrawOp("DrawPoints", captureDraw);  // capture-only draw for the assertion

  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)N;
  gen.params["Radius"] = R;
  gen.params["Cycles"] = injectBug ? 0.0f : 1.0f;  // bug: 0 turns -> all points collapse
  g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points -> DrawPoints.points

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  bool onCircle = captured.size() == N;
  for (const SwPoint& p : captured) {
    float r = std::sqrt(p.Position.x * p.Position.x + p.Position.y * p.Position.y);
    onCircle = onCircle && std::fabs(r - R) < 0.05f;
  }
  float spread = 0.0f;
  if (captured.size() == N) {
    const SwPoint& a = captured[0];
    const SwPoint& b = captured[N / 2];
    float dx = a.Position.x - b.Position.x, dy = a.Position.y - b.Position.y;
    spread = std::sqrt(dx * dx + dy * dy);
  }
  bool pass = onCircle && spread > 0.5f;
  printf("[selftest-radialop] n=%zu onCircle(R=%.1f)=%d spread=%.3f(need>0.5) -> %s\n",
         captured.size(), R, onCircle ? 1 : 0, spread, pass ? "PASS" : "FAIL");

  g_cap = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// Vector-param contract golden: cook RadialPoints with Center=(5,0,0), assert the WHOLE ring
// translated — mean x ~= 5 and each point still sits radius R from the new center. Proves the
// Center vector param (NodeSpec Vec ports -> readVecN -> RadialParams.CenterXYZ -> shader)
// flows end to end. injectBug: don't set Center (stays 0) so the mean-x==5 assertion FAILs —
// teeth proving the test checks translation, not something trivially true.
int runRadialCenterSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 64;
  const float R = 2.0f;
  const float CX = 5.0f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  MTL::Library* lib = loadLib(dev);
  if (!lib) {
    printf("[selftest-radialcenter] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  std::vector<SwPoint> captured;
  g_cap = &captured;
  registerDrawOp("DrawPoints", captureDraw);

  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)N;
  gen.params["Radius"] = R;
  gen.params["Cycles"] = 1.0f;
  if (!injectBug) gen.params["Center.x"] = CX;  // bug: leave Center at 0 -> no translation
  g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  float meanX = 0.0f;
  bool ringOK = captured.size() == N;
  for (const SwPoint& p : captured) {
    meanX += p.Position.x;
    float dx = p.Position.x - CX, dy = p.Position.y;  // distance from the TRANSLATED center
    ringOK = ringOK && std::fabs(std::sqrt(dx * dx + dy * dy) - R) < 0.05f;
  }
  if (!captured.empty()) meanX /= (float)captured.size();
  bool pass = (captured.size() == N) && std::fabs(meanX - CX) < 0.1f && ringOK;
  printf("[selftest-radialcenter] n=%zu meanX=%.3f(need~%.1f) ringAtCenter=%d -> %s\n",
         captured.size(), meanX, CX, ringOK ? 1 : 0, pass ? "PASS" : "FAIL");

  g_cap = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// cook RadialPoints -> DrawPoints (REAL renderer), readback target, assert lit ring + black
// center. injectBug = 0 points -> nothing drawn -> all black -> FAIL.
int runDrawOpSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 256, W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  MTL::Library* lib = loadLib(dev);
  if (!lib) {
    printf("[selftest-drawop] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();  // RadialPoints (cook) + DrawPoints (real draw)
  PointGraph pg(dev, lib, q, W, H);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)(injectBug ? 0u : N);  // bug: 0 points -> nothing drawn
  gen.params["Radius"] = 2.0f;
  g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  std::vector<uint8_t> px(W * H * 4, 0);
  pg.target()->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  auto lit = [&](uint32_t x, uint32_t y) {
    const uint8_t* p = &px[(y * W + x) * 4];
    return p[0] > 30 || p[1] > 30 || p[2] > 30;
  };
  int nonBlack = 0;
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x)
      if (lit(x, y)) ++nonBlack;
  bool centerBlack = true;
  for (uint32_t y = H / 2 - 8; y < H / 2 + 8; ++y)
    for (uint32_t x = W / 2 - 8; x < W / 2 + 8; ++x)
      if (lit(x, y)) centerBlack = false;
  bool pass = nonBlack > 50 && centerBlack;
  printf("[selftest-drawop] nonBlack=%d(need>50) centerBlack=%d -> %s\n", nonBlack,
         centerBlack ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// cook RadialPoints -> ParticleSystem(sim) -> capture, step N frames, assert turbulence pushed
// points OFF the emit ring (faithful to --selftest-flow, via the cook). injectBug: Amount=0.
int runSimOpSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 1024;
  const float R = 2.0f;
  const int STEPS = 30;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  MTL::Library* lib = loadLib(dev);
  if (!lib) {
    printf("[selftest-simop] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();  // RadialPoints + ParticleSystem(sim) + DrawPoints
  std::vector<SwPoint> captured;
  g_cap = &captured;
  registerDrawOp("DrawPoints", captureDraw);  // capture the sim output for assertion

  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)N; gen.params["Radius"] = R;
  Node sim; sim.id = 2; sim.type = "ParticleSystem";
  Node turb; turb.id = 4; turb.type = "TurbulenceForce";
  turb.params["Amount"] = injectBug ? 0.0f : 15.0f; turb.params["Frequency"] = 1.2f;
  Node drw; drw.id = 3; drw.type = "DrawPoints";
  g.nodes.push_back(gen); g.nodes.push_back(sim); g.nodes.push_back(turb); g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points -> ParticleSystem.emit
  g.connections.push_back({102, pinId(4, 0), pinId(2, 1)});  // TurbulenceForce.force -> ParticleSystem.forces
  g.connections.push_back({103, pinId(2, 2), pinId(3, 0)});  // ParticleSystem.result -> DrawPoints.points

  const int targetId = pg.defaultDrawTarget(g);
  for (int i = 0; i < STEPS; ++i) {
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)i; ctx.time = 0.05f * (float)i; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, targetId);
  }

  float maxDev = 0.0f;
  for (const SwPoint& p : captured) {
    float r = std::sqrt(p.Position.x * p.Position.x + p.Position.y * p.Position.y);
    float dev = std::fabs(r - R);
    if (!std::isnan(dev) && dev > maxDev) maxDev = dev;
  }
  // ParticleSystem now sizes its output to the particle POOL (= particlePoolCount(emitCount)),
  // not the emit ring — the cycle buffer that makes recycling possible (batch-6 decay fix).
  const uint32_t expectPool = particlePoolCount(N);
  bool pass = captured.size() == expectPool && maxDev > 0.1f;  // turbulence flowed off the ring
  // REFUTER-F probe (byte-equivalence差分): a high-precision checksum of the FULL captured
  // turbulence buffer. Identical between parent 4742203 (old inline turbulence) and HEAD
  // (new _ForceKind else-branch) iff the turbulence path is byte-equivalent. Not asserted —
  // diffed across builds by the refuter. Sum-of-bits is order-stable (capture order fixed).
  unsigned long long chk = 1469598103934665603ULL;  // FNV-1a over raw position bytes
  for (const SwPoint& p : captured) {
    const unsigned char* b = reinterpret_cast<const unsigned char*>(&p.Position);
    for (size_t k = 0; k < 3 * sizeof(float); ++k) { chk ^= b[k]; chk *= 1099511628211ULL; }
  }
  printf("[selftest-simop] n=%zu(pool want %u) steps=%d maxDevFromRing=%.4f(need>0.1) amt=%.0f chk=%016llx -> %s\n",
         captured.size(), expectPool, STEPS, maxDev, injectBug ? 0.0f : 15.0f, chk, pass ? "PASS" : "FAIL");

  g_cap = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// Shared force-op golden body: cook RadialPoints -> ParticleSystem(forceType wired) -> capture,
// step N frames, return the mean position of the live pool. The force pushes particles in a
// known direction; each force's own assertion checks the mean shifted that way (deterministic —
// the force is constant, so the pool's center-of-mass tracks it). injectBug sets Amount=0 so the
// pass leaves the ring symmetric (mean ~ 0) and the directional assertion FAILs. forceType is
// the NodeSpec type string ("DirectionalForce"/"VectorFieldForce"); the wired node's params map
// (incl. its pinless _ForceKind default) selects the kernel in cookParticleSim.
namespace {
struct ForceMean { float mx, my, mz; size_t n; };
ForceMean cookForceMean(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                        const char* forceType, float amount, uint32_t N, int steps) {
  registerBuiltinPointOps();
  std::vector<SwPoint> captured;
  g_cap = &captured;
  registerDrawOp("DrawPoints", captureDraw);
  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)N; gen.params["Radius"] = 2.0f;
  Node sim; sim.id = 2; sim.type = "ParticleSystem";
  Node force; force.id = 4; force.type = forceType;
  force.params["Amount"] = amount;  // the only knob the test drives; _ForceKind = spec default
  Node drw; drw.id = 3; drw.type = "DrawPoints";
  g.nodes.push_back(gen); g.nodes.push_back(sim); g.nodes.push_back(force); g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points -> emit
  g.connections.push_back({102, pinId(4, 0), pinId(2, 1)});  // <force>.force -> forces
  g.connections.push_back({103, pinId(2, 2), pinId(3, 0)});  // result -> DrawPoints

  const int targetId = pg.defaultDrawTarget(g);
  for (int i = 0; i < steps; ++i) {
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)i; ctx.time = 0.05f * (float)i; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, targetId);
  }
  double sx = 0, sy = 0, sz = 0; size_t n = 0;
  for (const SwPoint& p : captured) {
    if (std::isnan(p.Position.x) || std::isnan(p.Position.y) || std::isnan(p.Position.z)) continue;
    sx += p.Position.x; sy += p.Position.y; sz += p.Position.z; ++n;
  }
  g_cap = nullptr;
  if (n == 0) return {0, 0, 0, 0};
  return {(float)(sx / n), (float)(sy / n), (float)(sz / n), n};
}
}  // namespace

// DirectionalForce golden: a constant push along the (default) Direction=(0,-1,0) drags the
// pool's center-of-mass DOWN (mean Y << 0). injectBug Amount=0 -> no push -> ring stays
// symmetric (|mean Y| ~ 0) -> the assertion FAILs (RED). Direction default is faithful to
// DirectionalForce.t3; the test drives only Amount (large, to clear drag over the window).
int runDirectionalForceSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  MTL::Library* lib = loadLib(dev);
  if (!lib) { printf("[selftest-directionalforce] FAIL: no metallib\n");
              q->release(); dev->release(); pool->release(); return 1; }

  ForceMean m = cookForceMean(dev, q, lib, "DirectionalForce", injectBug ? 0.0f : 60.0f, 1024, 30);
  // Default Direction=(0,-1,0): the force is purely -Y, so the signal is meanY (X/Z stay ~0).
  bool pass = m.n > 0 && m.my < -0.05f;
  printf("[selftest-directionalforce] n=%zu meanPos=(%.3f,%.3f,%.3f) (need meanY<-0.05) amt=%.0f -> %s\n",
         m.n, m.mx, m.my, m.mz, injectBug ? 0.0f : 60.0f, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// VectorFieldForce golden: with no field bound (fork-VFF), GetField()=(1,1,1,1), so the force is
// a constant diagonal (1,1,1) push -> the pool's center-of-mass drifts along +X+Y+Z equally
// (each mean component > 0, and they track each other). injectBug Amount=0 -> no drift -> means
// stay ~0 -> FAILs (RED). This asserts BOTH the magnitude AND the diagonal isotropy ((1,1,1)).
int runVectorFieldForceSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  MTL::Library* lib = loadLib(dev);
  if (!lib) { printf("[selftest-vectorfieldforce] FAIL: no metallib\n");
              q->release(); dev->release(); pool->release(); return 1; }

  ForceMean m = cookForceMean(dev, q, lib, "VectorFieldForce", injectBug ? 0.0f : 6.0f, 1024, 30);
  // (1,1,1) push: every component drifts positive AND they stay close to each other (isotropy).
  bool drifted = m.n > 0 && m.mx > 0.05f && m.my > 0.05f && m.mz > 0.05f;
  float spread = std::fmax(std::fmax(std::fabs(m.mx - m.my), std::fabs(m.my - m.mz)),
                           std::fabs(m.mx - m.mz));
  bool isotropic = spread < 0.5f * std::fabs(m.mx);  // components track (1,1,1), not one axis
  bool pass = drifted && isotropic;
  printf("[selftest-vectorfieldforce] n=%zu meanPos=(%.3f,%.3f,%.3f) spread=%.3f (need all>0.05 & iso) amt=%.0f -> %s\n",
         m.n, m.mx, m.my, m.mz, spread, injectBug ? 0.0f : 6.0f, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// ---------------------------------------------------------------------------------------------
// 批次24 — VelocityForce / AxisStepForce / SnapToAnglesForce goldens.
//
// These forces TRANSFORM existing velocity (Velocity rescales speed; SnapToAngles snaps the
// velocity direction) — unlike Turbulence/Directional/VectorField/AxisStep which CREATE velocity.
// In the full RadialPoints->ParticleSystem rig the emitted particles start with ZERO velocity
// (the cook bakes EmitVelocity/InitialVelocity = 0; TiXL's default is 1.0 — a pre-existing parity
// gap in the integrator, NOT introduced here). With zero velocity Velocity/SnapToAngles are
// correctly NO-OPS (VelocityForce.hlsl `if(speed<0.0001) return`; SnapOrientationForce.hlsl
// `if(lengthXY<0.00001) return`), so the integrator rig can't exercise their math.
//
// So these goldens dispatch the force kernel DIRECTLY on a synthetic Particle buffer with KNOWN
// nonzero velocities (a precise unit test of the ported math, stronger teeth than a drift bound).
// AxisStepForce CREATES velocity from zero, so it also unit-tests cleanly here.
namespace {
MTL::ComputePipelineState* makeForcePSO(MTL::Device* dev, MTL::Library* lib, const char* name) {
  if (!lib) return nullptr;
  MTL::Function* fn = lib->newFunction(NS::String::string(name, NS::UTF8StringEncoding));
  if (!fn) return nullptr;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);
  fn->release();
  return pso;
}
// Build N particles, write velocities via `seed(i)`, dispatch `kernelName` with `params`, read back.
template <typename ParamsT, typename SeedFn>
std::vector<Particle> dispatchForce(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                                    const char* kernelName, const ParamsT& params, uint32_t N,
                                    SeedFn seed) {
  std::vector<Particle> out;
  MTL::ComputePipelineState* pso = makeForcePSO(dev, lib, kernelName);
  if (!pso) return out;
  MTL::Buffer* buf = dev->newBuffer((NS::UInteger)N * sizeof(Particle), MTL::ResourceStorageModeShared);
  Particle* p = static_cast<Particle*>(buf->contents());
  for (uint32_t i = 0; i < N; ++i) {
    p[i] = Particle{};
    p[i].Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};  // identity quat
    p[i].BirthTime = 0.0f;                               // emitted (not NaN) — passes VFF/snap guards
    seed(i, p[i]);
  }
  const uint32_t tg = 64;
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(buf, 0, FORCE_Particles);
  enc->setBytes(&params, sizeof(params), FORCE_Params);
  enc->dispatchThreadgroups(MTL::Size::Make((N + tg - 1) / tg, 1, 1), MTL::Size::Make(tg, 1, 1));
  enc->endEncoding(); cmd->commit(); cmd->waitUntilCompleted();
  out.assign(N, Particle{});
  std::memcpy(out.data(), buf->contents(), (size_t)N * sizeof(Particle));
  buf->release(); pso->release();
  return out;
}
inline float vlen3(SW_PACKED3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
}  // namespace

// VelocityForce golden (direct kernel unit test): seed every particle with velocity (1,0,0)
// (speed=1). With Accelerate>0 + Amount>0 the kernel does speed += Accelerate*0.02*strength, so
// speed grows ABOVE 1 (along the same +X direction). injectBug Amount=0 -> strength=0 -> speed
// stays 1 -> FAIL (teeth). MinSpeed/MaxSpeed wide so the clamp doesn't gate. Variation=0 so the
// per-particle hash/GainAndBias term collapses to variationFactor=1 (deterministic).
int runVelocityForceSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  MTL::Library* lib = loadLib(dev);
  if (!lib) { printf("[selftest-velocityforce] FAIL: no metallib\n");
              q->release(); dev->release(); pool->release(); return 1; }

  const uint32_t N = 256;
  VelForceParams P{};
  P.Amount = injectBug ? 0.0f : 1.0f;  // bug: strength 0 -> no acceleration
  P.Accelerate = 50.0f; P.MinSpeed = 0.0f; P.MaxSpeed = 1000.0f; P.Variation = 0.0f;
  P.GainBiasX = 0.5f; P.GainBiasY = 0.5f; P.Count = N;
  auto res = dispatchForce(dev, q, lib, "velocity_force", P, N,
      [](uint32_t, Particle& pt){ pt.Velocity = SW_PACKED3{1.0f, 0.0f, 0.0f}; });

  // Expected non-bug: speed = 1 + Accelerate*0.02*Amount = 1 + 50*0.02*1 = 2.0, direction +X kept.
  bool ok = !res.empty();
  float speed = res.empty() ? 0.0f : vlen3(res[0].Velocity);
  bool dirKept = !res.empty() && res[0].Velocity.x > 0.0f &&
                 std::fabs(res[0].Velocity.y) < 1e-4f && std::fabs(res[0].Velocity.z) < 1e-4f;
  bool pass = ok && dirKept && (speed > 1.10f);  // accelerated past the seed speed of 1
  printf("[selftest-velocityforce] N=%u speed=%.3f (seed=1.0, need>1.10 & +X) amt=%.1f -> %s\n",
         N, speed, injectBug ? 0.0f : 1.0f, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// AxisStepForce golden (direct kernel unit test): seed every particle with ZERO velocity (the op
// CREATES velocity). SelectRatio=1 (all selected) + Strength large + ApplyTrigger=1 -> every
// particle gets a strong axis-aligned kick -> mean |velocity| >> 0. injectBug ApplyTrigger=0 ->
// lerp factor 0 -> velocity stays 0 -> FAIL (teeth). AddOriginalVelocity=0 isolates the kick.
int runAxisStepForceSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  MTL::Library* lib = loadLib(dev);
  if (!lib) { printf("[selftest-axisstepforce] FAIL: no metallib\n");
              q->release(); dev->release(); pool->release(); return 1; }

  const uint32_t N = 1024;
  AxisStepForceParams P{};
  P.ApplyTrigger = injectBug ? 0.0f : 1.0f;  // bug: trigger off -> lerp 0 -> no kick
  P.Strength = 5.0f; P.RandomizeStrength = 0.0f; P.SelectRatio = 1.0f;
  P.AxisDistributionX = 1.0f; P.AxisDistributionY = 1.0f; P.AxisDistributionZ = 1.0f;
  P.AddOriginalVelocity = 0.0f;
  P.StrengthDistributionX = 1.0f; P.StrengthDistributionY = 1.0f; P.StrengthDistributionZ = 1.0f;
  P.Seed = 0.0f; P.AxisSpace = 0.0f; P.Count = N;
  auto res = dispatchForce(dev, q, lib, "axis_step_force", P, N,
      [](uint32_t, Particle& pt){ pt.Velocity = SW_PACKED3{0.0f, 0.0f, 0.0f}; });

  double sumSpeed = 0; size_t n = 0;
  for (const Particle& pt : res) { sumSpeed += vlen3(pt.Velocity); ++n; }
  float meanSpeed = n ? (float)(sumSpeed / n) : 0.0f;
  bool pass = n > 0 && meanSpeed > 0.5f;  // kicked particles carry real speed; bug -> ~0
  printf("[selftest-axisstepforce] N=%u meanSpeed=%.3f (need>0.5) trig=%.0f -> %s\n",
         N, meanSpeed, injectBug ? 0.0f : 1.0f, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// SnapToAnglesForce golden (direct kernel unit test): seed each particle with a DIFFERENT planar
// velocity direction (angles spread around the xy circle, speed=1). With AngleCount=360
// (subdivisions = 360/360 = 1, a SINGLE allowed angle) + Amount=1 + Variation=0 + Mode=1
// (WorldXY, camera-free), EVERY velocity direction snaps to the SAME angle -> all output planar
// directions become collinear -> they all point the same way (dot with the first ~ +1). injectBug
// Amount=0 -> no snap -> directions stay spread (mean pairwise dot low) -> FAIL (teeth).
int runSnapToAnglesForceSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  MTL::Library* lib = loadLib(dev);
  if (!lib) { printf("[selftest-snaptoanglesforce] FAIL: no metallib\n");
              q->release(); dev->release(); pool->release(); return 1; }

  const uint32_t N = 256;
  SnapAnglesForceParams P{};
  P.Amount = injectBug ? 0.0f : 1.0f;  // bug: no snap
  P.SnapAngle = 360.0f;                // one allowed angle -> all snap collinear
  P.PhaseAngle = 0.0f; P.Variation = 0.0f; P.VariationRatio = 0.0f; P.KeepPlanar = 0.0f;
  P.SpaceAndPlane = 1.0f;              // WorldXY (camera-free path)
  P.RandomSeed = 0.0f; P.Count = N;
  auto res = dispatchForce(dev, q, lib, "snaptoanglesforce", P, N,
      [N](uint32_t i, Particle& pt){
        float a = 6.2831853f * (float)i / (float)N;  // spread directions around the xy circle
        pt.Velocity = SW_PACKED3{std::cos(a), std::sin(a), 0.0f};
      });

  // After a collinear snap every planar direction is identical -> dot(dir_i, dir_0) ~ +1 for all.
  // injectBug keeps them spread -> mean dot ~ 0.
  bool ok = res.size() == N;
  float n0 = ok ? std::sqrt(res[0].Velocity.x * res[0].Velocity.x + res[0].Velocity.y * res[0].Velocity.y) : 0.0f;
  double sumDot = 0; size_t cnt = 0;
  if (ok && n0 > 1e-5f) {
    float d0x = res[0].Velocity.x / n0, d0y = res[0].Velocity.y / n0;
    for (const Particle& pt : res) {
      float n = std::sqrt(pt.Velocity.x * pt.Velocity.x + pt.Velocity.y * pt.Velocity.y);
      if (n < 1e-5f) continue;
      sumDot += (pt.Velocity.x / n) * d0x + (pt.Velocity.y / n) * d0y;
      ++cnt;
    }
  }
  float meanDot = cnt ? (float)(sumDot / cnt) : 0.0f;
  bool pass = ok && cnt > 0 && meanDot > 0.95f;  // all collinear -> ~1.0; bug (spread) -> ~0
  printf("[selftest-snaptoanglesforce] N=%u meanDot=%.3f (collinear need>0.95) amt=%.0f -> %s\n",
         N, meanDot, injectBug ? 0.0f : 1.0f, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// REFUTER-F probe (批次16 Lane F assertion 2 — cook side of the corruption path). A .swproj can be
// hand-edited / corrupted to carry an OUT-OF-RANGE `_ForceKind` override (compound_load keeps it
// UNCLAMPED; proven by forcekindcorrupt). This drives that value into the runtime Node.params and
// cooks a DirectionalForce, asking: does an out-of-range kind CRASH or select an out-of-bounds
// kernel? It does NOT — cookParticleSim's dispatch is `if(==DIR) elif(==VECFIELD) else{turbulence}`,
// so ANY unrecognized kind falls to the turbulence else (a safe, bounded MISROUTE: a DirectionalForce
// graph silently runs turbulence). This probe proves no-crash AND documents that misroute.
namespace {
// Cook a DirectionalForce with an explicit _ForceKind override + a strong down-push Amount; return
// meanY of the live pool. Direction default (0,-1,0) -> a working directional pass drags meanY << 0.
float cookDirForceKindMeanY(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, float forceKind) {
  registerBuiltinPointOps();
  std::vector<SwPoint> captured;
  g_cap = &captured;
  registerDrawOp("DrawPoints", captureDraw);
  PointGraph pg(dev, lib, q, 64, 64);
  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 1024.0f; gen.params["Radius"] = 2.0f;
  Node sim; sim.id = 2; sim.type = "ParticleSystem";
  Node force; force.id = 4; force.type = "DirectionalForce";
  force.params["Amount"] = 60.0f;            // strong push so the signal clears drag
  force.params["_ForceKind"] = forceKind;    // <-- the corrupted discriminator under test
  Node drw; drw.id = 3; drw.type = "DrawPoints";
  g.nodes.push_back(gen); g.nodes.push_back(sim); g.nodes.push_back(force); g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(4, 0), pinId(2, 1)});
  g.connections.push_back({103, pinId(2, 2), pinId(3, 0)});
  const int targetId = pg.defaultDrawTarget(g);
  for (int i = 0; i < 30; ++i) {
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)i; ctx.time = 0.05f * (float)i; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, targetId);
  }
  double sy = 0; size_t n = 0;
  for (const SwPoint& p : captured)
    if (!std::isnan(p.Position.y)) { sy += p.Position.y; ++n; }
  g_cap = nullptr;
  return n ? (float)(sy / n) : 0.0f;
}
}  // namespace

int runForceKindOobSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  MTL::Library* lib = loadLib(dev);
  if (!lib) { printf("[selftest-forcekindoob] FAIL: no metallib\n");
              q->release(); dev->release(); pool->release(); return 1; }

  // kind=1 (DIRECTIONAL): the down-push works -> meanY << 0 (the control).
  float yDir = cookDirForceKindMeanY(dev, q, lib, 1.0f);
  // kind=99 (corrupt high): unrecognized -> falls to turbulence else -> NOT a directional down-push
  // -> meanY ~ 0 (turbulence is symmetric about the ring). No crash = we returned at all.
  float y99 = cookDirForceKindMeanY(dev, q, lib, injectBug ? 1.0f : 99.0f);
  // kind=-5 (corrupt negative): (int)(-5+0.5) = -4 -> else -> turbulence. No crash.
  float yNeg = cookDirForceKindMeanY(dev, q, lib, -5.0f);

  bool dirWorks = yDir < -0.05f;                 // kind=1 really pushes down (sanity control)
  bool oobIsTurbulence = std::fabs(y99) < 0.05f; // kind=99 misroutes to symmetric turbulence
  bool negIsTurbulence = std::fabs(yNeg) < 0.05f;
  bool pass = dirWorks && oobIsTurbulence && negIsTurbulence;  // injectBug makes y99 a down-push -> FAIL
  printf("[selftest-forcekindoob] meanY: kind1=%.3f(need<-.05) kind99=%.3f(need~0, fell to turb) "
         "kindNeg=%.3f(need~0) NO-CRASH -> %s\n",
         yDir, y99, yNeg, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
