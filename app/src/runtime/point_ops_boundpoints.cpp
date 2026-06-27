// BoundPoints — lane-P MODIFIER op (batch 16): clamp each point's position into an AABB.
// Faithful port of external/tixl .../point/transform/BoundPoints (.cs slots, .hlsl math).
// Reads an input bag (c.inputs[0]) and writes a count-preserving bag (c.output) where each
// point's Position is clamped into a box of (Size * UniformScale) centered at Position. Count is
// INHERITED from upstream.
//
// TiXL parity (BoundPoints.cs/.hlsl):
//   - ports: Points, Position(Vector3,def 0), Size(Vector3,def 1), UniformScale(Single,def 1).
//     The .cs `Spaces` enum is NOT an [Input] -> no Mode port. We match: no Mode port.
//   - math: clamp(p.Position, Position - Size*UniformScale/2, Position + Size*UniformScale/2).
//   - No fork (clamp identical in MSL/HLSL).
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/boundpoints_params.h"  // BoundPointsParams, BoundPointsBinding
#include "runtime/dispatch.h"            // calcDispatchCount
#include "runtime/graph.h"              // Graph/Node/readVecN/pinId
#include "runtime/point_graph.h"        // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tex_op_cache.h"        // cachedComputePSO
#include "runtime/tixl_point.h"         // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// BoundPoints modifier: dispatch the boundpoints kernel input bag -> output bag.
// count comes from c.count (inherited from upstream Points bag). No input bag = safe no-op.
void cookBoundPoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "boundpoints");
  if (!pso) return;

  BoundPointsParams P{};
  P.Count = c.count;
  float pos[3] = {0.0f, 0.0f, 0.0f};
  float size[3] = {1.0f, 1.0f, 1.0f};
  cookVecN(c, "Position", pos, 3, pos);
  cookVecN(c, "Size", size, 3, size);
  P.PositionX = pos[0]; P.PositionY = pos[1]; P.PositionZ = pos[2];
  P.SizeX = size[0]; P.SizeY = size[1]; P.SizeZ = size[2];
  P.UniformScale = cookParam(c, "UniformScale", 1.0f);

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, BOUNDPOINTS_SourcePoints);
  enc->setBuffer(c.output, 0, BOUNDPOINTS_ResultPoints);
  enc->setBytes(&P, sizeof(P), BOUNDPOINTS_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capBound = nullptr;
void captureDrawBound(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capBound || !pts || c.count == 0) return;
  g_capBound->assign(c.count, SwPoint{});
  std::memcpy(g_capBound->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerBoundPointsOp() { registerPointOp("BoundPoints", cookBoundPoints); }

// Golden: LinePoints(N points spread along X from -L..+L, OUTSIDE a unit box) ->
// BoundPoints(Position=0, Size=1, UniformScale=1) -> capture.
// TEETH:
//   (1) count is PRESERVED (modifier never changes point count).
//   (2) EVERY clamped point lands inside [-0.5,0.5] on each axis (clamp to the AABB).
//   (3) the endpoints actually SATURATE at +/-0.5: the line reaches |x|=L>0.5 on both sides, so
//       max output x must be ~+0.5 and min ~-0.5 (the box edges) — proves real clamping, not a
//       coincidental pass on already-inside points.
// injectBug: UniformScale=20 -> box becomes [-10,10] -> larger than the line -> no clamp ->
//   points keep |x| up to L=3, outside [-0.5,0.5] -> "inside box" assertion FAILS.
int runBoundPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 128;
  const float L = 3.0f;
  const float USCALE = injectBug ? 20.0f : 1.0f;
  // The tooth always asserts against the NOMINAL unit box (half=0.5): a correct clamp pins the
  // -3..+3 line at +/-0.5. The bug enlarges the op's box (UniformScale=20) so nothing clamps and
  // the points keep |x|<=3 — OUTSIDE [-0.5,0.5] — which this fixed threshold catches.
  const float half = 0.5f;

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-boundpoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerBoundPointsOp();
  std::vector<SwPoint> captured;
  g_capBound = &captured;
  registerDrawOp("DrawPoints", captureDrawBound);

  Graph g;
  Node gen; gen.id = 1; gen.type = "LinePoints";
  gen.params["Count"] = (float)N;
  gen.params["Length"] = 2.0f * L;      // Pivot=0.5 default -> spread -L..+L about origin
  gen.params["Direction.x"] = 1.0f; gen.params["Direction.y"] = 0.0f; gen.params["Direction.z"] = 0.0f;
  g.nodes.push_back(gen);

  Node bnd; bnd.id = 2; bnd.type = "BoundPoints";
  bnd.params["Position.x"] = 0.0f; bnd.params["Position.y"] = 0.0f; bnd.params["Position.z"] = 0.0f;
  bnd.params["Size.x"] = 1.0f; bnd.params["Size.y"] = 1.0f; bnd.params["Size.z"] = 1.0f;
  bnd.params["UniformScale"] = USCALE;
  g.nodes.push_back(bnd);

  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});

  PointGraph pg(dev, lib, q, 64, 64);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  bool countOk = (captured.size() == N);
  bool allInside = countOk && !captured.empty();
  float maxX = -1e9f, minX = 1e9f;
  const float eps = 1e-4f;
  for (const SwPoint& p : captured) {
    if (p.Position.x > maxX) maxX = p.Position.x;
    if (p.Position.x < minX) minX = p.Position.x;
    if (std::fabs(p.Position.x) > half + eps || std::fabs(p.Position.y) > half + eps ||
        std::fabs(p.Position.z) > half + eps)
      allInside = false;
  }
  // For the real run, the endpoints must saturate to +/-0.5 (the line reaches past the box).
  bool saturated = injectBug ? true
                             : (std::fabs(maxX - 0.5f) < 1e-3f && std::fabs(minX + 0.5f) < 1e-3f);

  bool pass = countOk && allInside && saturated;
  printf("[selftest-boundpoints] n=%zu x∈[%.4f,%.4f](need[-0.5,0.5]) inside=%s saturated=%s -> %s\n",
         captured.size(), minX, maxX, allInside ? "yes" : "NO", saturated ? "yes" : "NO",
         pass ? "PASS" : "FAIL");

  g_capBound = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
