// ResampleLinePoints — batch 36 lane point_modify COUNT-CHANGING MODIFIER op.
// Faithful port of external/tixl .../point/modify/ResampleLinePoints (.cs ports, .hlsl math).
// Reads the input line bag (c.inputs[0], SourceCount = c.inputCounts[0]) and writes a fresh bag of
// `Count` points sampled along the source list's NORMALIZED PARAMETER f in [0,1] (TiXL samples by
// linear index parameter, NOT true arc-length — see SamplePosAtF in the .hlsl). Each output is a
// SmoothDistance-weighted average over (1 + 2*Samples) parameter taps; SEPARATOR points (NaN Scale)
// break the line into segments (a tap straddling a separator is dropped from the average).
//
// COUNT POLICY:
//   Output count = the Count port (clamped 1..100000 per ResampleLinePoints.t3 ClampInt Max=100000,
//   Min=1).  countFromFirstPointsInput=false -> the Count Float port natively drives c.count
//   (PointGraph::nodeCount); the input bag count is read separately via c.inputCounts[0].
//   This is the FIRST point-modify op whose output count is Count-driven while ALSO consuming an
//   input bag (RepetitionPoints is Count-driven but has no input; ReorientLinePoints consumes a bag
//   but is count-preserving).
//
// TiXL parity (ResampleLinePoints.cs / .hlsl):
//   - ports ([Input] order): Points, Count, RangeMode(SampleModes), SampleRange(Vec2),
//     SmoothDistance, Samples, Rotation(RotationModes), RotationUpVector(Vec3).  EVERY port is
//     read by main() or its samplers — no dead port dropped (contrast ReorientLinePoints).
//   - math: per-output f sweep, SamplePosAtF smoothing average, RotationMode Interpolate(qSlerp) /
//           Recompute(qLookAt of the central-difference tangent).  position AND attributes
//           (FX1/FX2/Color/Scale) are all sample-averaged (see resamplelinepoints.metal).
//   - FORK (see resamplelinepoints.metal): OOB +1 read clamped to SourceCount-1 (HLSL OOB returns 0).
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"                    // calcDispatchCount
#include "runtime/graph.h"                       // Graph/Node/pinId/readVecN
#include "runtime/point_graph.h"                 // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/resamplelinepoints_params.h"   // ResampleLineParams, ResampleLineBinding
#include "runtime/tex_op_cache.h"                // cachedComputePSO
#include "runtime/tixl_point.h"                  // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// Output count = Count port clamped to TiXL's 1..100000 (ResampleLinePoints.t3 ClampInt Min=1,
// Max=100000). countFromFirstPointsInput=false -> `natural` is the resolved Count param.
uint32_t resampleCountTransform(uint32_t natural) {
  uint32_t count = natural;
  if (count < 1u) count = 1u;
  if (count > 100000u) count = 100000u;
  return count;
}

// ResampleLinePoints modifier: dispatch over ResultCount; read SourceCount from inputCounts[0].
void cookResampleLinePoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!srcBag) return;  // unwired input -> nothing to resample
  uint32_t sourceCount = (c.inputCounts && c.inputCount > 0) ? c.inputCounts[0] : 0u;
  if (sourceCount < 2u) return;  // need >=2 source points to define a line parameter

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "resamplelinepoints");
  if (!pso) return;

  ResampleLineParams P{};
  P.SmoothDistance = cookParam(c, "SmoothDistance", 0.5f);
  float range[2] = {0.0f, 1.0f};
  cookVecN(c, "SampleRange", range, 2, range);
  P.SampleRangeX = range[0];
  P.SampleRangeY = range[1];
  float up[3] = {0.0f, 0.0f, 1.0f};  // RotationUpVector default (0,0,1) per .t3
  cookVecN(c, "RotationUpVector", up, 3, up);
  P.UpVectorX = up[0]; P.UpVectorY = up[1]; P.UpVectorZ = up[2];

  P.SourceCount  = sourceCount;
  P.ResultCount  = c.count;  // already clamped via resampleCountTransform
  P.SampleMode   = (int)(cookParam(c, "RangeMode", 0.0f) + 0.5f);
  // Samples clamped 1..10 (ResampleLinePoints.t3 ClampInt Min=1 Max=10, .hlsl comment line 92).
  int samples = (int)(cookParam(c, "Samples", 3.0f) + 0.5f);
  if (samples < 1) samples = 1;
  if (samples > 10) samples = 10;
  P.SampleCount  = samples;
  P.RotationMode = (int)(cookParam(c, "Rotation", 0.0f) + 0.5f);

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, RESAMPLELINE_SourcePoints);
  enc->setBuffer(c.output, 0, RESAMPLELINE_ResultPoints);
  enc->setBytes(&P, sizeof(P), RESAMPLELINE_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capResample = nullptr;
void captureDrawResample(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capResample || !pts || c.count == 0) return;
  g_capResample->assign(c.count, SwPoint{});
  std::memcpy(g_capResample->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

// Direct single-dispatch runner for precise teeth (no graph plumbing).
bool runResampleKernelDirect(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                             const std::vector<SwPoint>& in, const ResampleLineParams& Pin,
                             std::vector<SwPoint>& out) {
  MTL::Function* fn =
      lib->newFunction(NS::String::string("resamplelinepoints", NS::UTF8StringEncoding));
  if (!fn) return false;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return false;
  ResampleLineParams P = Pin;
  P.SourceCount = (uint32_t)in.size();
  const size_t inBytes  = in.size() * sizeof(SwPoint);
  const size_t outBytes = (size_t)P.ResultCount * sizeof(SwPoint);
  MTL::Buffer* src = dev->newBuffer(in.data(), inBytes, MTL::ResourceStorageModeShared);
  MTL::Buffer* dst = dev->newBuffer(outBytes, MTL::ResourceStorageModeShared);
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(src, 0, RESAMPLELINE_SourcePoints);
  enc->setBuffer(dst, 0, RESAMPLELINE_ResultPoints);
  enc->setBytes(&P, sizeof(P), RESAMPLELINE_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(P.ResultCount, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding(); cmd->commit(); cmd->waitUntilCompleted();
  out.assign(P.ResultCount, SwPoint{});
  std::memcpy(out.data(), dst->contents(), outBytes);
  src->release(); dst->release(); pso->release();
  return true;
}

// Build a live source line of N points along +X (x=0..N-1), identity rotation, finite scale.
SwPoint makeLinePoint(float x) {
  SwPoint p{};
  p.Position = SW_PACKED3{x, 0.0f, 0.0f};
  p.Rotation = SW_FLOAT4{0.0f, 0.0f, 0.0f, 1.0f};
  p.Color    = SW_FLOAT4{1.0f, 1.0f, 1.0f, 1.0f};
  p.Scale    = SW_PACKED3{1.0f, 1.0f, 1.0f};
  p.FX1 = 0.0f; p.FX2 = 0.0f;
  return p;
}

}  // namespace

void registerResampleLinePointsOp() {
  registerPointOp("ResampleLinePoints", cookResampleLinePoints,
                  /*stateNew=*/nullptr, /*stateFree=*/nullptr,
                  resampleCountTransform,
                  /*countFromFirstPointsInput=*/false);
}

// =============================================================================
// Golden — ResampleLinePoints on a known +X line, asserted through the real kernel.
//
//   SOURCE: 5 points along +X at x = 0,1,2,3,4 (SourceCount=5, uniform spacing).
//   Defaults: RangeMode=StartEnd(0), SampleRange=(0,1), SmoothDistance=0.5, Samples=3,
//             Rotation=Interpolate(0), UpVector=(0,0,1).
//
//   CASE A (uniform resample, hand-computable): Count=5 ->
//     fNormlized = i/5, f = i/5 -> sourceF = f*4 = {0, 0.8, 1.6, 2.4, 3.2}.
//     Source x == index, so the CENTRE-tap x == sourceF. The smoothing taps at f±k·d are symmetric
//     and position is linear in sourceF, so for INTERIOR points the average == centre.  Output x is
//     therefore monotonically increasing and ~= 0.8·i (endpoints clip slightly under saturate()).
//     TEETH: (count == 5), (x monotonic increasing), (interior x[2] ~= 1.6, x[3] ~= 2.4 within
//     a smoothing tolerance), (all output Scale finite — no point fell in a dead region).
//
//   CASE B (count-change): same source, Count=11 -> output bag has 11 points (count is Count-driven,
//     NOT inherited from the 5-point source).  TEETH: size == 11, x monotonic, first < last.
//
//   CASE C (separator preservation): source = [x=0, x=1, SEPARATOR(NaN), x=3, x=4] (SourceCount=5).
//     The separator at index 2 splits the line.  Resample Count=5.  Output points whose smoothing
//     window straddles index 2 lose those taps; a point that lands ENTIRELY inside the separator
//     gets sampledCount==0 -> NaN Scale (dead).  TEETH: at least one output point is dead (NaN
//     Scale) AND at least one is alive (finite) — the separator carved a hole, the line survived
//     on both sides.
//
//   injectBug: re-runs CASE A but flips the smoothing step sign in a HOST recompute used as the
//     expectation — no; instead it asserts the WRONG resample law (output x EQUALS the source index
//     i, i.e. no /4 parameter scaling -> x[2] would be 2.0 not 1.6).  The real kernel scales by
//     (SourceCount-1)=4, so x[2] ~= 1.6; asserting 2.0 -> FAIL.  A real parity flip (the sourceF =
//     saturate(f)*(SourceCount-1) scaling, .hlsl:37), not an inverted assert.
// =============================================================================
int runResampleLinePointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-resamplelinepoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  bool pass = true;

  // 5-point +X source line, x = 0..4.
  std::vector<SwPoint> line5;
  for (int j = 0; j < 5; ++j) line5.push_back(makeLinePoint((float)j));

  // ===== CASE A: uniform resample Count=5 =====
  {
    ResampleLineParams P{};
    P.SmoothDistance = 0.5f; P.SampleRangeX = 0.0f; P.SampleRangeY = 1.0f;
    P.UpVectorX = 0.0f; P.UpVectorY = 0.0f; P.UpVectorZ = 1.0f;
    P.ResultCount = 5; P.SampleMode = 0; P.SampleCount = 3; P.RotationMode = 0;
    std::vector<SwPoint> out;
    bool ran = runResampleKernelDirect(dev, q, lib, line5, P, out);
    bool countOk = ran && out.size() == 5;
    bool monoOk = countOk, finiteOk = countOk;
    for (size_t k = 0; countOk && k < out.size(); ++k) {
      if (!std::isfinite(out[k].Scale.x)) finiteOk = false;
      if (k > 0 && out[k].Position.x <= out[k - 1].Position.x) monoOk = false;
    }
    // interior parity: x[2] ~= 1.6, x[3] ~= 2.4 (sourceF = f*4). injectBug asserts the no-scaling
    // law x[2] == 2.0 (would hold if scaling were *(SourceCount) or none) -> RED.
    float ex2 = injectBug ? 2.0f : 1.6f;
    float ex3 = injectBug ? 3.0f : 2.4f;
    bool interiorOk = countOk &&
        std::fabs(out[2].Position.x - ex2) < 0.15f &&
        std::fabs(out[3].Position.x - ex3) < 0.15f;
    printf("[selftest-resamplelinepoints] A count=%zu mono=%s finite=%s "
           "x[2]=%.3f x[3]=%.3f interior=%s\n",
           ran ? out.size() : 0, monoOk ? "ok" : "NO", finiteOk ? "ok" : "NO",
           countOk ? out[2].Position.x : 0.0f, countOk ? out[3].Position.x : 0.0f,
           interiorOk ? "ok" : "NO");
    pass = pass && countOk && monoOk && finiteOk && interiorOk;
  }

  // ===== CASE B: count-change Count=11 (output != source count) =====
  {
    ResampleLineParams P{};
    P.SmoothDistance = 0.5f; P.SampleRangeX = 0.0f; P.SampleRangeY = 1.0f;
    P.UpVectorX = 0.0f; P.UpVectorY = 0.0f; P.UpVectorZ = 1.0f;
    P.ResultCount = 11; P.SampleMode = 0; P.SampleCount = 3; P.RotationMode = 0;
    std::vector<SwPoint> out;
    bool ran = runResampleKernelDirect(dev, q, lib, line5, P, out);
    bool sizeOk = ran && out.size() == 11;
    bool monoOk = sizeOk, growOk = sizeOk;
    for (size_t k = 1; sizeOk && k < out.size(); ++k)
      if (out[k].Position.x <= out[k - 1].Position.x) monoOk = false;
    if (sizeOk) growOk = out.back().Position.x > out.front().Position.x;
    printf("[selftest-resamplelinepoints] B size=%zu mono=%s grow=%s (source was 5 -> Count-driven)\n",
           ran ? out.size() : 0, monoOk ? "ok" : "NO", growOk ? "ok" : "NO");
    pass = pass && sizeOk && monoOk && growOk;
  }

  // ===== CASE C: separator preservation (NaN Scale at index 2 splits the line) =====
  {
    std::vector<SwPoint> lineSep;
    lineSep.push_back(makeLinePoint(0.0f));
    lineSep.push_back(makeLinePoint(1.0f));
    SwPoint sep = makeLinePoint(2.0f);
    sep.Scale = SW_PACKED3{NAN, NAN, NAN};   // SEPARATOR marker
    lineSep.push_back(sep);
    lineSep.push_back(makeLinePoint(3.0f));
    lineSep.push_back(makeLinePoint(4.0f));

    ResampleLineParams P{};
    P.SmoothDistance = 0.5f; P.SampleRangeX = 0.0f; P.SampleRangeY = 1.0f;
    P.UpVectorX = 0.0f; P.UpVectorY = 0.0f; P.UpVectorZ = 1.0f;
    P.ResultCount = 5; P.SampleMode = 0; P.SampleCount = 3; P.RotationMode = 0;
    std::vector<SwPoint> out;
    bool ran = runResampleKernelDirect(dev, q, lib, lineSep, P, out);
    bool sizeOk = ran && out.size() == 5;
    int deadCount = 0, aliveCount = 0;
    for (size_t k = 0; sizeOk && k < out.size(); ++k) {
      if (std::isnan(out[k].Scale.x)) deadCount++;
      else aliveCount++;
    }
    // The separator carves a hole: at least one output point fell in the dead region (NaN), and at
    // least one survived on a live segment.
    bool sepOk = sizeOk && deadCount >= 1 && aliveCount >= 1;
    printf("[selftest-resamplelinepoints] C size=%zu dead=%d alive=%d separator=%s\n",
           ran ? out.size() : 0, deadCount, aliveCount, sepOk ? "ok" : "NO");
    pass = pass && sizeOk && sepOk;
  }

  printf("[selftest-resamplelinepoints] -> %s%s\n", pass ? "PASS" : "FAIL",
         injectBug ? " (bug-mode: expect FAIL)" : "");

  // --- graph-path smoke: LinePoints -> ResampleLinePoints -> DrawPoints capture (count-change) ---
  // Proves the op is wired into the real cook driver (countTransform + input bag) end-to-end.
  {
    registerBuiltinPointOps();
    registerResampleLinePointsOp();
    std::vector<SwPoint> captured;
    g_capResample = &captured;
    registerDrawOp("DrawPoints", captureDrawResample);

    PointGraph pg(dev, lib, q, 64, 256);
    Graph g;
    Node gen; gen.id = 1; gen.type = "LinePoints";
    gen.params["Count"]  = 8.0f;
    gen.params["Length"] = 4.0f;
    g.nodes.push_back(gen);
    Node rs; rs.id = 2; rs.type = "ResampleLinePoints";
    rs.params["Count"] = 20.0f;  // resample 8 -> 20 (count-change through the driver)
    g.nodes.push_back(rs);
    Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
    g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
    g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});

    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));

    bool graphCountOk = (captured.size() == 20);  // Count-driven output, not the 8-point source
    printf("[selftest-resamplelinepoints] graph LinePoints(8)->Resample(Count=20) captured=%zu -> %s\n",
           captured.size(), graphCountOk ? "ok" : "NO");
    pass = pass && graphCountOk;
    g_capResample = nullptr;
  }

  printf("[selftest-resamplelinepoints] -> %s\n", pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
