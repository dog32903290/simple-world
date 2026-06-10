// Headless proof of the point-graph cook MACHINERY (not any real kernel). Split out of
// point_graph.cpp so the cook stays focused as the render-target pivot grows it (mirrors
// graph.cpp / graph_selftest.cpp). CPU-fill stub ops register under real type names; the
// chain RadialPoints→ParticleSystem→DrawPoints must thread the generated bag through the
// middle op to the draw. injectBug makes the middle op ignore its input so x==2 FAILS.
#include "runtime/point_graph.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"       // Graph/Node/pinId
#include "runtime/tixl_point.h"  // SwPoint (64B) + EvaluationContext

namespace sw {
namespace {

bool g_injectBug = false;
std::vector<SwPoint>* g_capture = nullptr;

// Generator: fill `count` points, Position.x = 1.
void stubGen(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  for (uint32_t i = 0; i < c.count; ++i) {
    dst[i] = SwPoint{};
    dst[i].Position = {1.0f, 0.0f, 0.0f};
  }
}
// Modifier: copy input[0] -> output, Position.x *= 2 (proves input threading).
// Bug variant ignores the input and writes x = 0 so the x==2 assertion fails.
void stubMul(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  const MTL::Buffer* in0 = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  const SwPoint* src = in0 ? (const SwPoint*)const_cast<MTL::Buffer*>(in0)->contents() : nullptr;
  for (uint32_t i = 0; i < c.count; ++i) {
    if (g_injectBug || !src) { dst[i] = SwPoint{}; dst[i].Position = {0.0f, 0.0f, 0.0f}; continue; }
    dst[i] = src[i];
    dst[i].Position.x = src[i].Position.x * 2.0f;
  }
}
// Draw (command stream): the real DrawPoints is a cmd op (Points → Command). This stub
// captures the upstream bag for the assertion while emitting a 1-item RenderCommand —
// the assertion semantics (count==8, x==2) are unchanged; only the value's source moved
// from a PointDrawFn's `points` arg to CmdCookCtx.points.
RenderCommand stubDrawCapture(CmdCookCtx& c) {
  RenderCommand rc;
  if (g_capture && c.points && c.count > 0) {
    g_capture->assign(c.count, SwPoint{});
    std::memcpy(g_capture->data(), const_cast<MTL::Buffer*>(c.points)->contents(),
                (size_t)c.count * sizeof(SwPoint));
    rc.items.push_back(RenderDrawItem{c.points, c.count, 3.5f});
  }
  return rc;
}

}  // namespace

int runPointGraphSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  g_injectBug = injectBug;
  std::vector<SwPoint> captured;
  g_capture = &captured;

  // Register CPU-fill stubs under real type names (clean: builtin ops aren't registered
  // during a --selftest run — main dispatches selftests before app init).
  registerPointOp("RadialPoints", stubGen);
  registerPointOp("ParticleSystem", stubMul);
  registerCmdOp("DrawPoints", stubDrawCapture);  // DrawPoints is a cmd op now (batch 2)

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  // Build RadialPoints(Count=8) -> ParticleSystem -> DrawPoints.
  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints"; gen.params["Count"] = 8.0f; g.nodes.push_back(gen);
  Node mid; mid.id = 2; mid.type = "ParticleSystem"; g.nodes.push_back(mid);
  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  // RadialPoints.points(port0) -> ParticleSystem.emit(port0)
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  // ParticleSystem.result(port2) -> DrawPoints.points(port0)
  g.connections.push_back({102, pinId(2, 2), pinId(3, 0)});

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));  // second cook: exercise buffer reuse (no realloc, same result)

  bool ok = captured.size() == 8;
  for (const SwPoint& p : captured) ok = ok && (p.Position.x == 2.0f);

  printf("[selftest-pointgraph] cooked=%zu (want 8) x[0]=%.1f (want 2.0) -> %s\n", captured.size(),
         captured.empty() ? -1.0f : captured[0].Position.x, ok ? "PASS" : "FAIL");

  g_capture = nullptr;
  q->release();
  dev->release();
  pool->release();
  return ok ? 0 : 1;
}

}  // namespace sw
