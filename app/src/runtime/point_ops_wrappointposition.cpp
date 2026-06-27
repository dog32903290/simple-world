// WrapPointPosition — batch 19 MODIFIER op (transform family): cube-fold wrap of each point's
// position into a padded box centered at Center. Faithful port of TiXL's WrapPointPosition.
//
// DISTINCT from WrapPoints (floored-mod torus): WrapPointPosition uses an offsetFactor trick
// (each axis checks > padded -> flip once) while WrapPoints uses floored-modulo for true tiling.
// Reference: external/tixl/Operators/Lib/point/transform/WrapPointPosition.cs (slots) +
//            external/tixl/Operators/Lib/Assets/shaders/points/modify/WrapPointPosition.hlsl (math)
//
// TiXL ports:
//   GPoints (BufferWithViews): input bag -> c.inputs[0]
//   Position/Center (Vector3, default 0,0,0): box center
//   Size (Vector3, default 2,2,2): box extents
//   UseCameraPosition (bool): BAKED=0 (no camera matrix in cook ctx) -- NAMED FORK
//   AddLineBreaks (bool): BAKED=0 (W edge-fade path; line-break variant deferred) -- NAMED FORK
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"
#include "runtime/tex_op_cache.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"                    // calcDispatchCount
#include "runtime/graph.h"                       // Graph/Node/readVecN/pinId
#include "runtime/point_graph.h"                 // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"                  // SwPoint (64B)
#include "runtime/wrappointposition_params.h"    // WrapPointPositionParams, WrapPointPositionBinding

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

void cookWrapPointPosition(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "wrappointposition");
  if (!pso) return;

  WrapPointPositionParams P{};
  P.Count = c.count;

  float center[3] = {0.0f, 0.0f, 0.0f};
  float size[3]   = {2.0f, 2.0f, 2.0f};  // TiXL WrapPointPosition default Size=(2,2,2)
  cookVecN(c, "Position", center, 3, center);  // .cs slot name is "Position" (box center)
  cookVecN(c, "Size",     size,   3, size);
  P.CenterX = center[0]; P.CenterY = center[1]; P.CenterZ = center[2];
  P.SizeX   = size[0];   P.SizeY   = size[1];   P.SizeZ   = size[2];

  MTL::CommandBuffer*        cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, WRAPPOINTPOS_SourcePoints);
  enc->setBuffer(c.output,                         0, WRAPPOINTPOS_ResultPoints);
  enc->setBytes(&P, sizeof(P),                        WRAPPOINTPOS_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capWrapPos = nullptr;
void captureDrawWrapPos(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capWrapPos || !pts || c.count == 0) return;
  g_capWrapPos->assign(c.count, SwPoint{});
  std::memcpy(g_capWrapPos->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerWrapPointPositionOp() { registerPointOp("WrapPointPosition", cookWrapPointPosition); }

// Golden: tests the CUBE FOLD path — points just outside the padded boundary get folded once.
//
// WrapPointPosition is a ONE-SHOT fold (not recursive mod): it adds ±Size per axis for points
// that exceed the padded boundary (halfSize + Padding = halfSize * 1.1).
// With Size=(10,10,10), padded = 5 + 0.5*10*0.1... wait: Padding = Size.x * 0.1 = 1.0.
// padded = halfSize + Padding = 5.0 + 1.0 = 6.0.
//
// Strategy: place points just beyond padded (x = ±6.5), expect fold by ±Size = ±10 -> x=±(6.5-10)
// = ∓3.5. These OUTPUT positions land INSIDE padded (|3.5| < 6.0). Verify:
//   (1) count preserved.
//   (2) Points that started at x > padded (>6.0) got folded: x_out = x_in - 10 -> negative.
//   (3) Points that started at x < -padded (<-6.0) got folded: x_out = x_in + 10 -> positive.
//   (4) Points within padded are unchanged.
//   (5) Non-degenerate: spread > 0.
//
// We craft the input manually by building a 5-point set:
//   x = {-7.0, -3.0, 0.0, +3.0, +7.0}  (RadialPoints can't easily do this, use LinePoints trick)
// Simpler: LinePoints(Count=64, Length=14.0, along X) -> x in [-7, +7].
//   With Size=10 (padded=6.0): points at |x| > 6 get folded by ±10.
//   x = 7.0 -> 7.0 - 10 = -3.0 (wrapped to negative side)
//   x = -7.0 -> -7.0 + 10 = 3.0 (wrapped to positive side)
//   x = 6.5 -> 6.5 - 10 = -3.5 (wrapped)
//   x = 3.0 -> 3.0 (no fold; |3|<6)
//   All output |x| should be < 7.0 - 10 = ... wait: |folded x| = |7-10| = 3 < padded(6). PASS.
//   All output |x| <= max(6, 10-7) = max(6, 3) = 6. Actually max |output x| = max(padded_input, 10-maxinput)
//   Inputs in [-6, +6]: pass through unchanged -> max |x| = 6.0 = padded.
//   Inputs in (6, 7]: wrapped -> |x_out| = |x_in - 10| = 10 - x_in in [3, 4).
//   So max |output x| = 6.0 (from unfolded points).
//
// Assertions:
//   (1) count == N.
//   (2) max |output x| <= padded + eps (= 6.0 + 0.01 for Size=10).
//       For the normal run: 6.0. For injectBug (Size=100, padded=60): no fold -> max |x| = 7.0 > 6.0.
//       Wait: bug padded = 50 + 10 = 60 >> 7; no fold; max |x| = 7.0. But the assertion is
//       "must be <= 6.0 (nominal padded)" -> 7.0 > 6.0 -> FAIL.
//   (3) Some output points have x < 0 (wrapping of the right-side points).
//   (4) Some output points have x > 0 (no-fold left + wrapped right).
//
// injectBug: Size=100 -> padded=60 >> input extent -> no fold -> max |x| ~ 7.0 > nomPadded(6.0) -> FAIL.
int runWrapPointPositionSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N    = 64;
  // Line along X: x in [-7, +7]. With Size=10: padded=6.0.
  // Points at |x| > 6 get folded (about 6/7 of the way along each tail).
  const float    BSIZ = injectBug ? 100.0f : 10.0f;
  // nominal padded for Size=10: halfSize(5) + Padding(Size.x*0.1=1) = 6.0
  const float    nomPadded = 6.0f;

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device*      dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue*  q = dev->newCommandQueue();
  NS::Error*        err = nullptr;
  MTL::Library*     lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-wrappointposition] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerBuiltinPointOps();
  registerWrapPointPositionOp();
  std::vector<SwPoint> captured;
  g_capWrapPos = &captured;
  registerDrawOp("DrawPoints", captureDrawWrapPos);

  Graph g;
  Node gen; gen.id = 1; gen.type = "LinePoints";
  gen.params["Count"]       = (float)N;
  gen.params["Length"]      = 14.0f;           // x in [-7, +7] (spans beyond padded=6)
  gen.params["Direction.x"] = 1.0f;
  gen.params["Direction.y"] = 0.0f;
  gen.params["Direction.z"] = 0.0f;
  g.nodes.push_back(gen);

  Node wrap; wrap.id = 2; wrap.type = "WrapPointPosition";
  wrap.params["Position.x"] = 0.0f; wrap.params["Position.y"] = 0.0f; wrap.params["Position.z"] = 0.0f;
  wrap.params["Size.x"] = BSIZ; wrap.params["Size.y"] = BSIZ; wrap.params["Size.z"] = BSIZ;
  g.nodes.push_back(wrap);

  Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});

  PointGraph pg(dev, lib, q, 64, 64);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

  bool countOk = (captured.size() == N);

  // (2) After fold: no |output x| should exceed nomPadded (6.0) for the normal run.
  // Unfolded points at |x| in [0,6] stay. Folded points at |x| in (6,7] fold to |x| in [3,4).
  // Both <= 6.0. For the bug (Size=100, padded=60): no fold, max |x| = 7.0 > 6.0 -> FAIL.
  bool allWithinPadded = countOk && !captured.empty();
  float maxX = 0.0f;
  for (const SwPoint& p : captured) {
    float ax = std::fabs(p.Position.x);
    if (ax > maxX) maxX = ax;
    if (ax > nomPadded + 0.01f) allWithinPadded = false;
  }

  // (3+4) non-degenerate: should have both negative and positive x values
  bool hasNeg = false, hasPos = false;
  for (const SwPoint& p : captured) {
    if (p.Position.x < -0.1f) hasNeg = true;
    if (p.Position.x >  0.1f) hasPos = true;
  }
  bool nonDegenerate = hasNeg && hasPos;

  bool pass = countOk && allWithinPadded && nonDegenerate;
  printf("[selftest-wrappointposition] n=%zu maxAbsX=%.4f(need<=%.2f) allWithinPadded=%s nonDegen=%s -> %s\n",
         captured.size(), maxX, nomPadded, allWithinPadded ? "yes" : "NO",
         nonDegenerate ? "yes" : "NO", pass ? "PASS" : "FAIL");

  g_capWrapPos = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
