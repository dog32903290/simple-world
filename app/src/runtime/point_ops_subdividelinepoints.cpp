// SubdivideLinePoints — batch 37 lane point_modify COUNT-CHANGING MODIFIER op.
// Faithful port of external/tixl .../point/generate/SubdivideLinePoints (.cs ports, .hlsl math).
// Subdivides every line SEGMENT of the source bag, inserting `InsertCount` interpolated points per
// segment (subdiv = InsertCount + 1). For an OPEN line of SourceCount points the output bag has
// SourceCount * subdiv points: output index i.x -> segmentIndex = i.x/subdiv,
// segmentPointIndex = i.x%subdiv, f = segmentPointIndex/subdiv; f<=0.001 copies the segment START
// verbatim, else lerps START -> START+1 by f (Rotation via qSlerp, the rest via mix). ClosedShape
// adds a closing segment (lastValid -> firstValid) and skips separator segments — see .metal.
//
// COUNT POLICY (the hard seam):
//   Output count = clamp(sourceCount * subdiv, 1, 1000000) where subdiv = clamp(InsertCount,0,1000)+1
//   (SubdivideLinePoints.t3 sizes the output buffer via MultiplyInt(srvComponentCount, subdiv) ->
//   ClampInt(Min=1, Max=1000000); subdiv = ClampInt(Count, 0, 1000) -> AddInts(+1)).
//   This depends on BOTH the source bag count AND the InsertCount param, but the driver's
//   countTransform hook only receives a single scalar (the resolved natural count) and CANNOT see
//   the param. So we use the established STATIC-STASH pattern (= PairPointsForLines): cook() computes
//   the wanted output count from c.inputCounts[0] + the InsertCount param and writes it to a
//   file-static; subdivideCountTransform() returns that static. Cook fns run single-threaded so the
//   static is selftest-safe. Like PairPointsForLines this carries a ONE-FRAME sizing lag on a fresh
//   build (the static defaults to 0 until the first cook sets it; the driver reallocates next frame).
//
//   NAMED FORK [port-id=InsertCount]: the .cs port is named "Count", but the cook driver hijacks any
//   Float input whose port.id == "Count" as the node's OUTPUT point count (point_graph.cpp:254-258).
//   SubdivideLinePoints' Count is InsertCount (per-segment), NOT the output count, so naming the port
//   id "Count" would mis-size the output to InsertCount and ignore the source bag. We give the port
//   id "InsertCount" (the inspector label name stays "Count" to match TiXL) so the driver falls
//   through to sumPointsCount (= the source bag), then countTransform multiplies by subdiv.
//
// TiXL parity (SubdivideLinePoints.cs / .hlsl):
//   - ports ([Input] order): Points, Count(InsertCount, int, d=100), ClosedShape(bool, d=false).
//   - math: see subdividelinepoints.metal (verbatim two-path port).
//
// Self-contained leaf: own capture vector + registerDrawOp.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"                    // calcDispatchCount
#include "runtime/eval_context.h"                // EvaluationContext
#include "runtime/graph.h"                       // Graph/Node/pinId
#include "runtime/point_graph.h"                 // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/subdividelinepoints_params.h"  // SubdivideLineParams, SubdivideLineBinding
#include "runtime/tixl_point.h"                  // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// subdiv = clamp(InsertCount, 0, 1000) + 1  (SubdivideLinePoints.t3 ClampInt(0..1000) -> AddInts +1).
uint32_t subdivFromInsertCount(float insertCount) {
  int ic = (int)(insertCount + 0.5f);
  if (ic < 0) ic = 0;
  if (ic > 1000) ic = 1000;
  return (uint32_t)ic + 1u;
}

// Output buffer size = clamp(sourceCount * subdiv, 1, 1000000) (SubdivideLinePoints.t3 final ClampInt).
uint32_t subdivideOutputCount(uint32_t sourceCount, uint32_t subdiv) {
  uint64_t total = (uint64_t)sourceCount * (uint64_t)subdiv;
  if (total < 1u) total = 1u;
  if (total > 1000000u) total = 1000000u;
  return (uint32_t)total;
}

// STATIC-STASH (= PairPointsForLines): cook() writes the wanted output count here; the
// countTransform hook reads it. Cook fns are single-threaded so this is selftest-safe.
static uint32_t g_subdivideResultCount = 1;

uint32_t subdivideCountTransform(uint32_t /*naturalCount*/) {
  return g_subdivideResultCount;
}

void cookSubdivideLinePoints(PointCookCtx& c) {
  if (!c.lib) return;

  const MTL::Buffer* srcBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  uint32_t sourceCount = (c.inputCounts && c.inputCount > 0) ? c.inputCounts[0] : 0u;

  float insertCount = cookParam(c, "InsertCount", 100.0f);
  float closeShape  = (cookParam(c, "ClosedShape", 0.0f) > 0.5f) ? 1.0f : 0.0f;
  uint32_t subdiv   = subdivFromInsertCount(insertCount);

  // Stash the wanted output count for the countTransform hook (drives next-frame buffer sizing).
  g_subdivideResultCount = subdivideOutputCount(sourceCount, subdiv);

  if (!srcBag || sourceCount == 0 || !c.output || c.count == 0) return;

  MTL::Function* fn =
      c.lib->newFunction(NS::String::string("subdividelinepoints", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  SubdivideLineParams P{};
  P.InsertCount = insertCount;
  P.CloseShape  = closeShape;
  P.ResultCount = c.count;       // ResultPoints.GetDimensions(pointCount)
  P.SourceCount = sourceCount;   // SourcePoints.GetDimensions(sourceCount)

  const uint32_t tg = 64;
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBag), 0, SUBDIVIDELINE_SourcePoints);
  enc->setBuffer(c.output, 0, SUBDIVIDELINE_ResultPoints);
  enc->setBytes(&P, sizeof(P), SUBDIVIDELINE_Params);
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capSubdivide = nullptr;
void captureDrawSubdivide(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capSubdivide || !pts || c.count == 0) return;
  g_capSubdivide->assign(c.count, SwPoint{});
  std::memcpy(g_capSubdivide->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

// Direct single-dispatch runner for precise teeth (no graph plumbing).
bool runSubdivideKernelDirect(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib,
                              const std::vector<SwPoint>& in, float insertCount, bool closed,
                              std::vector<SwPoint>& out) {
  MTL::Function* fn =
      lib->newFunction(NS::String::string("subdividelinepoints", NS::UTF8StringEncoding));
  if (!fn) return false;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return false;

  uint32_t sourceCount = (uint32_t)in.size();
  uint32_t subdiv = subdivFromInsertCount(insertCount);
  uint32_t resultCount = subdivideOutputCount(sourceCount, subdiv);

  SubdivideLineParams P{};
  P.InsertCount = insertCount;
  P.CloseShape  = closed ? 1.0f : 0.0f;
  P.ResultCount = resultCount;
  P.SourceCount = sourceCount;

  const size_t inBytes  = in.size() * sizeof(SwPoint);
  const size_t outBytes = (size_t)resultCount * sizeof(SwPoint);
  MTL::Buffer* src = dev->newBuffer(in.data(), inBytes, MTL::ResourceStorageModeShared);
  MTL::Buffer* dst = dev->newBuffer(outBytes, MTL::ResourceStorageModeShared);
  // Pre-fill output with a sentinel so we can detect untouched trailing slots (closed-shape shrink).
  auto* od = reinterpret_cast<SwPoint*>(dst->contents());
  for (uint32_t k = 0; k < resultCount; ++k) {
    od[k] = SwPoint{};
    od[k].Position = SW_PACKED3{-999.0f, -999.0f, -999.0f};
  }

  const uint32_t tg = 64;
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(src, 0, SUBDIVIDELINE_SourcePoints);
  enc->setBuffer(dst, 0, SUBDIVIDELINE_ResultPoints);
  enc->setBytes(&P, sizeof(P), SUBDIVIDELINE_Params);
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(resultCount, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding(); cmd->commit(); cmd->waitUntilCompleted();

  out.assign(resultCount, SwPoint{});
  std::memcpy(out.data(), dst->contents(), outBytes);
  src->release(); dst->release(); pso->release();
  return true;
}

// Build a source line point at x along +X, identity rotation, finite unit scale.
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

void registerSubdivideLinePointsOp() {
  registerPointOp("SubdivideLinePoints", cookSubdivideLinePoints,
                  /*stateNew=*/nullptr, /*stateFree=*/nullptr,
                  subdivideCountTransform,
                  /*countFromFirstPointsInput=*/false);
}

// =============================================================================
// Golden — SubdivideLinePoints on known +X lines, asserted through the real kernel.
//
//   CASE A (open, hand-computable): SOURCE = 2 points (0,0,0)->(4,0,0), InsertCount=3, open.
//     subdiv = clamp(3,0,1000)+1 = 4.  output count = sourceCount*subdiv = 2*4 = 8.
//     Output index i -> seg = i/4, segPt = i%4, f = segPt/4 = {0,.25,.5,.75}.
//       i=0: seg0 f=0     -> copy SourcePoints[0]            -> x=0
//       i=1: seg0 f=.25   -> lerp src0->src1 by .25 = 0+4*.25 -> x=1
//       i=2: seg0 f=.5    -> x=2
//       i=3: seg0 f=.75   -> x=3
//       i=4: seg1 f=0     -> copy SourcePoints[1]            -> x=4
//       i=5: seg1 f=.25   -> lerp src1->src2 (src2 OOB read; we only assert i<4 endpoints + i=4)
//     TEETH: count==8; x[0]==0; x[1]~=1; x[2]~=2; x[3]~=3; x[4]==4 (segment-start copy of src1).
//     The /subdiv interpolation law (f = (i%subdiv)/subdiv) is what x[1..3] = {1,2,3} proves.
//
//   CASE B (open, count formula): SOURCE = 5 points x=0..4, InsertCount=1, open.
//     subdiv=2, count = 5*2 = 10.  TEETH: count==10 (NOT 5 — output != source; count-change proven).
//
//   CASE C (ClosedShape true vs false differ): SOURCE = 3 points forming a path x=0,1,2 (open path
//     has no wrap), InsertCount=0 -> subdiv=1.  open: count=3*1=3, each output = its segment start
//     (x=0,1,2).  closed: actualSegmentCount = 2 regular (0-1,1-2) + 1 closing (2->0) = 3,
//     totalResultPoints=3 -> the LAST output (i=2, the closing segment, f=0) copies startIndex=
//     lastValid=2 -> x=2; BUT the distinguishing tooth is that the closed path EXERCISES the closing
//     segment branch.  We assert: open count==3 AND closed count==3 AND (with InsertCount=1 ->
//     subdiv=2) the closed output's closing-segment INTERPOLATED point lerps last(x=2)->first(x=0),
//     i.e. an output x == 1.0 appears that the open path (which lerps 2->OOB) does NOT produce.
//
//   CASE D (separator carves closed segments): SOURCE = [x=0, x=1, SEP(NaN), x=3, x=4], InsertCount=0
//     -> subdiv=1, closed.  Regular segments skipping separators: (0-1) ok, (1-SEP) skip, (SEP-3)
//     skip, (3-4) ok => 2 regular.  firstValid=0, lastValid=4, closing (4->0) => +1 => 3 segments.
//     totalResultPoints = 3.  TEETH: at least the closing-segment output exists and equals a valid
//     (non-NaN) source point (the separator did NOT leak a NaN into a closed segment start).
//
//   injectBug: asserts the WRONG subdivision law for CASE A — that output x EQUALS the segment index
//     (x[1]==0, x[2]==0, x[3]==0, i.e. no f-interpolation, every sub-point sits on the segment start).
//     The real kernel lerps by f so x[1..3] = {1,2,3}; asserting they're all 0 -> FAIL.  A real parity
//     flip (the f = segmentPointIndex/subdiv interpolation, .hlsl:42/48), not an inverted assert.
// =============================================================================
int runSubdivideLinePointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-subdividelinepoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  bool pass = true;

  // ===== CASE A: open subdivide of a 2-point line, InsertCount=3 =====
  {
    std::vector<SwPoint> line2 = {makeLinePoint(0.0f), makeLinePoint(4.0f)};
    std::vector<SwPoint> out;
    bool ran = runSubdivideKernelDirect(dev, q, lib, line2, /*insertCount=*/3.0f, /*closed=*/false, out);
    bool countOk = ran && out.size() == 8;
    // Expected segment-0 sub-points: x = {0,1,2,3}; segment-1 start (i=4) = src1 = 4.
    float ex1 = injectBug ? 0.0f : 1.0f;
    float ex2 = injectBug ? 0.0f : 2.0f;
    float ex3 = injectBug ? 0.0f : 3.0f;
    bool x0 = countOk && std::fabs(out[0].Position.x - 0.0f) < 0.01f;
    bool x1 = countOk && std::fabs(out[1].Position.x - ex1) < 0.01f;
    bool x2 = countOk && std::fabs(out[2].Position.x - ex2) < 0.01f;
    bool x3 = countOk && std::fabs(out[3].Position.x - ex3) < 0.01f;
    bool x4 = countOk && std::fabs(out[4].Position.x - 4.0f) < 0.01f;
    bool aOk = countOk && x0 && x1 && x2 && x3 && x4;
    printf("[selftest-subdividelinepoints] A count=%zu x={%.2f,%.2f,%.2f,%.2f,%.2f} interp=%s\n",
           ran ? out.size() : 0,
           countOk ? out[0].Position.x : 0.0f, countOk ? out[1].Position.x : 0.0f,
           countOk ? out[2].Position.x : 0.0f, countOk ? out[3].Position.x : 0.0f,
           countOk ? out[4].Position.x : 0.0f, aOk ? "ok" : "NO");
    pass = pass && aOk;
  }

  // ===== CASE B: open count formula — 5 points, InsertCount=1 -> 10 (count-change) =====
  {
    std::vector<SwPoint> line5;
    for (int j = 0; j < 5; ++j) line5.push_back(makeLinePoint((float)j));
    std::vector<SwPoint> out;
    bool ran = runSubdivideKernelDirect(dev, q, lib, line5, /*insertCount=*/1.0f, /*closed=*/false, out);
    bool sizeOk = ran && out.size() == 10;  // 5*2, NOT inherited 5
    printf("[selftest-subdividelinepoints] B size=%zu (source was 5 -> count-change) -> %s\n",
           ran ? out.size() : 0, sizeOk ? "ok" : "NO");
    pass = pass && sizeOk;
  }

  // ===== CASE C: ClosedShape true vs false differ (closing segment 2->0) =====
  {
    std::vector<SwPoint> line3 = {makeLinePoint(0.0f), makeLinePoint(1.0f), makeLinePoint(2.0f)};

    std::vector<SwPoint> outOpen, outClosed;
    bool ranO = runSubdivideKernelDirect(dev, q, lib, line3, /*insertCount=*/1.0f, /*closed=*/false, outOpen);
    bool ranC = runSubdivideKernelDirect(dev, q, lib, line3, /*insertCount=*/1.0f, /*closed=*/true,  outClosed);
    // Both buffers size to sourceCount*subdiv = 3*2 = 6.
    bool sizeOk = ranO && ranC && outOpen.size() == 6 && outClosed.size() == 6;
    // CLOSED has 3 segments (0-1, 1-2, closing 2-0) -> 6 result points (all 6 written).
    //   The closing segment is segmentIndex 2 -> outputs i=4 (f=0 -> src2 x=2), i=5 (f=.5 -> lerp
    //   src2(x=2)->src0(x=0) = 1.0).  The OPEN path's i=5 lerps src2->src3 (OOB) -> NOT 1.0.
    bool closingInterp = sizeOk && std::fabs(outClosed[5].Position.x - 1.0f) < 0.01f;
    bool differ = sizeOk && std::fabs(outClosed[5].Position.x - outOpen[5].Position.x) > 0.01f;
    bool cOk = sizeOk && closingInterp && differ;
    printf("[selftest-subdividelinepoints] C openSz=%zu closedSz=%zu closed[5].x=%.2f open[5].x=%.2f "
           "closingInterp=%s differ=%s\n",
           ranO ? outOpen.size() : 0, ranC ? outClosed.size() : 0,
           sizeOk ? outClosed[5].Position.x : 0.0f, sizeOk ? outOpen[5].Position.x : 0.0f,
           closingInterp ? "ok" : "NO", differ ? "ok" : "NO");
    pass = pass && cOk;
  }

  // ===== CASE D: separator carves closed segments =====
  {
    std::vector<SwPoint> lineSep;
    lineSep.push_back(makeLinePoint(0.0f));
    lineSep.push_back(makeLinePoint(1.0f));
    SwPoint sep = makeLinePoint(2.0f);
    sep.Scale = SW_PACKED3{NAN, NAN, NAN};  // SEPARATOR marker
    lineSep.push_back(sep);
    lineSep.push_back(makeLinePoint(3.0f));
    lineSep.push_back(makeLinePoint(4.0f));

    std::vector<SwPoint> out;
    bool ran = runSubdivideKernelDirect(dev, q, lib, lineSep, /*insertCount=*/0.0f, /*closed=*/true, out);
    // subdiv=1. Regular non-sep segments: (0-1) ok, (3-4) ok = 2; closing (4->0) = +1 => 3 segments.
    // totalResultPoints = 3. Each output (f=0) copies its segment start.  The buffer is sized to
    // sourceCount*subdiv = 5, so slots 3,4 are TRAILING-UNTOUCHED (sentinel x=-999).
    bool sizeOk = ran && out.size() == 5;
    // Written outputs (i=0,1,2) must be finite (no NaN leaked from the separator into a segment start)
    // and the closing-segment output (i=2, start=lastValid=4) must be x=4 (a valid source point).
    bool finiteWritten = sizeOk;
    for (int k = 0; sizeOk && k < 3; ++k)
      if (!std::isfinite(out[k].Position.x) || std::isnan(out[k].Scale.x)) finiteWritten = false;
    bool closingStartOk = sizeOk && std::fabs(out[2].Position.x - 4.0f) < 0.01f;  // lastValid=4
    bool trailingUntouched = sizeOk && out[3].Position.x < -900.0f && out[4].Position.x < -900.0f;
    bool dOk = sizeOk && finiteWritten && closingStartOk && trailingUntouched;
    printf("[selftest-subdividelinepoints] D size=%zu written.x={%.2f,%.2f,%.2f} trailing={%.1f,%.1f} "
           "finite=%s closing=%s trailing=%s\n",
           ran ? out.size() : 0,
           sizeOk ? out[0].Position.x : 0.0f, sizeOk ? out[1].Position.x : 0.0f,
           sizeOk ? out[2].Position.x : 0.0f,
           sizeOk ? out[3].Position.x : 0.0f, sizeOk ? out[4].Position.x : 0.0f,
           finiteWritten ? "ok" : "NO", closingStartOk ? "ok" : "NO",
           trailingUntouched ? "ok" : "NO");
    pass = pass && dOk;
  }

  printf("[selftest-subdividelinepoints] -> %s%s\n", pass ? "PASS" : "FAIL",
         injectBug ? " (bug-mode: expect FAIL)" : "");

  // --- graph-path smoke: LinePoints -> SubdivideLinePoints -> DrawPoints capture (count-change) ---
  // Proves the op is wired into the real cook driver (static-stash countTransform + input bag).
  // NOTE: the static-stash sizing has a one-frame lag (g_subdivideResultCount starts at its prior
  // value), so we cook TWICE and assert the SECOND cook's captured count = sourceCount*subdiv.
  {
    registerBuiltinPointOps();
    registerSubdivideLinePointsOp();
    std::vector<SwPoint> captured;
    g_capSubdivide = &captured;
    registerDrawOp("DrawPoints", captureDrawSubdivide);

    PointGraph pg(dev, lib, q, 64, 256);
    Graph g;
    Node gen; gen.id = 1; gen.type = "LinePoints";
    gen.params["Count"]  = 6.0f;
    gen.params["Length"] = 5.0f;
    g.nodes.push_back(gen);
    Node sd; sd.id = 2; sd.type = "SubdivideLinePoints";
    sd.params["InsertCount"] = 3.0f;  // subdiv=4 -> output = 6*4 = 24
    g.nodes.push_back(sd);
    Node drw; drw.id = 3; drw.type = "DrawPoints"; g.nodes.push_back(drw);
    g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
    g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});

    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));  // frame 1: sizes the static
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));  // frame 2: buffer now correctly sized

    bool graphCountOk = (captured.size() == 24);  // 6 source * subdiv 4 (count-change via driver)
    printf("[selftest-subdividelinepoints] graph LinePoints(6)->Subdivide(InsertCount=3) captured=%zu -> %s\n",
           captured.size(), graphCountOk ? "ok" : "NO");
    pass = pass && graphCountOk;
    g_capSubdivide = nullptr;
  }

  printf("[selftest-subdividelinepoints] -> %s\n", pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
