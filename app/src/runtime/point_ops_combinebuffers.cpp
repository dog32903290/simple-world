// CombineBuffers — lane-A COMBINE op: cook fn + register + golden. Faithful port of
// external/tixl .../point/combine/CombineBuffers (a MultiInputSlot<BufferWithViews> that
// concatenates its wired point bags end to end). The first COMBINE op (N Points inputs -> one
// output bag) and the TEMPLATE the batch-3 fan-out copies.
//
// No shader, no params: concatenation is a pure GPU blit (copy each input's bytes into the
// output at a running offset). The output count = sum of all wired Points inputs — provided by
// PointGraph::nodeCount (the sumPointsCount contract) + the per-input counts in c.inputCounts.
//
// v1 exposes a FIXED 4 Points input ports (input0..input3) for TiXL's dynamic MultiInput;
// unwired inputs contribute 0 points. Bump the port count if more inputs are needed.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"        // Graph/Node/pinId
#include "runtime/point_graph.h"  // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"   // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// CombineBuffers: concatenate the wired input bags into the output bag via blit copies.
// Each input i contributes c.inputCounts[i] points at the running offset; the total equals
// c.count (the summed-count contract). Unwired / empty inputs are skipped. No input = no-op.
void cookCombineBuffers(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.inputCounts) return;
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
  uint32_t offset = 0;  // running destination offset in POINTS
  for (int i = 0; i < c.inputCount; ++i) {
    const MTL::Buffer* src = c.inputs[i];
    uint32_t n = c.inputCounts[i];
    if (!src || n == 0) continue;
    blit->copyFromBuffer(src, 0, c.output, (NS::UInteger)offset * sizeof(SwPoint),
                         (NS::UInteger)n * sizeof(SwPoint));
    offset += n;
  }
  blit->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capCombine = nullptr;
void captureDrawCombine(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capCombine || !pts || c.count == 0) return;
  g_capCombine->assign(c.count, SwPoint{});
  std::memcpy(g_capCombine->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerCombineBuffersOp() { registerPointOp("CombineBuffers", cookCombineBuffers); }

// Golden: RadialPoints(N1 ring, radius RR at origin, XY plane) into input0 + SpherePoints(N2
// sphere, radius SR about Center=(10,0,0)) into input1 -> CombineBuffers -> capture. Asserts the
// summed-count contract end to end: (1) count == N1+N2; (2) slice [0,N1) is the ring (radius RR
// from origin, z~0); (3) slice [N1,N1+N2) is the sphere (radius SR from (10,0,0)). The two
// generators are spatially disjoint so a mis-concatenation can't alias.
// injectBug: wire ONLY input0 -> sumPointsCount == N1 -> captured.size() != N1+N2 -> FAILs (a
// real degeneracy of the count contract, not a flipped assert).
int runCombineBuffersSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N1 = 64, N2 = 128;
  const float RR = 2.0f;                       // ring radius (input0)
  const float SR = 5.0f, SCX = 10.0f;          // sphere radius + center x (input1)

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-combinebuffers] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();     // RadialPoints + SpherePoints (the input generators)
  registerCombineBuffersOp();    // this op (explicit -> self-contained)
  std::vector<SwPoint> captured;
  g_capCombine = &captured;
  registerDrawOp("DrawPoints", captureDrawCombine);

  PointGraph pg(dev, lib, q, 64, 64);

  Graph g;
  Node ring; ring.id = 1; ring.type = "RadialPoints";
  ring.params["Count"] = (float)N1; ring.params["Radius"] = RR; ring.params["Cycles"] = 1.0f;
  g.nodes.push_back(ring);
  Node sph; sph.id = 2; sph.type = "SpherePoints";
  sph.params["Count"] = (float)N2; sph.params["Radius"] = SR; sph.params["Center.x"] = SCX;
  g.nodes.push_back(sph);
  Node comb; comb.id = 3; comb.type = "CombineBuffers"; g.nodes.push_back(comb);
  Node drw; drw.id = 4; drw.type = "DrawPoints"; g.nodes.push_back(drw);

  // RadialPoints.out -> CombineBuffers.input0 (port 0)
  g.connections.push_back({101, pinId(1, 0), pinId(3, 0)});
  if (!injectBug)  // bug: leave input1 unwired -> sum == N1 only
    g.connections.push_back({102, pinId(2, 0), pinId(3, 1)});  // SpherePoints.out -> input1 (port 1)
  // CombineBuffers.out (port 4) -> DrawPoints.points
  g.connections.push_back({103, pinId(3, 4), pinId(4, 0)});

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  uint32_t want = injectBug ? N1 : (N1 + N2);
  bool countOK = captured.size() == (N1 + N2);  // teeth: the bug breaks this (size == N1)
  bool ringOK = captured.size() >= N1;
  bool sphereOK = captured.size() == (N1 + N2);
  for (uint32_t i = 0; i < captured.size(); ++i) {
    const SwPoint& p = captured[i];
    if (i < N1) {  // ring slice: radius RR from origin in XY, z ~ 0
      float r = std::sqrt(p.Position.x * p.Position.x + p.Position.y * p.Position.y);
      ringOK = ringOK && std::fabs(r - RR) < 0.05f && std::fabs(p.Position.z) < 0.05f;
    } else {       // sphere slice: radius SR from (SCX,0,0)
      float dx = p.Position.x - SCX, dy = p.Position.y, dz = p.Position.z;
      sphereOK = sphereOK && std::fabs(std::sqrt(dx * dx + dy * dy + dz * dz) - SR) < 0.05f;
    }
  }
  bool pass = countOK && ringOK && sphereOK;
  printf("[selftest-combinebuffers] n=%zu(want %u) ring=%d sphere=%d -> %s\n",
         captured.size(), want, ringOK ? 1 : 0, sphereOK ? 1 : 0, pass ? "PASS" : "FAIL");

  g_capCombine = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
