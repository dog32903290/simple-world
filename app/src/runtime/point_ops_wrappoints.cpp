// WrapPoints — lane-P MODIFIER op (batch 16): toroidal box-wrap of each point's position.
// Faithful port of external/tixl .../point/transform/WrapPoints (.cs slots, .hlsl math).
// Reads an input bag (c.inputs[0]) and writes a count-preserving bag (c.output) where each
// point's Position is wrapped (floored-mod) into a box of Size centered at Position. Count is
// INHERITED from upstream.
//
// TiXL parity (WrapPoints.cs/.hlsl):
//   - ports: Points, Position(Vector3,def 0), Size(Vector3,def 1). The .cs `Spaces` enum is NOT
//     an [Input] -> no Mode port (the kernel never branches on space). We match: no Mode port.
//   - math: newPos = floorMod(p.Position - Position + Size/2, Size) - Size/2 + Position.
//   - FORK (see wrappoints.metal): floored modulo, not MSL truncated fmod (TiXL `mod` macro).
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
#include "runtime/tixl_point.h"         // SwPoint (64B)
#include "runtime/wrappoints_params.h"  // WrapPointsParams, WrapPointsBinding

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// WrapPoints modifier: dispatch the wrappoints kernel input bag -> output bag.
// count comes from c.count (inherited from upstream Points bag). No input bag = safe no-op.
void cookWrapPoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  MTL::Function* fn = c.lib->newFunction(NS::String::string("wrappoints", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  WrapPointsParams P{};
  P.Count = c.count;
  float pos[3] = {0.0f, 0.0f, 0.0f};
  float size[3] = {1.0f, 1.0f, 1.0f};
  cookVecN(c, "Position", pos, 3, pos);
  cookVecN(c, "Size", size, 3, size);
  P.PositionX = pos[0]; P.PositionY = pos[1]; P.PositionZ = pos[2];
  P.SizeX = size[0]; P.SizeY = size[1]; P.SizeZ = size[2];

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, WRAPPOINTS_SourcePoints);
  enc->setBuffer(c.output, 0, WRAPPOINTS_ResultPoints);
  enc->setBytes(&P, sizeof(P), WRAPPOINTS_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capWrap = nullptr;
void captureDrawWrap(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capWrap || !pts || c.count == 0) return;
  g_capWrap->assign(c.count, SwPoint{});
  std::memcpy(g_capWrap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerWrapPointsOp() { registerPointOp("WrapPoints", cookWrapPoints); }

// Golden: LinePoints(N points spread along +X from -L..+L, far OUTSIDE a unit box) ->
// WrapPoints(Position=0, Size=1) -> capture.
// TEETH:
//   (1) count is PRESERVED (modifier never changes point count).
//   (2) EVERY wrapped point lands inside the box [-0.5,0.5] on each axis (floored-mod wrap).
//       This specifically exercises the NEGATIVE-side wrap (half the line is at x<0), which is
//       exactly what truncated fmod would get wrong -> the fork is what makes this pass.
//   (3) the wrap is non-trivial: the input x-range (2L) exceeds the box, so the output must NOT
//       equal the input (some point moved) — guards an accidental passthrough.
// injectBug: Size=20 -> the box [-10,10] is larger than the whole line -> no point wraps ->
//   points stay at their original x (up to ~L=3, outside [-0.5,0.5]) -> "inside box" assert FAILS.
int runWrapPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 128;
  const float L = 3.0f;          // line half-length along X (points at |x| up to 3, box is unit)
  const float BOX = injectBug ? 20.0f : 1.0f;
  // The tooth always asserts against the NOMINAL unit box (half=0.5): a correct wrap pulls the
  // -3..+3 line inside [-0.5,0.5]. The bug enlarges the op's box (BOX=20) so nothing wraps and the
  // points stay at |x|<=3 — OUTSIDE [-0.5,0.5] — which this fixed threshold catches.
  const float half = 0.5f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-wrappoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerWrapPointsOp();
  std::vector<SwPoint> captured;
  g_capWrap = &captured;
  registerDrawOp("DrawPoints", captureDrawWrap);

  Graph g;
  Node gen; gen.id = 1; gen.type = "LinePoints";
  gen.params["Count"] = (float)N;
  gen.params["Length"] = 2.0f * L;      // spread -L..+L along the default +X direction
  gen.params["Direction.x"] = 1.0f; gen.params["Direction.y"] = 0.0f; gen.params["Direction.z"] = 0.0f;
  g.nodes.push_back(gen);

  Node wrp; wrp.id = 2; wrp.type = "WrapPoints";
  wrp.params["Position.x"] = 0.0f; wrp.params["Position.y"] = 0.0f; wrp.params["Position.z"] = 0.0f;
  wrp.params["Size.x"] = BOX; wrp.params["Size.y"] = BOX; wrp.params["Size.z"] = BOX;
  g.nodes.push_back(wrp);

  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});

  PointGraph pg(dev, lib, q, 64, 64);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  bool countOk = (captured.size() == N);
  bool allInside = countOk && !captured.empty();
  float maxX = 0.0f;          // max |x| of output (must <= half for the real run)
  const float eps = 1e-4f;
  for (const SwPoint& p : captured) {
    float ax = std::fabs(p.Position.x), ay = std::fabs(p.Position.y), az = std::fabs(p.Position.z);
    if (ax > maxX) maxX = ax;
    if (ax > half + eps || ay > half + eps || az > half + eps) allInside = false;
  }

  bool pass = countOk && allInside;
  printf("[selftest-wrappoints] n=%zu maxAbsX=%.4f(need<=%.3f) allInsideBox=%s -> %s\n",
         captured.size(), maxX, half, allInside ? "yes" : "NO", pass ? "PASS" : "FAIL");

  g_capWrap = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
