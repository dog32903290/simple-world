// PairPointsForGridWalkLines — COMBINE op (point_combine family): connects each StartPoints[i] to
// TargetPoints[i] (both cycled via modulo) with an 11-step grid-walk polyline (the 11th step is the
// NaN divider for DrawLines). Faithful port of TiXL PairPointsForGridWalkLines.
//
// Reference:
//   external/tixl/Operators/Lib/point/combine/PairPointsForGridWalkLines.cs (slots, defaults)
//   external/tixl/Operators/Lib/point/combine/PairPointsForGridWalkLines.t3   (cbuffer routing)
//   external/tixl/Operators/Lib/Assets/shaders/points/combine/PairPointsForGridWalkLine.hlsl (math)
//
// TiXL ports (from .cs InputSlots):
//   StartPoints   (BufferWithViews) -> c.inputs[0] (t0)
//   TargetPoints  (BufferWithViews) -> c.inputs[1] (t1)
//   GridSize      (Vector3, default 0.25,0.25,0.25)
//   GridOffset    (Vector3, default 0,0,0)
//   RandomizeGrid (Vector3, default 0,0,0)
//   StrokeLength  (float,   default 2.0)
//   Speed         (float,   default 0.5)
//   PhaseOffset   (float,   default 0.0)
//
// Count policy (.t3 BACKWARD-TRACE):
//   ResultCount(lines) = MaxInt(StartCount, TargetCount) = max(c.inputCounts[0], c.inputCounts[1])
//   Output count       = ResultCount * 11  (MultiplyInt B=11; numthreads(11) = 11 steps per line)
//   Realized via the file-static + countTransform pattern (same as PairPointsForLines): the cook
//   writes g_gridWalkResultCount = ResultCount*11 each call; the driver's countTransform reads it.
//
// cbuffer routing: the .t3 FloatsToBuffer connection order is 1:1 with the HLSL cbuffer (GridSize.xyz
// + pad, GridOffset.xyz + pad, RandomizeGrid.xyz + pad, StrokeLength, Speed, PhaseOffset). The only
// math nodes (MaxInt/MultiplyInt) feed COUNT, not the cbuffer — so op param -> cbuffer field is direct
// (verified, Cut-58 trap avoided).
//
// NAMED FORKS:
//   fork[resultcount]: TiXL's driver sets ResultCount = max(StartCount,TargetCount) externally; we
//     compute it in the cook fn and pass total = ResultCount*11 to the dispatch + count policy.
//   fork[dummy-buf]: when TargetPoints is unwired we bind StartPoints as a dummy to avoid Metal nil
//     slot validation; the shader cycles via `lineIndex % countB` with the real countB clamped >=1.
//   fork[counts-cbuffer]: HLSL reads buffer lengths via StructuredBuffer.GetDimensions(); MSL has no
//     such query for constant buffers, so the cook passes (totalCount, countA, countB) explicitly.
//
// Self-contained leaf: own cook + register fn + capture + golden.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include <simd/simd.h>

#include "runtime/dispatch.h"            // calcDispatchCount
#include "runtime/eval_context.h"        // EvaluationContext
#include "runtime/graph.h"               // Graph/Node/pinId
#include "runtime/point_graph.h"         // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"          // SwPoint (64B)
#include "runtime/pairpointsforgridwalklines_params.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

constexpr uint32_t kStepsPerPair = 11u;

// file-static result-count for the countTransform hook (single-threaded cook; same pattern as
// PairPointsForLines). Holds the desired OUTPUT count = max(A,B)*11.
static uint32_t g_gridWalkResultCount = 0;

uint32_t gridWalkCountTransform(uint32_t /*naturalCount*/) {
  return g_gridWalkResultCount;
}

void fillParams(PointCookCtx& c, PairPointsForGridWalkLinesParams& P) {
  float gridSize[3]  = {0.25f, 0.25f, 0.25f};
  float gridOffset[3] = {0.0f, 0.0f, 0.0f};
  float randGrid[3]  = {0.0f, 0.0f, 0.0f};
  cookVecN(c, "GridSize", gridSize, 3, gridSize);
  cookVecN(c, "GridOffset", gridOffset, 3, gridOffset);
  cookVecN(c, "RandomizeGrid", randGrid, 3, randGrid);
  P.GridSizeX = gridSize[0]; P.GridSizeY = gridSize[1]; P.GridSizeZ = gridSize[2];
  P.GridOffsetX = gridOffset[0]; P.GridOffsetY = gridOffset[1]; P.GridOffsetZ = gridOffset[2];
  P.RandomizeGridX = randGrid[0]; P.RandomizeGridY = randGrid[1]; P.RandomizeGridZ = randGrid[2];
  P.StrokeLength = cookParam(c, "StrokeLength", 2.0f);
  P.Speed        = cookParam(c, "Speed", 0.5f);
  P.PhaseOffset  = cookParam(c, "PhaseOffset", 0.0f);
}

void cookPairPointsForGridWalkLines(PointCookCtx& c) {
  if (!c.lib) return;

  const MTL::Buffer* startBuf  = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  const MTL::Buffer* targetBuf = (c.inputCount > 1) ? c.inputs[1] : nullptr;

  uint32_t countA = (c.inputCounts && c.inputCount > 0) ? c.inputCounts[0] : 0u;
  uint32_t countB = (c.inputCounts && c.inputCount > 1) ? c.inputCounts[1] : 0u;

  uint32_t resultCount = (countA > countB) ? countA : countB;  // max(A,B) lines
  g_gridWalkResultCount = resultCount * kStepsPerPair;

  if (resultCount == 0 || !startBuf || !c.output) return;

  // fork[dummy-buf]: bind StartPoints in TargetPoints' slot if unwired; clamp countB>=1 so the
  // shader's `lineIndex % countB` never divides by zero.
  const MTL::Buffer* targetBound = (targetBuf && countB > 0) ? targetBuf : startBuf;
  uint32_t safeA = (countA > 0) ? countA : 1u;
  uint32_t safeB = (countB > 0) ? countB : safeA;

  PairPointsForGridWalkLinesParams P{};
  fillParams(c, P);

  uint32_t totalOut = resultCount * kStepsPerPair;
  simd::uint3 counts = {totalOut, safeA, safeB};

  MTL::Function* fn = c.lib->newFunction(
      NS::String::string("pairpointsforgridwalklines", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  const uint32_t tg = 64;
  MTL::CommandBuffer*         cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(startBuf),    0, PAIRGRIDWALK_StartPoints);
  enc->setBuffer(const_cast<MTL::Buffer*>(targetBound), 0, PAIRGRIDWALK_TargetPoints);
  enc->setBuffer(c.output,                              0, PAIRGRIDWALK_Result);
  enc->setBytes(&P, sizeof(P),                             PAIRGRIDWALK_Params);
  enc->setBytes(&counts, sizeof(counts),                   PAIRGRIDWALK_Counts);
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(totalOut, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained) ---
std::vector<SwPoint>* g_capGridWalk = nullptr;
void captureDrawGridWalk(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capGridWalk || !pts || c.count == 0) return;
  g_capGridWalk->assign(c.count, SwPoint{});
  std::memcpy(g_capGridWalk->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerPairPointsForGridWalkLinesOp() {
  // countTransform: output = max(StartCount, TargetCount) * 11. The static g_gridWalkResultCount is
  // written by the cook fn on every cook (same pattern as PairPointsForLines / ParticleSystem pool).
  registerPointOp("PairPointsForGridWalkLines", cookPairPointsForGridWalkLines,
                  /*stateNew=*/nullptr, /*stateFree=*/nullptr,
                  gridWalkCountTransform,
                  /*countFromFirstPointsInput=*/false);
}

// Golden: drive the kernel DIRECTLY (no graph) over a controlled input and assert the structural
// invariants of TiXL's grid-walk emission that hold for EVERY hash/branch path:
//
//   StartPoints: N=3 points (x=i), TargetPoints: M=2 points (x=10+i). ResultCount=max(3,2)=3.
//   Expected output count = 3 * 11 = 33 points (3 lines, 11 steps each).
//
//   (1) COUNT LAW (TiXL MaxInt -> MultiplyInt B=11): totalOut == max(N,M) * 11 == 33.
//   (2) DIVIDER SENTINEL (TiXL line 183-189, UNCONDITIONAL `if(lineStepIndex==10) w=NaN`): every
//       step index i%11==10 -> scaleFactor=NaN -> Scale = 0.5*NaN -> Scale.x is NaN. Guaranteed
//       for EVERY line regardless of hash/positions/branch (it is set after all case logic).
//   (3) QUANTIZED SCALE (TiXL line 189): the kernel writes Scale = 0.5 * {1 or NaN} and NOTHING
//       else. So every output point has Scale.x either NaN or == 0.5 exactly. Covers all 33.
//
// injectBug: expect the WRONG count multiplier (max*3 instead of max*11) — emulating a port that
// mis-reads TiXL's MultiplyInt(B=11) as the PairPointsForLines triple. Real output is 33, the bugged
// expectation is 9 -> countOK FAILS (RED). 100% deterministic, branch-independent.
int runPairPointsForGridWalkLinesSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t NA = 3, NB = 2;
  const float offsetB = 10.0f;

  MTL::Device*       dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue*   q = dev->newCommandQueue();
  NS::Error*         err = nullptr;
  MTL::Library*      lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-pairpointsforgridwalklines] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const size_t szA = NA * sizeof(SwPoint);
  const size_t szB = NB * sizeof(SwPoint);
  MTL::Buffer* ptsA = dev->newBuffer(szA, MTL::ResourceStorageModeShared);
  MTL::Buffer* ptsB = dev->newBuffer(szB, MTL::ResourceStorageModeShared);
  auto* a = reinterpret_cast<SwPoint*>(ptsA->contents());
  auto* b = reinterpret_cast<SwPoint*>(ptsB->contents());
  for (uint32_t i = 0; i < NA; ++i) {
    a[i] = SwPoint{};
    a[i].Position.x = (float)i;
    a[i].FX1 = 0.5f;
  }
  for (uint32_t i = 0; i < NB; ++i) {
    b[i] = SwPoint{};
    b[i].Position.x = offsetB + (float)i;
    b[i].FX1 = 0.5f;
  }

  uint32_t resultCount = (NA > NB) ? NA : NB;     // 3
  uint32_t totalOut    = resultCount * kStepsPerPair;  // 33

  PairPointsForGridWalkLinesParams P{};
  P.GridSizeX = 0.25f; P.GridSizeY = 0.25f; P.GridSizeZ = 0.25f;
  P.StrokeLength = 2.0f;
  P.Speed = 0.5f;
  P.PhaseOffset = 0.0f;
  // GridOffset / RandomizeGrid default to zero (P{} zero-init).

  simd::uint3 counts = {totalOut, NA, NB};

  MTL::Function* fn = lib->newFunction(
      NS::String::string("pairpointsforgridwalklines", NS::UTF8StringEncoding));
  if (!fn) {
    printf("[selftest-pairpointsforgridwalklines] FAIL: no kernel\n");
    ptsA->release(); ptsB->release(); lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }
  NS::Error* psoErr = nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &psoErr);
  fn->release();
  if (!pso) {
    printf("[selftest-pairpointsforgridwalklines] FAIL: no PSO\n");
    ptsA->release(); ptsB->release(); lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::Buffer* outBuf = dev->newBuffer(totalOut * sizeof(SwPoint), MTL::ResourceStorageModeShared);

  MTL::CommandBuffer*         cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(ptsA,   0, PAIRGRIDWALK_StartPoints);
  enc->setBuffer(ptsB,   0, PAIRGRIDWALK_TargetPoints);
  enc->setBuffer(outBuf, 0, PAIRGRIDWALK_Result);
  enc->setBytes(&P, sizeof(P), PAIRGRIDWALK_Params);
  enc->setBytes(&counts, sizeof(counts), PAIRGRIDWALK_Counts);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make((totalOut + tg - 1) / tg, 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();

  auto* out = reinterpret_cast<SwPoint*>(outBuf->contents());

  // injectBug emulates a port that reads TiXL's MultiplyInt(B=11) as the PairPointsForLines triple.
  uint32_t expectMultiplier = injectBug ? 3u : kStepsPerPair;  // 3 (bug) vs 11 (real)
  uint32_t expectCount = resultCount * expectMultiplier;        // 9 (bug) vs 33 (real)

  bool countOK    = (totalOut == expectCount);  // (1) count law: max(3,2)*11 == 33
  bool dividerOK  = true;                        // (2) i%11==10 -> Scale.x NaN (unconditional)
  bool quantOK    = true;                        // (3) every Scale.x is NaN or 0.5

  for (uint32_t i = 0; i < totalOut; ++i) {
    uint32_t step = i % kStepsPerPair;
    float s = out[i].Scale.x;

    if (step == 10u) {
      if (!std::isnan(s)) dividerOK = false;  // (2) divider step must be NaN
    }
    // (3) quantized scale: NaN OR exactly 0.5 (kernel writes only 0.5 * {1, NaN})
    if (!std::isnan(s) && std::fabs(s - 0.5f) > 0.01f) quantOK = false;
  }

  bool pass = countOK && dividerOK && quantOK;
  printf("[selftest-pairpointsforgridwalklines] count=%u(need 33,%d) divider=%d quant=%d -> %s\n",
         totalOut, countOK ? 1 : 0, dividerOK ? 1 : 0, quantOK ? 1 : 0,
         pass ? "PASS" : "FAIL");

  outBuf->release();
  ptsA->release(); ptsB->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
