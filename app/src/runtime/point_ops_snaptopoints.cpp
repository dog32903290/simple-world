// SnapToPoints — batch 21 COMBINE op (point_combine family): index-paired lerp of Points1
// toward Points2 positions, blended by a smoothstep(Distance) curve scaled by MaxAmount.
// Faithful port of TiXL SnapToPoints.
//
// Reference:
//   external/tixl/Operators/Lib/point/transform/SnapToPoints.cs (slots)
//   external/tixl/Operators/Lib/Assets/shaders/points/modify/SnapToPoints.hlsl (math)
//
// TiXL ports ported (from .hlsl cbuffer b0):
//   Points1 (BufferWithViews) -> c.inputs[0]        // primary point bag
//   Points2 (BufferWithViews) -> c.inputs[1]        // snap-target bag (index-paired)
//   BlendFactor (float, default 0.0)                // smoothstep lower edge + W-lerp factor
//   Distance    (float, default 1.0)                // smoothstep upper edge (snap radius)
//   MaxAmount   (float, default 1.0)                // scale on blendFactor for Position
//
// NOTE: TiXL .cs uses BlendMode(int)+BlendValue(float) but .hlsl cbuffer only has
// BlendFactor/Distance/MaxAmount (the math source of truth).  We follow the .hlsl.
//
// TiXL kernel (SnapToPoints.hlsl lines 20-27), verbatim logic:
//   A          = Points1[i];
//   SnapPoint  = Points2[i];  // index-paired, NOT nearest-point search
//   distance   = length(A.Position - SnapPoint.Position);
//   blendFactor= smoothstep(BlendFactor + Distance, Distance, distance) * MaxAmount;
//   Result[i].Position = lerp(A.Position, SnapPoint.Position, blendFactor);
//   Result[i].W (FX1)  = lerp(A.W, SnapPoint.W, BlendFactor);  // W uses raw BlendFactor
//
// NAMED FORKS:
//   fork[count-guard]: TiXL .hlsl assumes Points1 and Points2 are equal length (no OOB
//     guard).  When Points2 has fewer elements than Points1 we clamp the Points2 index to
//     (Points2Count-1).  When Points2 is empty (Points2Count==0) the point passes through
//     unchanged.  See snaptopoints.metal for the GPU implementation.
//
//   fork[count-policy]: This op is index-paired (output count = Points1 count), but point_graph's
//     cook driver defaults to summing ALL Points input counts (the CombineBuffers concat contract).
//     SnapToPoints opts into OpReg.countFromFirstPointsInput=true (registerSnapToPointsOp below), so
//     the driver sizes the output to inputCounts[0] (Points1) only — Points2 is a snap TARGET, not
//     concatenated. Without the flag the driver would size to 2N, leaving N stale garbage points that
//     downstream DrawPoints would render. The flag was added to point_graph.cpp/_resident.cpp this
//     batch; locked by the -countpolicy graph golden (cooked==N, not 2N) below.
//
// Self-contained leaf: own capture + registerDrawOp.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"              // calcDispatchCount
#include "runtime/eval_context.h"          // EvaluationContext (graph count golden)
#include "runtime/graph.h"                 // Graph/Node/pinId
#include "runtime/point_graph.h"           // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"            // SwPoint (64B)
#include "runtime/snaptopoints_params.h"   // SnapToPointsParams, SnapToPointsBinding

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// cookSnapToPoints: index-paired lerp from Points1 toward Points2.
// c.inputs[0] = Points1 (primary bag, sets output semantic count)
// c.inputs[1] = Points2 (snap targets; may have fewer elements -> clamp in shader)
// c.count     = Points1 count (driver sizes output to inputCounts[0] via countFromFirstPointsInput)
// We dispatch inputCounts[0] threads so exactly the N output points are written.
void cookSnapToPoints(PointCookCtx& c) {
  if (!c.output || !c.lib) return;
  const MTL::Buffer* pts1 = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  const MTL::Buffer* pts2 = (c.inputCount > 1) ? c.inputs[1] : nullptr;
  if (!pts1) return;

  // Dispatch over Points1 count (inputCounts[0]); c.count already == Points1 count via the
  // driver's countFromFirstPointsInput policy, but read inputCounts[0] directly to be explicit.
  uint32_t pts1Count = (c.inputCounts && c.inputCount > 0) ? c.inputCounts[0] : c.count;
  uint32_t pts2Count = (c.inputCounts && c.inputCount > 1) ? c.inputCounts[1] : 0u;
  if (pts1Count == 0) return;

  MTL::Function* fn = c.lib->newFunction(
      NS::String::string("snaptopoints", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  SnapToPointsParams P{};
  P.Count        = pts1Count;
  P.Points2Count = pts2Count;
  P.BlendFactor  = cookParam(c, "BlendFactor", 0.0f);
  P.Distance     = cookParam(c, "Distance",    1.0f);
  P.MaxAmount    = cookParam(c, "MaxAmount",   1.0f);

  // Provide a dummy fallback buffer for Points2 when unwired: the shader guards on
  // Points2Count==0 so it will short-circuit before any reads from the Points2 slot.
  // We still need to bind SOMETHING to satisfy the Metal buffer slot (cannot leave it nil
  // without a validation error from the Metal debug layer).  Reuse the Points1 buffer as
  // the dummy — it is accessible and the shader will never read it when pts2Count==0.
  const MTL::Buffer* pts2Buf = pts2 ? pts2 : pts1;

  MTL::CommandBuffer*         cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(pts1),   0, SNAPTOPOINTS_Points1);
  enc->setBuffer(const_cast<MTL::Buffer*>(pts2Buf), 0, SNAPTOPOINTS_Points2);
  enc->setBuffer(c.output,                          0, SNAPTOPOINTS_Result);
  enc->setBytes(&P, sizeof(P),                         SNAPTOPOINTS_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(pts1Count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained) ---
std::vector<SwPoint>* g_capSnap2 = nullptr;
void captureDrawSnap2(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capSnap2 || !pts || c.count == 0) return;
  g_capSnap2->assign(c.count, SwPoint{});
  std::memcpy(g_capSnap2->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerSnapToPointsOp() {
  // countFromFirstPointsInput=true: output count = Points1 count (Points2 is a snap TARGET, not
  // concatenated). Without this the graph driver SUMS both Points inputs -> 2N output, leaving N
  // stale garbage points that downstream DrawPoints would render. TiXL SnapToPoints out == Points1.
  registerPointOp("SnapToPoints", cookSnapToPoints,
                  /*stateNew=*/nullptr, /*stateFree=*/nullptr, /*countTransform=*/nullptr,
                  /*countFromFirstPointsInput=*/true);
}

// Golden: Two bags of N=32 points.
//   Points1: a row of points at x = i * 1.0f, y=0, z=0.
//   Points2: Points1 shifted by offset (2.0f, 0, 0) — i.e. each point displaced +2 in X.
//   Params: MaxAmount=1.0, Distance=4.0 (>> offset 2.0), BlendFactor=0.0
//     -> smoothstep(0+4, 4, dist=2.0) = smoothstep(4, 4, 2) ... wait that's edge0==edge1.
//     Actually smoothstep(edge0=BlendFactor+Distance, edge1=Distance, x=dist):
//       edge0 = 0 + 4 = 4, edge1 = 4 — equal edges -> smoothstep undefined.
//     Use Distance=3.0, BlendFactor=0.0: edge0=3, edge1=3 — still degenerate.
//     Use Distance=4.0, BlendFactor=1.0: edge0=5.0, edge1=4.0.
//     At dist=2.0 < edge1=4.0 -> smoothstep returns 1.0 -> blendFactor = 1.0 * MaxAmount = 1.0
//     -> Position = lerp(x_i, x_i+2, 1.0) = x_i + 2 -> matches Points2.
//
//   Green: each output[i].Position.x should be near (i*1.0 + 2.0) = Points2[i].Position.x.
//          Tolerance < 0.1f.
//
//   injectBug: MaxAmount=0.0 -> blendFactor=0 -> no snap -> output = Points1.
//              output[i].Position.x = i*1.0 (unchanged), assert FAILS (Position differs from P2).
//
//   count-guard: we verify the fork with N1=32, N2=16 (Points2 shorter).  Points[16..31]
//     should clamp to Points2[15] for snap.  We check they don't crash and output is finite.
//     (Separate from the injectBug path.)
int runSnapToPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 32;
  const float offset = 2.0f;  // Points2 = Points1 shifted +offset in X

  MTL::Device*       dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue*   q = dev->newCommandQueue();
  NS::Error*         err = nullptr;
  MTL::Library*      lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-snaptopoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Build Points1 and Points2 buffers directly (hand-crafted; no generator needed).
  // Points1: x=i*1.0, y=0, z=0, all others zero.
  // Points2: x=i*1.0+offset, y=0, z=0.
  const size_t bufSz = N * sizeof(SwPoint);
  MTL::Buffer* pts1Buf = dev->newBuffer(bufSz, MTL::ResourceStorageModeShared);
  MTL::Buffer* pts2Buf = dev->newBuffer(bufSz, MTL::ResourceStorageModeShared);
  auto* pts1 = reinterpret_cast<SwPoint*>(pts1Buf->contents());
  auto* pts2 = reinterpret_cast<SwPoint*>(pts2Buf->contents());
  for (uint32_t i = 0; i < N; ++i) {
    pts1[i] = SwPoint{};
    pts1[i].Position.x = (float)i;
    pts1[i].Position.y = 0.0f;
    pts1[i].Position.z = 0.0f;
    pts1[i].FX1 = 0.5f;  // W channel

    pts2[i] = SwPoint{};
    pts2[i].Position.x = (float)i + offset;
    pts2[i].Position.y = 0.0f;
    pts2[i].Position.z = 0.0f;
    pts2[i].FX1 = 1.0f;  // W channel for Points2
  }

  // Build SnapToPoints directly (bypassing graph, like hand-crafted cook tests).
  // Params: BlendFactor=1.0, Distance=4.0, MaxAmount = 1.0 (normal) or 0.0 (bug).
  // At dist=offset=2.0: edge0=BF+D=1+4=5, edge1=D=4. dist=2 < edge1=4 -> step=1.0.
  // blendFactor = 1.0 * MaxAmount.
  // When MaxAmount=1: lerp(x_i, x_i+2, 1.0) = x_i+2.  (full snap)
  // When MaxAmount=0: blendFactor=0 -> no snap -> output = input.
  SnapToPointsParams P{};
  P.Count        = N;
  P.Points2Count = N;
  P.BlendFactor  = 1.0f;
  P.Distance     = 4.0f;
  P.MaxAmount    = injectBug ? 0.0f : 1.0f;

  MTL::Function* fn = lib->newFunction(NS::String::string("snaptopoints", NS::UTF8StringEncoding));
  if (!fn) {
    printf("[selftest-snaptopoints] FAIL: no kernel 'snaptopoints'\n");
    pts1Buf->release(); pts2Buf->release();
    lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }
  NS::Error* psoErr = nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &psoErr);
  fn->release();
  if (!pso) {
    printf("[selftest-snaptopoints] FAIL: no PSO\n");
    pts1Buf->release(); pts2Buf->release();
    lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::Buffer* outBuf = dev->newBuffer(bufSz, MTL::ResourceStorageModeShared);

  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(pts1Buf, 0, SNAPTOPOINTS_Points1);
  enc->setBuffer(pts2Buf, 0, SNAPTOPOINTS_Points2);
  enc->setBuffer(outBuf,  0, SNAPTOPOINTS_Result);
  enc->setBytes(&P, sizeof(P), SNAPTOPOINTS_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make((N + tg - 1) / tg, 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();

  auto* out = reinterpret_cast<SwPoint*>(outBuf->contents());

  // Verify:
  //   Normal (MaxAmount=1): output[i].Position.x ~= i + offset (snapped to Points2).
  //   injectBug (MaxAmount=0): output[i].Position.x ~= i (no snap, stays at Points1).
  //
  // In BOTH cases we assert "output is near Points2" (= i+offset).
  // Normal path: blendFactor=1 -> output = Points2 -> assertion PASSES.
  // injectBug path: blendFactor=0 -> output = Points1 (~= i) -> near Points2 FAILS -> RED.
  bool posOK = true;
  float maxErr = 0.0f;
  for (uint32_t i = 0; i < N; ++i) {
    float expectX = (float)i + offset;  // always assert "near Points2"
    float err = std::fabs(out[i].Position.x - expectX);
    if (err > maxErr) maxErr = err;
    if (err > 0.1f) posOK = false;
    // Sanity: output Y and Z should be ~0 always.
    if (!std::isfinite(out[i].Position.x) || !std::isfinite(out[i].Position.y) ||
        !std::isfinite(out[i].Position.z)) {
      posOK = false;
    }
  }
  // W channel: lerp(A.FX1=0.5, SnapPoint.FX1=1.0, BlendFactor=1.0) = 1.0
  // This holds in BOTH paths (W always lerps by raw BlendFactor=1.0, not by MaxAmount).
  float wErr = std::fabs(out[0].FX1 - 1.0f);
  bool wOK = wErr < 0.01f;

  // pass = posOK && wOK:
  //   Normal: posOK=true (snapped), wOK=true -> PASS (0)
  //   injectBug: posOK=false (no snap, not near Points2) -> FAIL (1) = RED
  bool pass = posOK && wOK;
  printf("[selftest-snaptopoints] maxPosErr=%.4f wErr=%.4f posOK=%s wOK=%s -> %s\n",
         maxErr, wErr, posOK ? "yes" : "NO", wOK ? "yes" : "NO", pass ? "PASS" : "FAIL");

  // --- count-guard sub-check (only in normal path; injectBug skips) ---
  if (!injectBug) {
    // Points2 shorter: N2 = N/2 = 16. Points[16..31] clamp to Points2[15].
    // Use Distance=20 (large) so smoothstep is 1 even for distant clamped points:
    //   for i=22, dist=|22-17|=5 < Distance=20 -> blendFactor=1 -> snaps fully to pts2[15].
    const uint32_t N2 = N / 2;
    SnapToPointsParams P2 = P;
    P2.Points2Count = N2;
    P2.Distance = 20.0f;   // large distance: ensures full snap for all i>=N2
    P2.BlendFactor = 1.0f; // edge0=21, edge1=20; dist < 20 -> blendFactor=1
    // Points2[15].Position.x = 15 + offset = 17.0
    float snapLast = (float)(N2 - 1) + offset;

    // Re-create PSO (pso was released above).
    MTL::Function* fn2 = lib->newFunction(NS::String::string("snaptopoints", NS::UTF8StringEncoding));
    MTL::ComputePipelineState* pso2 = fn2 ? dev->newComputePipelineState(fn2, &psoErr) : nullptr;
    if (fn2) fn2->release();
    if (!pso2) {
      printf("[selftest-snaptopoints-countguard] FAIL: no pso2\n");
      pass = false;
      goto guardDone;
    }

    {
    MTL::CommandBuffer*         cmd2 = q->commandBuffer();
    MTL::ComputeCommandEncoder* enc2 = cmd2->computeCommandEncoder();
    enc2->setComputePipelineState(pso2);
    enc2->setBuffer(pts1Buf, 0, SNAPTOPOINTS_Points1);
    enc2->setBuffer(pts2Buf, 0, SNAPTOPOINTS_Points2);  // pts2Buf still has N points but P2.Points2Count=N2
    enc2->setBuffer(outBuf,  0, SNAPTOPOINTS_Result);
    enc2->setBytes(&P2, sizeof(P2), SNAPTOPOINTS_Params);
    enc2->dispatchThreadgroups(MTL::Size::Make((N + tg - 1) / tg, 1, 1),
                               MTL::Size::Make(tg, 1, 1));
    enc2->endEncoding();
    cmd2->commit();
    cmd2->waitUntilCompleted();
    pso2->release();
    }

    guardDone:
    // Points[N2..N-1] should all snap to Points2[N2-1] (clamped last point of P2).
    // With Distance=20, blendFactor=1 for all (dist <= 14 < 20), so full snap to pts2[15].
    bool guardOK = true;
    for (uint32_t i = N2; i < N; ++i) {
      float err = std::fabs(out[i].Position.x - snapLast);
      if (err > 0.1f) guardOK = false;
      if (!std::isfinite(out[i].Position.x)) guardOK = false;
    }
    printf("[selftest-snaptopoints-countguard] snapLast=%.1f guardOK=%s\n",
           snapLast, guardOK ? "yes" : "NO");
    if (!guardOK) pass = false;
  }

  // --- graph count-policy golden (normal path only) -------------------------------------
  // Locks the driver fix (point_graph.cpp/_resident.cpp countFromFirstPointsInput): a SnapToPoints
  // node fed by TWO equal-N Points generators must cook to N points, NOT 2N. Without the flag the
  // driver SUMS both Points inputs -> 2N, leaving N stale garbage points that downstream DrawPoints
  // would render. RadialPoints(N) + RadialPoints(N) -> SnapToPoints, cook through PointGraph, assert
  // the node's cooked count == N. Regress (flag dropped) -> cooked=2N -> FAIL (permanent tooth).
  if (!injectBug) {
    registerBuiltinPointOps();
    Graph g;
    Node a; a.id = 1; a.type = "RadialPoints"; a.params["Count"] = (float)N; g.nodes.push_back(a);
    Node b; b.id = 2; b.type = "RadialPoints"; b.params["Count"] = (float)N; g.nodes.push_back(b);
    Node sp; sp.id = 3; sp.type = "SnapToPoints"; g.nodes.push_back(sp);
    g.connections.push_back({101, pinId(1, 0), pinId(3, 0)});  // A -> SnapToPoints Points1 (port 0)
    g.connections.push_back({102, pinId(2, 0), pinId(3, 1)});  // B -> SnapToPoints Points2 (port 1)
    PointGraph pg(dev, lib, q, 64, 64);
    EvaluationContext ctx;
    pg.cook(g, ctx, nullptr, 3);
    uint32_t cookedN = pg.debugCookedCount(3);
    bool countOK = (cookedN == N);
    printf("[selftest-snaptopoints-countpolicy] cooked=%u expect=%u (sum would be %u) -> %s\n",
           cookedN, N, 2u * N, countOK ? "PASS" : "FAIL");
    if (!countOK) pass = false;
  }

  outBuf->release();
  pts1Buf->release(); pts2Buf->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
