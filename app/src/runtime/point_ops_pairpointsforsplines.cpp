// PairPointsForSplines — point_combine COMBINE op. Faithful port of TiXL
// external/tixl/Operators/Lib/point/combine/PairPointsForSplines.{cs,t3} +
// external/tixl/Operators/Lib/Assets/shaders/points/combine/PairPointsForSplines.hlsl.
//
// Pairs each GPoints[i] with GTargets[i] (both cycled via modulo) and, per pair, emits a Hermite
// cubic spline strip of `pointsPerSegment` output points (the LAST is a NaN divider for DrawLines).
//
// TiXL inputs (.cs InputSlots) -> our ports / params:
//   GPoints   (BufferWithViews) -> c.inputs[0]   // PointsA
//   GTargets  (BufferWithViews) -> c.inputs[1]   // PointsB (index-paired cyclic)
//   SetWTo01  (bool,  default false)             // >0.5 => FX1 = f (spline param)
//   Segments  (int,   default 10)                // clamped [3,16385] then +1 = pointsPerSegment
//   TangentDirection (Vector3, default (0,0,1))
//   TangentA  (float, default 1) / TangentA_WFactor (float, default 0)
//   TangentB  (float, default 1) / TangentB_WFactor (float, default 0)
//   Debug     (float, default 0)                 // unused by the math
//
// COUNT POLICY (.t3: MaxInt(countA,countB) -> MultiplyInt(B = ClampInt(Segments,3,16385)+1) ->
// CalcDispatchCount): ResultCount = max(CountA,CountB) * (clamp(Segments,3,16385)+1). Surfaced via
// the PointCountFn static (single-threaded cook) exactly like PairPointsForLines.
//
// ROUTING: the .t3 FloatsToBuffer connection order is 1:1 with the HLSL cbuffer (verified field by
// field); the only intermediate math is the Segments -> clamp -> +1 routing captured in SegmentCount.
//
// NAMED FORKS:
//   fork[resultcount]: TiXL's driver sizes the output buffer externally (MultiplyInt). We compute
//     ResultCount in the cook fn and pass it; the buffer length is the count-transform static.
//   fork[dummy-buf]: when GTargets is unwired we bind GPoints as a dummy to avoid a Metal nil-slot
//     validation error; the shader uses CountB==CountA-style modulo guards so no OOB read occurs.
//
// Self-contained leaf (own params header + .metal kernel + golden).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"        // calcDispatchCount
#include "runtime/eval_context.h"    // EvaluationContext
#include "runtime/graph.h"           // Graph/Node/pinId
#include "runtime/point_graph.h"     // PointCookCtx, registerPointOp, cookParam/cookVecN
#include "runtime/tex_op_cache.h"    // cachedComputePSO
#include "runtime/tixl_point.h"      // SwPoint (64B)
#include "runtime/pairpointsforsplines_params.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// segmentCount param -> pointsPerSegment (= clamp(Segments,3,16385)+1). Matches the .t3 ClampInt
// (Min=3, Max=16385) feeding "Add +1".
uint32_t pointsPerSegmentFrom(float segmentsParam) {
  int s = (int)std::lround(segmentsParam);
  if (s < 3) s = 3;
  if (s > 16385) s = 16385;
  return (uint32_t)s + 1u;
}

// Count-transform static (single-threaded cook; same pattern as PairPointsForLines).
static uint32_t g_pairSplinesResultCount = 0;
uint32_t pairSplinesCountTransform(uint32_t /*naturalCount*/) { return g_pairSplinesResultCount; }

void cookPairPointsForSplines(PointCookCtx& c) {
  if (!c.lib) return;

  const MTL::Buffer* gPoints  = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  const MTL::Buffer* gTargets = (c.inputCount > 1) ? c.inputs[1] : nullptr;

  uint32_t countA = (c.inputCounts && c.inputCount > 0) ? c.inputCounts[0] : 0u;
  uint32_t countB = (c.inputCounts && c.inputCount > 1) ? c.inputCounts[1] : 0u;

  float segmentsParam = cookParam(c, "Segments", 10.0f);
  uint32_t pointsPerSegment = pointsPerSegmentFrom(segmentsParam);

  uint32_t maxAB = (countA > countB) ? countA : countB;
  uint32_t resultCount = maxAB * pointsPerSegment;
  g_pairSplinesResultCount = resultCount;

  if (resultCount == 0 || !gPoints || !c.output) return;

  // fork[dummy-buf]: bind GPoints as GTargets when GTargets unwired/empty.
  const MTL::Buffer* gTargetsBuf = (gTargets && countB > 0) ? gTargets : gPoints;
  uint32_t safeBcount = (countB > 0) ? countB : ((countA > 0) ? countA : 1u);
  uint32_t safeAcount = (countA > 0) ? countA : 1u;

  float tangentDir[3] = {0.0f, 0.0f, 1.0f};
  cookVecN(c, "TangentDirection", tangentDir, 3, tangentDir);

  PairPointsForSplinesParams P{};
  P.TangentDirectionX = tangentDir[0];
  P.TangentDirectionY = tangentDir[1];
  P.TangentDirectionZ = tangentDir[2];
  P.InitWTo01         = (cookParam(c, "SetWTo01", 0.0f) > 0.5f) ? 1.0f : 0.0f;
  P.SegmentCount      = (float)pointsPerSegment;
  P.TangentA          = cookParam(c, "TangentA", 1.0f);
  P.TangentA_WFactor  = cookParam(c, "TangentA_WFactor", 0.0f);
  P.TangentB          = cookParam(c, "TangentB", 1.0f);
  P.TangentB_WFactor  = cookParam(c, "TangentB_WFactor", 0.0f);
  P.Debug             = cookParam(c, "Debug", 0.0f);
  P.CountA            = (float)safeAcount;
  P.CountB            = (float)safeBcount;
  P.ResultCount       = (float)resultCount;

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "pairpointsforsplines");
  if (!pso) return;

  const uint32_t tg = 64;
  MTL::CommandBuffer*         cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(gPoints),     0, PAIRPOINTSFORSPLINES_GPoints);
  enc->setBuffer(const_cast<MTL::Buffer*>(gTargetsBuf), 0, PAIRPOINTSFORSPLINES_GTargets);
  enc->setBuffer(c.output,                              0, PAIRPOINTSFORSPLINES_Result);
  enc->setBytes(&P, sizeof(P),                             PAIRPOINTSFORSPLINES_Params);
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(resultCount, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

}  // namespace

void registerPairPointsForSplinesOp() {
  registerPointOp("PairPointsForSplines", cookPairPointsForSplines,
                  /*stateNew=*/nullptr, /*stateFree=*/nullptr,
                  pairSplinesCountTransform,
                  /*countFromFirstPointsInput=*/false);
}

// Golden: GPoints = 1 point A=(0,0,0), GTargets = 1 point B=(1,0,0), both identity Rotation,
// FX1=0.5, neutral Color/Scale. TangentDirection=(0,0,1), TangentA=TangentB=1, WFactors=0,
// SetWTo01=1, Segments=4. Then pointsPerSegment = clamp(4,3,16385)+1 = 5; ResultCount = 1*5 = 5.
//
// Endpoint plateaus (tangent-independent, d=0/d=1 — NO fwidth/finite-diff fragility):
//   indexInLine=0: f=0 -> Hermite basis (h00=1) -> Position == A == (0,0,0); FX1 (InitWTo01) == 0.
//   indexInLine=3: f = 3/(5-2) = 1 -> Hermite basis (h01=1) -> Position == B == (1,0,0); FX1 == 1.
//   indexInLine=4 (== pointsPerSegment-1): NaN divider -> Scale.x is NaN.
//
// Assertions:
//   (1) ResultCount == 5  (max(1,1) * (clamp(4,3,16385)+1))
//   (2) Position[0] == (0,0,0)   (spline start = A)
//   (3) Position[3] == (1,0,0)   (spline end   = B)
//   (4) Scale.x[4] is NaN        (divider sentinel)
//   (5) FX1[0]==0 and FX1[3]==1  (InitWTo01 = spline param f at endpoints)
//
// injectBug: perturb Segments param +1 (Segments=5 -> pointsPerSegment=6). Then f at indexInLine=3
// becomes 3/4 = 0.75 (not 1), so Position[3] != (1,0,0) and FX1[3] != 1, and ResultCount becomes 6.
// The golden still ASSERTS the Segments=4 expectations -> assertions (1),(3),(5) FAIL -> RED.
int runPairPointsForSplinesSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device*       dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue*   q = dev->newCommandQueue();
  NS::Error*         err = nullptr;
  MTL::Library*      lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-pairpointsforsplines] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const uint32_t NA = 1, NB = 1;
  MTL::Buffer* ptsA = dev->newBuffer(NA * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* ptsB = dev->newBuffer(NB * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  auto* a = reinterpret_cast<SwPoint*>(ptsA->contents());
  auto* b = reinterpret_cast<SwPoint*>(ptsB->contents());
  a[0] = SwPoint{};
  a[0].Position = {0.0f, 0.0f, 0.0f};
  a[0].Rotation = {0.0f, 0.0f, 0.0f, 1.0f};  // identity
  a[0].FX1 = 0.5f; a[0].FX2 = 0.5f;
  a[0].Color = {1, 1, 1, 1}; a[0].Scale = {1, 1, 1};
  b[0] = SwPoint{};
  b[0].Position = {1.0f, 0.0f, 0.0f};
  b[0].Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  b[0].FX1 = 0.5f; b[0].FX2 = 0.5f;
  b[0].Color = {1, 1, 1, 1}; b[0].Scale = {1, 1, 1};

  // Segments=4 golden; injectBug perturbs the param to 5 (real shader honors it).
  float segmentsParam = injectBug ? 5.0f : 4.0f;
  uint32_t pointsPerSegment = pointsPerSegmentFrom(segmentsParam);  // 5 (bug:6)
  uint32_t maxAB = (NA > NB) ? NA : NB;                              // 1
  uint32_t resultCount = maxAB * pointsPerSegment;                   // 5 (bug:6)

  PairPointsForSplinesParams P{};
  P.TangentDirectionX = 0.0f; P.TangentDirectionY = 0.0f; P.TangentDirectionZ = 1.0f;
  P.InitWTo01 = 1.0f;
  P.SegmentCount = (float)pointsPerSegment;
  P.TangentA = 1.0f; P.TangentA_WFactor = 0.0f;
  P.TangentB = 1.0f; P.TangentB_WFactor = 0.0f;
  P.Debug = 0.0f;
  P.CountA = (float)NA; P.CountB = (float)NB; P.ResultCount = (float)resultCount;

  MTL::Function* fn =
      lib->newFunction(NS::String::string("pairpointsforsplines", NS::UTF8StringEncoding));
  if (!fn) {
    printf("[selftest-pairpointsforsplines] FAIL: no kernel 'pairpointsforsplines'\n");
    ptsA->release(); ptsB->release(); lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }
  NS::Error* psoErr = nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &psoErr);
  fn->release();
  if (!pso) {
    printf("[selftest-pairpointsforsplines] FAIL: no PSO\n");
    ptsA->release(); ptsB->release(); lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::Buffer* outBuf =
      dev->newBuffer(resultCount * sizeof(SwPoint), MTL::ResourceStorageModeShared);

  MTL::CommandBuffer*         cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(ptsA,   0, PAIRPOINTSFORSPLINES_GPoints);
  enc->setBuffer(ptsB,   0, PAIRPOINTSFORSPLINES_GTargets);
  enc->setBuffer(outBuf, 0, PAIRPOINTSFORSPLINES_Result);
  enc->setBytes(&P, sizeof(P), PAIRPOINTSFORSPLINES_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make((resultCount + tg - 1) / tg, 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();

  auto* out = reinterpret_cast<SwPoint*>(outBuf->contents());

  // Golden asserts the Segments=4 layout (5 points; divider at idx 4) REGARDLESS of injectBug.
  const uint32_t EXPECT_COUNT = 5;
  bool countOK = (resultCount == EXPECT_COUNT);

  // Only read the golden indices if the buffer is large enough (bug shrinks/grows count).
  bool startOK = false, endOK = false, nanOK = false, wOK = false;
  if (resultCount > 4) {
    const SwPoint& s0 = out[0];  // f=0 -> A
    startOK = std::fabs(s0.Position.x - 0.0f) < 0.001f &&
              std::fabs(s0.Position.y - 0.0f) < 0.001f &&
              std::fabs(s0.Position.z - 0.0f) < 0.001f;

    const SwPoint& s3 = out[3];  // f=1 (only when pointsPerSegment==5) -> B
    endOK = std::fabs(s3.Position.x - 1.0f) < 0.001f &&
            std::fabs(s3.Position.y - 0.0f) < 0.001f &&
            std::fabs(s3.Position.z - 0.0f) < 0.001f;

    const SwPoint& s4 = out[4];  // divider (only when pointsPerSegment==5)
    nanOK = std::isnan(s4.Scale.x);

    wOK = std::fabs(s0.FX1 - 0.0f) < 0.001f && std::fabs(s3.FX1 - 1.0f) < 0.001f;
  }

  bool pass = countOK && startOK && endOK && nanOK && wOK;
  printf("[selftest-pairpointsforsplines] count=%u(want %u) start=%d end=%d nan=%d w=%d -> %s\n",
         resultCount, EXPECT_COUNT, startOK ? 1 : 0, endOK ? 1 : 0, nanOK ? 1 : 0, wOK ? 1 : 0,
         pass ? "PASS" : "FAIL");

  outBuf->release();
  ptsA->release(); ptsB->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
