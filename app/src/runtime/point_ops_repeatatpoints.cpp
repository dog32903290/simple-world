// RepeatAtPoints — GENERATE op (point combine/generate family): the count-PRODUCT canonical leaf.
// Places each SourcePoint into EACH TargetPoint's local frame → the FULL CARTESIAN PRODUCT:
//   ResultCount = source.N * target.N   (NOT a sum — this is what the count-product driver seam proves)
// Faithful port of TiXL's GPU RepeatAtPoints (the .t3-wired ComputeShader, NOT the simplified _cpu variant).
//
// Reference:
//   external/tixl/Operators/Lib/point/generate/RepeatAtPoints.cs   (12 InputSlots + ConnectionModes/UseFSources enums)
//   external/tixl/Operators/Lib/point/generate/RepeatAtPoints.t3    (buffer-count graph: MultiplyInt(srcN+sepA, tgtN+sepB))
//   external/tixl/Operators/Lib/Assets/shaders/points/generate/RepeatAtGPoints.hlsl   (the per-point math, ported 1:1)
//
// COUNT-PRODUCT DRIVER SEAM (verdict (B) static-stash, zero driver signature change):
//   The driver's countTransform hook is `uint32_t(*)(uint32_t naturalCount)` — it sees ONLY the summed
//   natural count, not per-input counts, so it cannot compute A*B by itself. The PROVEN precedent
//   (PairPointsForLines, point_ops_pairpointsforlines.cpp) is: the cook fn writes a FILE-STATIC with
//   the true product; countTransform reads it. Cook fns run SINGLE-THREADED & SEQUENTIALLY → the static
//   is safe. Both cook paths (flat point_graph.cpp:357 + resident point_graph_resident.cpp:227) call
//   countTransform(naturalCount) IDENTICALLY, so the SAME static drives BOTH — no driver edit at all.
//   Existing ops register countTransform=nullptr → their path is byte-identical untouched.
//
//   ★Two-frame seeding (production-faithful): on the FIRST cook after a (re)build the static defaults to
//   0 → output sized 0 → cook runs & sets the static to srcN*tgtN → the NEXT cook reallocs to the real
//   product. This mirrors exactly how production runs frame-after-frame; the resident-pixel golden cooks
//   TWICE (frame 1 seeds, frame 2 reallocs) — the same double-cook the mesh-input production golden uses
//   for buffer reuse.
//
// SwPoint <-> TiXL Point: Position/Color/Scale 1:1; Rotation<-Orientation; FX1<-F1; FX2<-F2 (四流 rename).
//
// NAMED FORKS (also in repeatatpoints_params.h):
//   fork[count-product-policy]: ResultCount = (srcN + sepSrc) * (tgtN + sepTgt) — the .t3 count graph
//     MultiplyInt(AddInts(srcN, gateSrc), AddInts(tgtN, gateTgt)). Static-stash driver hook (B), NOT
//     signature widen (A): (A) would add per-input-counts to PointCountFn — wider regression surface —
//     and the proven (B) precedent already nails count=f(A,B) on both paths.
//   AddSeparators count (RESOLVED — was deferred, refuter BLOCK forced the fix): the .t3 grows the
//     per-loop length by +1 when AddSeparators inserts a NaN-divider row. The two CompareInt gates run
//     Mode=IsEqual (CompareInt.t3 Mode DefaultValue=1, NOT the C# field default 0 — verified from the
//     symbol .t3, this was the trap): ef16fc8f (source-side) = (CombineMode==0 ? BoolToInt(AddSep) : 0);
//     9506cd5e (target-side) = (CombineMode==1 ? BoolToInt(AddSep) : 0). Linear default (AddSep=true)
//     => count=(srcN+1)*tgtN. This matches RepeatAtGPoints.hlsl: Linear sourceLength=srcN+1 (line 53),
//     the per-loop trailing NaN-divider row (lines 66-70). The shader separator branch was ALREADY
//     ported faithfully (.metal lines 53-69); only the count was wrong (pure product) — now fixed so
//     the separator row count matches the shader's wrap arithmetic exactly.
//   fork[combinemode-both]: Linear(0) + Interwoven(1) ConnectPointsMode BOTH ported in the shader; the
//     production golden exercises Linear default (CombineMode=0, AddSeparators=true → with separators).
//   fork[dummy-buf]: when TargetPoints is unwired we bind SourcePoints as a dummy to avoid a Metal nil
//     buffer slot; the count is 0 (target empty) so no thread reads it.
//
// Self-contained leaf: own cook + register + golden. The NodeSpec lives in node_registry_point_combine.cpp
// (shared family table, same as SnapToPoints/MultiUpdatePoints — combine ops need a real spec to cook on
// the PRODUCTION resident path, unlike the golden-only PairPointsForLines).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"            // calcDispatchCount
#include "runtime/eval_context.h"        // EvaluationContext
#include "runtime/graph.h"               // Graph/Node/pinId/findSpec
#include "runtime/graph_bridge.h"        // libFromGraph (flat Graph -> SymbolLibrary)
#include "runtime/point_graph.h"         // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/resident_eval_graph.h" // buildEvalGraph (production path)
#include "runtime/tixl_point.h"          // SwPoint (64B)
#include "runtime/repeatatpoints_params.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// fork[count-product-policy] static-stash: the cook fn writes the true product here; countTransform
// reads it (the proven PairPointsForLines pattern). Single-threaded sequential cook → safe.
static uint32_t g_repeatAtResultCount = 0;

uint32_t repeatAtCountTransform(uint32_t /*naturalCount*/) {
  return g_repeatAtResultCount;
}

// Read UseFSources / mode enums from the resolved Float param map (stored as floats, rounded to int).
int cookEnum(PointCookCtx& c, const char* id, int def) {
  float v = cookParam(c, id, (float)def);
  return (int)(v + (v >= 0.0f ? 0.5f : -0.5f));
}

void cookRepeatAtPoints(PointCookCtx& c) {
  if (!c.lib) return;

  // SourcePoints (spec port 0 = GPoints) + TargetPoints (spec port 1 = GTargets).
  const MTL::Buffer* srcBuf = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  const MTL::Buffer* tgtBuf = (c.inputCount > 1) ? c.inputs[1] : nullptr;
  uint32_t srcN = (c.inputCounts && c.inputCount > 0) ? c.inputCounts[0] : 0u;
  uint32_t tgtN = (c.inputCounts && c.inputCount > 1) ? c.inputCounts[1] : 0u;

  // Read params first — the separator-count gates depend on CombineMode + AddSeparators.
  RepeatAtPointsParams P{};
  P.Scale                  = cookParam(c, "Scale", 1.0f);
  P.ApplyTargetOrientation = (cookParam(c, "ApplyOrientation", 1.0f) > 0.5f) ? 1 : 0;
  P.ApplyTargetScale       = (cookParam(c, "ApplyPointScale", 1.0f) > 0.5f) ? 1 : 0;
  P.ScaleFactorMode        = cookEnum(c, "ScaleFactor", 0);
  P.SetF1To                = cookEnum(c, "SetF1To", 5);   // .t3 default 5 (Multiplied_F1)
  P.SetF2To                = cookEnum(c, "SetF2To", 6);   // .t3 default 6 (Multiplied_F2)
  P.ConnectPointsMode      = cookEnum(c, "CombineMode", 0);  // 0 Linear (.t3 default)
  P.AddSeperators          = (cookParam(c, "AddSeparators", 1.0f) > 0.5f) ? 1 : 0;  // .t3 default true

  // fork[count-product-policy]: ResultCount = (srcN + sepSrc) * (tgtN + sepTgt). The .t3 count graph is
  //   MultiplyInt( AddInts(srcN, ef16fc8f), AddInts(tgtN, 9506cd5e) )
  // where each CompareInt gate runs Mode=IsEqual (CompareInt.t3 Mode DefaultValue=1, NOT the C# field 0):
  //   ef16fc8f (source-side): (CombineMode == 0 ? BoolToInt(AddSep) : 0)     [TestValue=0, ResultTrue<-BoolToInt]
  //   9506cd5e (target-side): (CombineMode == 1 ? BoolToInt(AddSep) : 0)     [TestValue=1, ResultTrue<-BoolToInt]
  // => Linear adds the separator row to the SOURCE loop (sourceLength=srcN+1, HLSL line 53);
  //    Interwoven adds it to the TARGET loop. This matches RepeatAtGPoints.hlsl byte-for-byte:
  //    Linear default (AddSep=true) -> count=(8+1)*4=36, NOT the bare product 32.
  const uint32_t addSep = (P.AddSeperators != 0) ? 1u : 0u;
  const uint32_t sepSrc = (P.ConnectPointsMode == 0) ? addSep : 0u;  // Linear: source loop +1
  const uint32_t sepTgt = (P.ConnectPointsMode == 1) ? addSep : 0u;  // Interwoven: target loop +1
  uint32_t resultCount = (srcN > 0 && tgtN > 0) ? (srcN + sepSrc) * (tgtN + sepTgt) : 0u;
  g_repeatAtResultCount = resultCount;  // seed for the NEXT cook's countTransform reallocation

  if (resultCount == 0 || !srcBuf || !c.output) return;
  // fork[dummy-buf]: TargetPoints unwired (tgtN==0 already gave resultCount 0, so we only reach here
  // when both are wired with >0 counts). Still guard tgtBuf nil for safety.
  const MTL::Buffer* tgtSafe = (tgtBuf && tgtN > 0) ? tgtBuf : srcBuf;

  MTL::Function* fn = c.lib->newFunction(
      NS::String::string("repeatatpoints", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  const uint32_t tg = 64;
  MTL::CommandBuffer*         cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(srcBuf),  0, REPEATATPOINTS_SourcePoints);
  enc->setBuffer(const_cast<MTL::Buffer*>(tgtSafe), 0, REPEATATPOINTS_TargetPoints);
  enc->setBuffer(c.output,                          0, REPEATATPOINTS_Result);
  enc->setBytes(&P, sizeof(P),                         REPEATATPOINTS_Params);
  enc->setBytes(&srcN,        sizeof(uint32_t),        4);
  enc->setBytes(&tgtN,        sizeof(uint32_t),        5);
  enc->setBytes(&resultCount, sizeof(uint32_t),        6);
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(resultCount, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden plumbing (self-contained: own capture vector + draw op) ---
std::vector<SwPoint>* g_capRepeatAt = nullptr;
void captureDrawRepeatAt(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_capRepeatAt || !pts || c.count == 0) return;
  g_capRepeatAt->assign(c.count, SwPoint{});
  std::memcpy(g_capRepeatAt->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}

}  // namespace

void registerRepeatAtPointsOp() {
  // fork[count-product-policy]: countTransform returns the static product set by the cook fn (the
  // proven PairPointsForLines two-frame seeding). countFromFirstPointsInput=false: the natural count
  // is the SUM (ignored — countTransform overrides it entirely with the product).
  registerPointOp("RepeatAtPoints", cookRepeatAtPoints,
                  /*stateNew=*/nullptr, /*stateFree=*/nullptr,
                  repeatAtCountTransform,
                  /*countFromFirstPointsInput=*/false);
}

// ===================== Golden 1: closed-form buffer readback (AddSeparators=0, pure product) ============
// ★This case pins the NO-SEPARATOR math: P.AddSeperators=0 → count = pure product NS_*ND = 32 (no NaN
// rows). Golden 2 covers the production DEFAULT (AddSeparators=true → 36 with separators). Together they
// bracket the count formula at both ends of the AddSeparators gate.
// Source N=8 (positions (i+1,0,0), F1=i, identity Rot, unit Scale) × Dest M=4 (positions (0, 10+d, 0),
// identity Rot, unit Scale) → out = 32 points. With Scale=1, ScaleFactor=0(factor 1), ApplyTargetScale=1
// (×unit), ApplyOrientation=1 (qRotate by identity = no-op):
//   out[d*8 + s].Position == dest[d].Pos + source[s].Pos == (s+1, 10+d, 0)   (closed-form, byte-exact)
//   out[d*8 + s].Rotation == qMul(identity, identity) == identity
// This drives the GPU kernel DIRECTLY (raw buffers) so the math is asserted independent of the driver.
// injectBug: assert the count is the SUM (8+4=12) instead of the product → real output is 32 → the
// count assertion FAILS (RED), proving the op produces the PRODUCT not the sum.
int runRepeatAtPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t NS_ = 8, ND = 4;

  MTL::Device*       dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue*   q = dev->newCommandQueue();
  NS::Error*         err = nullptr;
  MTL::Library*      lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-repeatatpoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::Buffer* srcBuf = dev->newBuffer(NS_ * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* tgtBuf = dev->newBuffer(ND  * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  auto* sp = reinterpret_cast<SwPoint*>(srcBuf->contents());
  auto* tp = reinterpret_cast<SwPoint*>(tgtBuf->contents());
  for (uint32_t i = 0; i < NS_; ++i) {
    sp[i] = SwPoint{};
    sp[i].Position = {(float)(i + 1), 0.0f, 0.0f};
    sp[i].FX1 = (float)i;
    sp[i].Rotation = {0, 0, 0, 1};               // identity
    sp[i].Scale = {1, 1, 1};
    sp[i].Color = {1, 1, 1, 1};
  }
  for (uint32_t d = 0; d < ND; ++d) {
    tp[d] = SwPoint{};
    tp[d].Position = {0.0f, 10.0f + (float)d, 0.0f};
    tp[d].Rotation = {0, 0, 0, 1};               // identity → qRotate no-op
    tp[d].Scale = {1, 1, 1};                       // unit → ApplyTargetScale no-op
    tp[d].Color = {1, 1, 1, 1};
  }

  uint32_t resultCount = NS_ * ND;  // 32
  RepeatAtPointsParams P{};
  P.Scale = 1.0f; P.ApplyTargetOrientation = 1; P.ApplyTargetScale = 1;
  P.ScaleFactorMode = 0; P.SetF1To = 0; P.SetF2To = 0;  // factors[0]=1 (deterministic F1/F2)
  P.ConnectPointsMode = 0;  // Linear
  P.AddSeperators = 0;       // no separators (count = pure product)

  MTL::Function* fn = lib->newFunction(NS::String::string("repeatatpoints", NS::UTF8StringEncoding));
  if (!fn) {
    printf("[selftest-repeatatpoints] FAIL: no kernel 'repeatatpoints'\n");
    srcBuf->release(); tgtBuf->release(); lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }
  NS::Error* psoErr = nullptr;
  MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &psoErr);
  fn->release();
  if (!pso) {
    printf("[selftest-repeatatpoints] FAIL: no PSO\n");
    srcBuf->release(); tgtBuf->release(); lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::Buffer* outBuf = dev->newBuffer(resultCount * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::CommandBuffer*         cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(srcBuf, 0, REPEATATPOINTS_SourcePoints);
  enc->setBuffer(tgtBuf, 0, REPEATATPOINTS_TargetPoints);
  enc->setBuffer(outBuf, 0, REPEATATPOINTS_Result);
  enc->setBytes(&P, sizeof(P), REPEATATPOINTS_Params);
  enc->setBytes(&NS_,         sizeof(uint32_t), 4);
  enc->setBytes(&ND,          sizeof(uint32_t), 5);
  enc->setBytes(&resultCount, sizeof(uint32_t), 6);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make((resultCount + tg - 1) / tg, 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();

  auto* out = reinterpret_cast<SwPoint*>(outBuf->contents());

  // injectBug: assert the SUM count (12) — the real product is 32 → the count tooth bites.
  uint32_t expectCount = injectBug ? (NS_ + ND) : (NS_ * ND);
  bool countOK = (resultCount == expectCount);  // 32==12 false under bug → RED
  bool posOK = true, rotOK = true;
  for (uint32_t d = 0; d < ND && posOK && rotOK; ++d) {
    for (uint32_t s = 0; s < NS_; ++s) {
      uint32_t i = d * NS_ + s;  // Linear ordering: outer dest, inner source (== HLSL i/srcLen, i%srcLen)
      // closed-form: dest.Pos + source.Pos (identity rot, unit scale)
      float ex = 0.0f + (float)(s + 1);
      float ey = 10.0f + (float)d + 0.0f;
      if (std::fabs(out[i].Position.x - ex) > 1e-3f) posOK = false;
      if (std::fabs(out[i].Position.y - ey) > 1e-3f) posOK = false;
      if (std::fabs(out[i].Position.z - 0.0f) > 1e-3f) posOK = false;
      // identity * identity = identity
      if (std::fabs(out[i].Rotation.w - 1.0f) > 1e-3f) rotOK = false;
    }
  }

  bool pass = countOK && posOK && rotOK;
  printf("[selftest-repeatatpoints] product=%u(want %u) count=%d pos=%d rot=%d -> %s\n",
         resultCount, expectCount, countOK ? 1 : 0, posOK ? 1 : 0, rotOK ? 1 : 0,
         pass ? "PASS" : "FAIL");

  outBuf->release(); srcBuf->release(); tgtBuf->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// ===================== Golden 2: driver count-product (flat) + production resident pixel =====================
// ★PRODUCTION-DEFAULT PARITY (refuter修向1): production cooks with DEFAULT params — and the .t3 default is
// AddSeparators=true, CombineMode=Linear. So the production count is NOT the bare product (8*4=32); it is
// (srcN + 1) * tgtN = 9*4 = 36 (the Linear separator row inserted at the end of each source loop, HLSL
// line 53/66-70). The earlier golden asserted 32 against a DEFAULT-true cook = a deviation-self-consistent
// bug (Cut47「滑成只自洽」). This cell now asserts the TiXL-true count 36 AND that the separator rows land
// with Scale=NAN at the correct stride positions (sourceIndex == srcN, i.e. every 9th point).
// Proves the count-product DRIVER SEAM lives on BOTH cook paths:
//   (a) FLAT: SpherePoints(N=8) → RepeatAtPoints.Source + SpherePoints(M=4) → RepeatAtPoints.Target →
//       DrawPoints (terminal). Cook TWICE (frame 1 seeds the static, frame 2 reallocs to 36). Assert
//       debugCookedCount == 36 (driver sized output to the production-default count WITH separators) +
//       the capture has 36 points + the 4 separator rows (indices 8,17,26,35) have Scale.x == NAN.
//       injectBug DROPS the Target wire → tgtN=0 → count 0 ≠ 36 → RED.
//   (b) ★PRODUCTION RESIDENT PIXEL: same graph + RenderTarget through libFromGraph→buildEvalGraph→
//       cookResident (the canonical production path). Cook TWICE; read pg.target() pixels and assert the
//       point cloud is ON SCREEN. injectBug drops the Target wire → count 0 → black → RED.
int runRepeatAtPointsProductionSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  // Production default: AddSeparators=true + Linear → count = (NS_+1)*ND = 9*4 = 36 (WITH separators).
  const uint32_t NS_ = 8, ND = 4, EXPECT = (NS_ + 1) * ND;  // 36 (Linear separator row per source loop)
  const uint32_t W = 256, H = 256;

  MTL::Device*       dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue*   q = dev->newCommandQueue();
  NS::Error*         err = nullptr;
  MTL::Library*      lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-repeatatpoints-prod] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();        // SpherePoints + DrawPoints(cmd) + RenderTarget(tex) + RepeatAtPoints
  registerRepeatAtPointsOp();       // explicit (self-contained)

  // Build the source/target generators + RepeatAtPoints. Find ports from specs (robust to spec order).
  auto firstOut = [](const char* t) {
    const NodeSpec* s = findSpec(t); int o = -1;
    if (s) for (size_t i = 0; i < s->ports.size(); ++i) if (!s->ports[i].isInput) { o = (int)i; break; }
    return o;
  };
  auto inputPortByType = [](const char* t, const char* dt, int which) {
    const NodeSpec* s = findSpec(t); int seen = 0;
    if (s) for (size_t i = 0; i < s->ports.size(); ++i)
      if (s->ports[i].isInput && s->ports[i].dataType == dt) { if (seen == which) return (int)i; ++seen; }
    return -1;
  };

  const NodeSpec* rapSpec = findSpec("RepeatAtPoints");
  if (!rapSpec) {
    printf("[selftest-repeatatpoints-prod] FAIL: RepeatAtPoints has no NodeSpec (needs node_registry_point_combine entry)\n");
    lib->release(); q->release(); dev->release(); pool->release();
    return 1;
  }
  int sphOut   = firstOut("SpherePoints");
  int rapSrcIn = inputPortByType("RepeatAtPoints", "Points", 0);   // SourcePoints (port 0)
  int rapTgtIn = inputPortByType("RepeatAtPoints", "Points", 1);   // TargetPoints (port 1)
  int rapOut   = firstOut("RepeatAtPoints");
  int drawPtsIn= inputPortByType("DrawPoints", "Points", 0);
  int drawOut  = firstOut("DrawPoints");
  int rtCmdIn  = inputPortByType("RenderTarget", "Command", 0);

  auto buildGraph = [&](bool withTarget, bool withRender) {
    Graph g;
    Node src; src.id = 1; src.type = "SpherePoints"; src.params["Count"] = (float)NS_; src.params["Radius"] = 0.3f;
    g.nodes.push_back(src);
    Node tgt; tgt.id = 2; tgt.type = "SpherePoints"; tgt.params["Count"] = (float)ND; tgt.params["Radius"] = 0.6f;
    g.nodes.push_back(tgt);
    Node rap; rap.id = 3; rap.type = "RepeatAtPoints"; g.nodes.push_back(rap);
    g.connections.push_back({100, pinId(1, sphOut), pinId(3, rapSrcIn)});       // source wired always
    if (withTarget) g.connections.push_back({101, pinId(2, sphOut), pinId(3, rapTgtIn)});  // target (bug drops)
    if (withRender) {
      Node draw; draw.id = 4; draw.type = "DrawPoints";
      draw.params["Radius"] = 0.05f; draw.params["Color.x"] = 1.0f; draw.params["Color.y"] = 1.0f;
      draw.params["Color.z"] = 1.0f; draw.params["Color.w"] = 1.0f;  // white points
      g.nodes.push_back(draw);
      Node rt; rt.id = 5; rt.type = "RenderTarget";
      rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
      g.nodes.push_back(rt);
      g.connections.push_back({102, pinId(3, rapOut), pinId(4, drawPtsIn)});
      g.connections.push_back({103, pinId(4, drawOut), pinId(5, rtCmdIn)});
    } else {
      Node draw; draw.id = 4; draw.type = "DrawPoints"; g.nodes.push_back(draw);
      g.connections.push_back({102, pinId(3, rapOut), pinId(4, drawPtsIn)});
    }
    return g;
  };

  // ---- (a) FLAT driver count-product ----
  bool flatCountOK = false; uint32_t flatCooked = 0; size_t flatCap = 0; bool sepRowsOK = false;
  {
    PointGraph pg(dev, lib, q, W, H);
    std::vector<SwPoint> captured; g_capRepeatAt = &captured;
    registerDrawOp("DrawPoints", captureDrawRepeatAt);  // capture-mode draw for the flat readback
    Graph g = buildGraph(/*withTarget=*/!injectBug, /*withRender=*/false);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    g_repeatAtResultCount = 0;  // simulate a fresh (re)build
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));  // frame 1: seeds the static
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));  // frame 2: reallocs to the count (with separators)
    flatCooked = pg.debugCookedCount(3);
    flatCap = captured.size();
    flatCountOK = (flatCooked == (injectBug ? 0u : EXPECT)) && (flatCap == (injectBug ? 0u : EXPECT));

    // ★Separator-row parity (production default AddSeparators=true, Linear): every source loop of length
    // sourceLength=(NS_+1)=9 ends with a NaN-divider row at sourceIndex==NS_ → global indices 8,17,26,35.
    // Those rows must have Scale=NAN (HLSL line 69); all OTHER rows must have FINITE Scale (=1 unit here).
    // This is the byte-exact tooth that the deviation-self-consistent 32-count golden could never see.
    if (!injectBug && flatCap == EXPECT) {
      const uint32_t sourceLength = NS_ + 1;  // Linear separator: srcN + 1 = 9
      sepRowsOK = true;
      for (uint32_t i = 0; i < EXPECT && sepRowsOK; ++i) {
        bool isSep = (i % sourceLength) == NS_;         // the 9th slot of each source loop
        bool nan   = std::isnan(captured[i].Scale.x);
        if (isSep != nan) sepRowsOK = false;            // separator iff Scale is NAN
      }
    }
    g_capRepeatAt = nullptr;
  }

  // ---- (b) ★PRODUCTION RESIDENT PIXEL ----
  // Re-register DrawPoints as the REAL command op (registerBuiltinPointOps installed it; the flat leg
  // overwrote it with the capture stub, so restore production draw for the pixel leg).
  registerBuiltinPointOps();
  registerRepeatAtPointsOp();
  bool pixelOK = false; uint32_t litCount = 0; bool sized = false;
  {
    PointGraph pg(dev, lib, q, W, H);
    Graph g = buildGraph(/*withTarget=*/!injectBug, /*withRender=*/true);
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    g_repeatAtResultCount = 0;  // fresh build
    pg.cookResident(rg, ctx, nullptr, /*RenderTarget path*/ "5");  // frame 1: seed
    pg.cookResident(rg, ctx, nullptr, /*RenderTarget path*/ "5");  // frame 2: realloc to product
    MTL::Texture* tex = pg.target();
    sized = tex && (uint32_t)tex->width() == W && (uint32_t)tex->height() == H;
    if (sized) {
      std::vector<uint8_t> px((size_t)W * H * 4, 0);
      tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
      for (size_t i = 0; i < (size_t)W * H; ++i)
        if (px[i * 4] > 200 && px[i * 4 + 1] > 200 && px[i * 4 + 2] > 200) ++litCount;  // white pixels
    }
    // 32 points at radius 0.05 NDC → at least a few hundred lit px when the product cooked; the bug
    // (no target → product 0 → nothing drawn) leaves the field black. Threshold well between the two.
    pixelOK = sized && (litCount >= 50);
  }

  bool pass = !injectBug ? (flatCountOK && sepRowsOK && pixelOK)
                         : (!flatCountOK && !pixelOK);  // bug must BREAK both legs (count 0 + black)
  printf("[selftest-repeatatpoints-prod] flat: cooked=%u(want %u) cap=%zu countOK=%d sepRows=%d | resident: lit=%u sized=%d pixelOK=%d -> %s\n",
         flatCooked, injectBug ? 0u : EXPECT, flatCap, flatCountOK ? 1 : 0, sepRowsOK ? 1 : 0,
         litCount, sized ? 1 : 0, pixelOK ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
