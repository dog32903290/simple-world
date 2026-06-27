// ClearSomePoints — lane point_modify MODIFIER op (batch 20): per-point hash kill (Scale→NAN).
// Faithful port of external/tixl .../point/modify/ClearSomePoints (.cs slots, .hlsl math).
// Reads an input bag (c.inputs[0]) and writes a count-preserving bag (c.output) where each
// point whose hash(Resolution,Seed,Repeat,blockIdx) <= Ratio has its Scale set to NAN.
// Count is INHERITED from upstream (count-preserving modifier).
//
// TiXL parity (ClearSomePoints.cs / ClearSomePoints.hlsl):
//   - ports: Points(BufferWithViews), Ratio(float,def 0), Seed(int,def 0),
//            Repeat(int,def 0), Resolution(int,def 0). Exactly 4 scalar inputs + 1 bag.
//   - math (hlsl line 36):
//       pointU = ((i.x - Mod(i.x,Resolution)+1)*_PRIME0 + Seed*_PRIME1) % (Repeat?Repeat:999999999)
//       hash   = hash11u(pointU)
//       if (hash <= Ratio) p.Scale = NAN
//   - count: NOT changed; output buffer size = input bag count.
//
// Named forks (see clearsomepoints.metal):
//   FORK-A: Resolution=0 guard (integer divisor 0 UB in MSL) → cwMod returns 0 (TiXL GPU
//           behaviour is implementation-defined for div-by-zero; our guard matches intent).
//   FORK-B: NAN representation: MSL NAN constant = quiet NaN (same IEEE754 as HLSL NAN).
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"            // calcDispatchCount
#include "runtime/graph.h"              // Graph/Node/readVecN/pinId
#include "runtime/point_graph.h"        // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tex_op_cache.h"       // cachedComputePSO
#include "runtime/tixl_point.h"         // SwPoint (64B)
#include "runtime/clearsomepoints_params.h"  // ClearSomePointsParams, ClearSomePointsBinding

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// ClearSomePoints modifier: dispatch the clearsomepoints kernel input bag -> output bag.
// count comes from c.count (inherited from upstream Points bag). No input bag = safe no-op.
void cookClearSomePoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "clearsomepoints");
  if (!pso) return;

  ClearSomePointsParams P{};
  P.Count      = c.count;
  P.Ratio      = cookParam(c, "Ratio",      0.0f);
  P.Seed       = (int32_t)(cookParam(c, "Seed",       0.0f) + 0.5f);
  P.Repeat     = (int32_t)(cookParam(c, "Repeat",     0.0f) + 0.5f);
  P.Resolution = (int32_t)(cookParam(c, "Resolution", 0.0f) + 0.5f);

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, CLEARSOMEPOINTS_SourcePoints);
  enc->setBuffer(c.output, 0, CLEARSOMEPOINTS_ResultPoints);
  enc->setBytes(&P, sizeof(P), CLEARSOMEPOINTS_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capClear = nullptr;
void captureDrawClear(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capClear || !pts || c.count == 0) return;
  g_capClear->assign(c.count, SwPoint{});
  std::memcpy(g_capClear->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerClearSomePointsOp() { registerPointOp("ClearSomePoints", cookClearSomePoints); }

// Golden: RadialPoints(N=64, Radius=1) → ClearSomePoints → capture via DrawPoints.
//
// TEETH:
//   (1) COUNT PRESERVED: output bag still has N points (count-preserving modifier).
//   (2) RATIO=0 kills NO points: all N points have finite (non-NaN) Scale.x.
//   (3) RATIO=1 kills ALL points: all N points have Scale.x=NaN.
//   (4) MID-RATIO kills roughly the right fraction: Ratio=0.5, Resolution=1
//       → each point gets its own hash → ~half killed.
//       We assert at least 10% and at most 90% are killed (loose bound, avoids hash-
//       distribution fragility while still catching passthrough and full-kill regressions).
//
// injectBug: uses Ratio=0 (kills nothing) but asserts that ALL N points must be killed
//   (i.e., the "Ratio=1 kills all" assertion but applied to a Ratio=0 run). The correct
//   shader with Ratio=0 kills 0 points, so "killed==N" is FALSE → exit 1 (RED). This
//   directly exercises the `hash <= Ratio` kill branch: if Ratio=0 the hash is never <=0
//   so nothing is killed, and the assertion "all killed" fails.
int runClearSomePointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 64;

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-clearsomepoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerClearSomePointsOp();

  // Helper: run ClearSomePoints with given params and capture the result bag.
  auto runOne = [&](float ratio, int seed, int repeat, int resolution) -> std::vector<SwPoint> {
    std::vector<SwPoint> captured;
    g_capClear = &captured;
    registerDrawOp("DrawPoints", captureDrawClear);

    Graph g;
    Node gen; gen.id = 1; gen.type = "RadialPoints";
    gen.params["Count"]     = (float)N;
    gen.params["Radius"]    = 1.0f;
    gen.params["Cycles"]    = 1.0f;
    g.nodes.push_back(gen);

    Node clr; clr.id = 2; clr.type = "ClearSomePoints";
    clr.params["Ratio"]      = ratio;
    clr.params["Seed"]       = (float)seed;
    clr.params["Repeat"]     = (float)repeat;
    clr.params["Resolution"] = (float)resolution;
    g.nodes.push_back(clr);

    Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
    g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
    g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});

    PointGraph pg(dev, lib, q, 64, 64);
    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

    g_capClear = nullptr;
    return captured;
  };

  if (injectBug) {
    // Bug-mode: run with Ratio=0 (correct shader kills 0) but assert ALL must be killed.
    // Correct shader → killed=0 ≠ N → assertion fails → RED.
    auto pts = runOne(0.0f, 0, 0, 0);
    uint32_t killed = 0;
    for (const SwPoint& p : pts) { if (!std::isfinite(p.Scale.x)) ++killed; }
    bool allKill = (killed == N);
    printf("[selftest-clearsomepoints] bug-mode Ratio=0: n=%zu killed=%u allKill=%s -> %s\n",
           pts.size(), killed, allKill ? "yes" : "NO",
           allKill ? "PASS(bad—should not happen)" : "FAIL(expected for RED)");
    lib->release(); q->release(); dev->release(); pool->release();
    return allKill ? 0 : 1;  // exit 1 when correct shader fails -> RED confirmed
  }

  // --- Tooth 1+2: Ratio=0 kills NO points (count preserved, all finite Scale) ---
  {
    auto pts = runOne(0.0f, 0, 0, 0);
    uint32_t killed = 0;
    for (const SwPoint& p : pts) { if (!std::isfinite(p.Scale.x)) ++killed; }
    bool countOk  = (pts.size() == N);
    bool zeroKill = (killed == 0);
    if (!countOk || !zeroKill) {
      printf("[selftest-clearsomepoints] FAIL Ratio=0: n=%zu (need %u) killed=%u (need 0)\n",
             pts.size(), N, killed);
      lib->release(); q->release(); dev->release(); pool->release();
      return 1;
    }
    printf("[selftest-clearsomepoints] Ratio=0: n=%zu killed=%u -> OK\n", pts.size(), killed);
  }

  // --- Tooth 3: Ratio=1 kills ALL points ---
  {
    auto pts = runOne(1.0f, 0, 0, 0);
    uint32_t killed = 0;
    for (const SwPoint& p : pts) { if (!std::isfinite(p.Scale.x)) ++killed; }
    bool countOk = (pts.size() == N);
    bool allKill = (killed == N);
    if (!countOk || !allKill) {
      printf("[selftest-clearsomepoints] FAIL Ratio=1: n=%zu killed=%u (need %u)\n",
             pts.size(), killed, N);
      lib->release(); q->release(); dev->release(); pool->release();
      return 1;
    }
    printf("[selftest-clearsomepoints] Ratio=1: n=%zu killed=%u -> OK\n", pts.size(), killed);
  }

  // --- Tooth 4: Ratio=0.5 Resolution=1 kills a meaningful fraction (10%-90%) ---
  {
    auto pts = runOne(0.5f, 0, 0, 1);
    uint32_t killed = 0;
    for (const SwPoint& p : pts) { if (!std::isfinite(p.Scale.x)) ++killed; }
    bool countOk    = (pts.size() == N);
    uint32_t minKill = N / 10;        // >= 10%
    uint32_t maxKill = N * 9 / 10;   // <= 90%
    bool fracOk = (killed >= minKill && killed <= maxKill);
    if (!countOk || !fracOk) {
      printf("[selftest-clearsomepoints] FAIL Ratio=0.5: n=%zu killed=%u (need %u..%u)\n",
             pts.size(), killed, minKill, maxKill);
      lib->release(); q->release(); dev->release(); pool->release();
      return 1;
    }
    printf("[selftest-clearsomepoints] Ratio=0.5: n=%zu killed=%u (need %u..%u) -> OK\n",
           pts.size(), killed, minKill, maxKill);
  }

  printf("[selftest-clearsomepoints] -> PASS\n");
  lib->release(); q->release(); dev->release(); pool->release();
  return 0;
}

}  // namespace sw
