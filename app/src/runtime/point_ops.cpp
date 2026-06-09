#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"        // calcDispatchCount
#include "runtime/graph.h"           // Graph/Node/findSpec/pinId
#include "runtime/particle_params.h" // RadialParams, RadialBinding
#include "runtime/point_graph.h"     // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"      // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

float paramOr(const Node* n, const char* id, float def) {
  if (!n) return def;
  auto it = n->params.find(id);
  return it != n->params.end() ? it->second : def;
}

// RadialPoints generator: dispatch the radial_points kernel into the node's output bag.
// Reads the Float params it has today (Count via ctx.count; Radius/RadiusOffset/StartAngle/
// Cycles from the node); TiXL's vector params (Axis/Center/Color) + orientation are baked
// TiXL defaults in the kernel until vector params land in NodeSpec.
// NOTE: builds the PSO per cook — fine for the headless golden (one cook). The live loop
// (A1.5) must cache PSOs; flagged there, not here.
void cookRadialPoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  MTL::Function* fn = c.lib->newFunction(NS::String::string("radial_points", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  const Node* n = c.graph ? c.graph->node(c.nodeId) : nullptr;
  RadialParams P{};
  P.Count = c.count;
  P.Radius = paramOr(n, "Radius", 2.0f);
  P.RadiusOffset = paramOr(n, "RadiusOffset", 0.0f);
  P.StartAngle = paramOr(n, "StartAngle", 0.0f);
  P.Cycles = paramOr(n, "Cycles", 1.0f);
  P.ScaleBase = 1.0f;
  P.ScaleByF = 0.0f;

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(c.output, 0, RADIAL_Points);
  enc->setBytes(&P, sizeof(P), RADIAL_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

}  // namespace

void registerBuiltinPointOps() {
  registerPointOp("RadialPoints", cookRadialPoints);
  // A.1+ register here: TransformPoints, DrawPoints (draw), ParticleSystem (stateful sim) ...
}

// ---------------------------------------------------------------------------
// Golden proof of the RadialPoints cook op THROUGH the point-graph.
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

  registerBuiltinPointOps();        // registers RadialPoints
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
  pg.cook(g, ctx, nullptr);

  bool onCircle = captured.size() == N;
  for (const SwPoint& p : captured) {
    float r = std::sqrt(p.Position.x * p.Position.x + p.Position.y * p.Position.y);
    onCircle = onCircle && std::fabs(r - R) < 0.05f;
  }
  // Opposite indices are far apart on a real ring (Cycles=1); Cycles=0 collapses them.
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

}  // namespace sw
