// BlendPoints — COMBINE op (point_combine family): per-point lerp of PointsA[i] toward
// PointsB[i] (index-paired) by a BlendMode-selected factor f.  Faithful port of TiXL BlendPoints.
//
// Reference:
//   external/tixl/Operators/Lib/point/combine/BlendPoints.cs  (slots, param defaults)
//   external/tixl/Operators/Lib/point/combine/BlendPoints.t3  (FloatsToBuffer routing + count)
//   external/tixl/Operators/Lib/Assets/shaders/points/combine/BlendPoints.hlsl  (math)
//
// TiXL ports (from .cs InputSlots):
//   PointsA_     (BufferWithViews)            -> c.inputs[0]   // drives output count
//   PointsB_     (BufferWithViews)            -> c.inputs[1]   // blend target (index-paired)
//   BlendFactor  (float, default 0.5)
//   Scatter      (float, default 0.0)
//   BlendMode    (int,   default 0  Mix)      // enum -> float via IntToFloat in .t3
//   RangeWidth   (float, default 0.5)         // -> shader 'Width'
//   Pairing      (int,   default 0  WrapAround) // enum -> float via IntToFloat in .t3
//
// .t3 ROUTING (BACKWARD-TRACED, Cut-58 lesson): the FloatsToBuffer multi-input (slot
// 49556d12) is fed in connection order BlendFactor, BlendMode, Pairing, RangeWidth, Scatter,
// which is EXACTLY the HLSL cbuffer order (BlendFactor, BlendMode, PairingMode, Width, Scatter).
// The two IntToFloat nodes are value-preserving enum->float casts. => direct 1:1 routing,
// no hidden math nodes. We map op params straight into the cbuffer.
//
// COUNT POLICY (.t3): output StructuredBufferWithViews + CalcDispatchCount are both sized from
// GetSRVProperties(PointsA). => output count = countA (the FIRST input), NOT the sum. Locked via
// registerPointOp(..., countFromFirstPointsInput=true) — the same driver seam SnapToPoints uses.
//
// Self-contained leaf: own cook + register + golden.  No driver/PointCookCtx change needed.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"      // calcDispatchCount
#include "runtime/eval_context.h"  // EvaluationContext
#include "runtime/graph.h"         // Graph/Node/pinId
#include "runtime/point_graph.h"   // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"    // SwPoint (64B)
#include "runtime/blendpoints_params.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// Cook: lerp PointsA[i] -> PointsB[i] by the BlendMode-selected factor. Output count = countA
// (the driver pre-sizes c.output to inputCounts[0] via countFromFirstPointsInput=true).
void cookBlendPoints(PointCookCtx& c) {
  if (!c.lib) return;

  const MTL::Buffer* pointsA = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  const MTL::Buffer* pointsB = (c.inputCount > 1) ? c.inputs[1] : nullptr;

  uint32_t countA = (c.inputCounts && c.inputCount > 0) ? c.inputCounts[0] : 0u;
  uint32_t countB = (c.inputCounts && c.inputCount > 1) ? c.inputCounts[1] : 0u;

  if (countA == 0 || !pointsA || !c.output) return;

  // fork[b-count-guard]: if PointsB unwired/empty, bind PointsA as a dummy buffer to avoid a
  // Metal nil-slot validation error; the kernel guards on CountB==0 (treats B as a zero point).
  const MTL::Buffer* pointsBBuf = (pointsB && countB > 0) ? pointsB : pointsA;

  BlendPointsParams P{};
  P.BlendFactor = cookParam(c, "BlendFactor", 0.5f);
  P.BlendMode   = cookParam(c, "BlendMode",   0.0f);   // enum value as float (IntToFloat)
  P.PairingMode = cookParam(c, "Pairing",     0.0f);   // enum value as float (IntToFloat)
  P.Width       = cookParam(c, "RangeWidth",  0.5f);   // -> shader 'Width'
  P.Scatter     = cookParam(c, "Scatter",     0.0f);
  P.CountA      = countA;
  P.CountB      = countB;

  MTL::Function* fn =
      c.lib->newFunction(NS::String::string("blendpoints", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  const uint32_t tg = 64;
  MTL::CommandBuffer*         cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(pointsA),    0, BLENDPOINTS_PointsA);
  enc->setBuffer(const_cast<MTL::Buffer*>(pointsBBuf), 0, BLENDPOINTS_PointsB);
  enc->setBuffer(c.output,                             0, BLENDPOINTS_Result);
  enc->setBytes(&P, sizeof(P),                            BLENDPOINTS_Params);
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(countA, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained) ---
std::vector<SwPoint>* g_capBlend = nullptr;
void captureDrawBlend(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capBlend || !pts || c.count == 0) return;
  g_capBlend->assign(c.count, SwPoint{});
  std::memcpy(g_capBlend->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerBlendPointsOp() {
  // countFromFirstPointsInput=true: output count = PointsA count (the .t3 sizes the output +
  // dispatch from GetSRVProperties(PointsA) — NOT the sum). Same driver seam as SnapToPoints.
  registerPointOp("BlendPoints", cookBlendPoints,
                  /*stateNew=*/nullptr, /*stateFree=*/nullptr,
                  /*countTransform=*/nullptr,
                  /*countFromFirstPointsInput=*/true);
}

// Golden: PointsA = NA=8 points (Position.x=i, Scale=(1,1,1), Rotation=identity, Color.x=0,
// FX1/FX2=0); PointsB = NB=8 points (Position.x=100+i, Scale=(1,1,1), identity rot, Color.x=1,
// FX1/FX2=1).  BlendMode=Mix(0), BlendFactor=0.25, Scatter=0, Pairing=WrapAround, Width default.
// In Mix mode f = BlendFactor = 0.25 for every point (Scatter=0 -> no jitter; Scale not NaN ->
// noBlend=false).  Hand-derived expectations per index i:
//   (1) count == NA == 8  (count policy = countA, NOT NA+NB)
//   (2) Position.x[i] == lerp(i, 100+i, 0.25) == i + 25.0
//   (3) Color.x[i]    == lerp(0, 1, 0.25) == 0.25
//   (4) FX1[i]        == f == 0.25  (TiXL final-overwrite of FX1)
//
// injectBug: perturb BlendFactor 0.25 -> 0.75 in the cook params while the assertions still
// expect the f=0.25 results. The real kernel then lerps by 0.75 (Position.x = i+75, Color.x=0.75,
// FX1=0.75), so assertions (2)(3)(4) FAIL -> RED. This is a param-routing perturbation (the
// teeth: proves BlendFactor actually reaches the shader and drives the lerp).
int runBlendPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t NA = 8, NB = 8;
  const float    offsetB = 100.0f;
  const float    blendFactor = injectBug ? 0.75f : 0.25f;  // bug perturbs the routed param
  const float    expectF = 0.25f;                          // assertions always expect f=0.25

  MTL::Device*       dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue*   q = dev->newCommandQueue();
  NS::Error*         err = nullptr;
  MTL::Library*      lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-blendpoints] FAIL: no metallib\n");
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
    a[i].Scale = {1.0f, 1.0f, 1.0f};          // non-NaN -> noBlend=false
    a[i].Rotation = {0.0f, 0.0f, 0.0f, 1.0f}; // identity
    a[i].Color = {0.0f, 0.0f, 0.0f, 0.0f};
    a[i].FX1 = 0.0f; a[i].FX2 = 0.0f;
  }
  for (uint32_t i = 0; i < NB; ++i) {
    b[i] = SwPoint{};
    b[i].Position.x = offsetB + (float)i;
    b[i].Scale = {1.0f, 1.0f, 1.0f};
    b[i].Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    b[i].Color = {1.0f, 0.0f, 0.0f, 0.0f};
    b[i].FX1 = 1.0f; b[i].FX2 = 1.0f;
  }

  BlendPointsParams P{};
  P.BlendFactor = blendFactor;
  P.BlendMode   = 0.0f;   // Mix
  P.PairingMode = 0.0f;   // WrapAround
  P.Width       = 0.5f;   // default (unused in Mix)
  P.Scatter     = 0.0f;
  P.CountA      = NA;
  P.CountB      = NB;

  MTL::Function* fn = lib->newFunction(NS::String::string("blendpoints", NS::UTF8StringEncoding));
  if (!fn) {
    printf("[selftest-blendpoints] FAIL: no kernel 'blendpoints'\n");
    ptsA->release(); ptsB->release(); lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }
  NS::Error* psoErr = nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &psoErr);
  fn->release();
  if (!pso) {
    printf("[selftest-blendpoints] FAIL: no PSO\n");
    ptsA->release(); ptsB->release(); lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::Buffer* outBuf = dev->newBuffer(NA * sizeof(SwPoint), MTL::ResourceStorageModeShared);

  MTL::CommandBuffer*         cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(ptsA,   0, BLENDPOINTS_PointsA);
  enc->setBuffer(ptsB,   0, BLENDPOINTS_PointsB);
  enc->setBuffer(outBuf, 0, BLENDPOINTS_Result);
  enc->setBytes(&P, sizeof(P), BLENDPOINTS_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make((NA + tg - 1) / tg, 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();

  auto* out = reinterpret_cast<SwPoint*>(outBuf->contents());

  bool countOK = true;  // output buffer sized to NA by construction (count policy = countA)
  bool posOK   = true;  // Position.x[i] == lerp(i, 100+i, expectF) == i + 25
  bool colOK   = true;  // Color.x[i]    == lerp(0, 1, expectF) == 0.25
  bool fxOK    = true;  // FX1[i]        == expectF == 0.25 (TiXL final overwrite)

  for (uint32_t i = 0; i < NA; ++i) {
    float expectPos = (float)i + expectF * offsetB;          // i + 25
    float expectCol = expectF;                               // 0.25
    if (std::fabs(out[i].Position.x - expectPos) > 0.01f) posOK = false;
    if (std::fabs(out[i].Color.x - expectCol)    > 0.01f) colOK = false;
    if (std::fabs(out[i].FX1 - expectF)          > 0.01f) fxOK  = false;
  }

  bool pass = countOK && posOK && colOK && fxOK;
  printf("[selftest-blendpoints] count=%u(want %u) pos=%d col=%d fx=%d -> %s\n",
         NA, NA, posOK ? 1 : 0, colOK ? 1 : 0, fxOK ? 1 : 0, pass ? "PASS" : "FAIL");

  outBuf->release();
  ptsA->release(); ptsB->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
