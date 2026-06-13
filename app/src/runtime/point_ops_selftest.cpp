// Golden proofs for the built-in point operators (point_ops.cpp). Split out so point_ops.cpp
// stays focused on the ops as more land (mirrors graph.cpp / graph_selftest.cpp). Each proof
// cooks a small graph THROUGH the real PointGraph + registered ops and asserts against TiXL's
// behavior; the bug variant injects a real degeneracy so it FAILs (teeth, not a flipped assert).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
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
  printf("[selftest-simop] n=%zu(pool want %u) steps=%d maxDevFromRing=%.4f(need>0.1) amt=%.0f -> %s\n",
         captured.size(), expectPool, STEPS, maxDev, injectBug ? 0.0f : 15.0f, pass ? "PASS" : "FAIL");

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

}  // namespace sw
