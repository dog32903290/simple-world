// LinePoints generator op + golden — faithful port of TiXL
// .../points/generate/LinePoints.hlsl. A line of Count points from Center along
// Direction over Length, distributed by Pivot + GainAndBias.
//
// Self-contained leaf (parallel-safe): its own cook fn, register fn, and golden with
// its own capture vector + registerDrawOp. The main agent wires the integration
// snippets (NodeSpec, CMake, selftest table) centrally.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"          // calcDispatchCount
#include "runtime/graph.h"             // Graph/Node/pinId/readVecN
#include "runtime/linepoints_params.h" // LineParams, LineBinding
#include "runtime/point_graph.h"       // PointCookCtx, registerPointOp/DrawOp, PointGraph
#include "runtime/tixl_point.h"        // SwPoint (64B) + EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

float paramOr(const Node* n, const char* id, float def) {
  if (!n) return def;
  auto it = n->params.find(id);
  return it != n->params.end() ? it->second : def;
}

// LinePoints generator: dispatch the linepoints kernel into the node's output bag.
// Reads scalar Float params (Count via ctx.count; Length/Pivot/Scale*/GainBias* from
// the node) + the two vector params Center & Direction via readVecN (per-node, like
// cookRadialPoints reads Center). TiXL's Color/orientation-quat/F1/F2 are baked to
// defaults in linepoints.metal until those param kinds land in NodeSpec.
void cookLinePoints(PointCookCtx& c) {
  if (!c.output || c.count == 0 || !c.lib) return;
  MTL::Function* fn = c.lib->newFunction(NS::String::string("linepoints", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  const Node* n = c.graph ? c.graph->node(c.nodeId) : nullptr;
  LineParams P{};
  P.Count = c.count;
  P.LengthFactor = paramOr(n, "Length", 5.0f);
  P.Pivot = paramOr(n, "Pivot", 0.5f);
  P.GainBiasX = paramOr(n, "GainAndBias.x", 0.5f);  // TiXL GainAndBias default (identity)
  P.GainBiasY = paramOr(n, "GainAndBias.y", 0.5f);
  P.ScaleBase = paramOr(n, "Scale.x", 1.0f);        // TiXL PointSize.x
  P.ScaleByF = paramOr(n, "Scale.y", 0.0f);         // TiXL PointSize.y
  float center[3] = {0.0f, 0.0f, 0.0f};
  if (n) readVecN(*n, "Center", center, 3, center);
  P.CenterX = center[0]; P.CenterY = center[1]; P.CenterZ = center[2];
  float dirFallback[3] = {0.0f, 1.0f, 0.0f};        // TiXL .md: 0,1,0 points the line up
  float direction[3] = {0.0f, 1.0f, 0.0f};
  if (n) readVecN(*n, "Direction", dirFallback, 3, direction);
  P.DirectionX = direction[0]; P.DirectionY = direction[1]; P.DirectionZ = direction[2];

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(c.output, 0, LINE_Points);
  enc->setBytes(&P, sizeof(P), LINE_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

// --- golden: self-contained capture + draw stub ---
std::vector<SwPoint>* g_lineCap = nullptr;
void captureLineDraw(PointCookCtx& c, MTL::Texture*, const MTL::Buffer* pts) {
  if (!g_lineCap || !pts || c.count == 0) return;
  g_lineCap->assign(c.count, SwPoint{});
  std::memcpy(g_lineCap->data(), const_cast<MTL::Buffer*>(pts)->contents(),
              (size_t)c.count * sizeof(SwPoint));
}
MTL::Library* loadLineLib(MTL::Device* dev) {
  NS::Error* err = nullptr;
  return dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
}

}  // namespace

void registerLinePointsOp() {
  registerPointOp("LinePoints", cookLinePoints);
}

// Golden with TEETH (TiXL LinePoints.hlsl line 66: Pos = lerp(Center, Center+Dir*Len, f1-Pivot)):
//  - COLINEAR: every point lies on the infinite line through Center along Direction
//    (perpendicular distance ~= 0) — Direction is a non-axis-aligned 3D vector so this is real.
//  - EVENLY SPACED: with GainAndBias identity + Pivot 0, projections onto Direction are
//    arithmetic (consecutive gaps equal) and span exactly Length (last point at Center+Dir*Len).
//  - PIVOT CENTERED: re-cook with Pivot=0.5 and assert the projection midpoint sits at Center
//    (mean projection ~= 0) — proves Pivot shifts the line, not just translates it.
// injectBug = collapse the spacing: Length=0 so every point lands on Center (zero span) ->
// the "spans Length" + "even nonzero spacing" assertions FAIL. A real degeneracy, not a flip.
int runLinePointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 64;
  const float LEN = injectBug ? 0.0f : 6.0f;       // bug: 0 length -> all points collapse
  // Non-axis-aligned unit direction so colinearity is a genuine 3D test.
  const float dx = 0.6f, dy = 0.8f, dz = 0.0f;     // |d| = 1
  const float cx = 1.0f, cy = -2.0f, cz = 0.5f;    // arbitrary Center

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  MTL::Library* lib = loadLineLib(dev);
  if (!lib) {
    printf("[selftest-linepoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerLinePointsOp();
  std::vector<SwPoint> captured;
  g_lineCap = &captured;
  registerDrawOp("DrawPoints", captureLineDraw);

  PointGraph pg(dev, lib, q, 64, 64);

  auto cookWithPivot = [&](float pivot) {
    Graph g;
    Node gen; gen.id = 1; gen.type = "LinePoints";
    gen.params["Count"] = (float)N;
    gen.params["Length"] = LEN;
    gen.params["Pivot"] = pivot;
    gen.params["Center.x"] = cx; gen.params["Center.y"] = cy; gen.params["Center.z"] = cz;
    gen.params["Direction.x"] = dx; gen.params["Direction.y"] = dy; gen.params["Direction.z"] = dz;
    // GainAndBias identity (0.5,0.5) is the default in the cook; leave unset.
    g.nodes.push_back(gen);
    Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
    g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
    EvaluationContext ctx{};
    ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));
  };

  // --- Pass 1: Pivot=0 -> line runs Center .. Center+Dir*Len, colinear, evenly spaced ---
  cookWithPivot(0.0f);
  std::vector<SwPoint> atZero = captured;

  bool colinear = atZero.size() == N;
  float maxPerp = 0.0f;
  for (const SwPoint& p : atZero) {
    // vector from Center to point; perpendicular component to unit Direction.
    float vx = p.Position.x - cx, vy = p.Position.y - cy, vz = p.Position.z - cz;
    float along = vx * dx + vy * dy + vz * dz;            // projection onto Direction
    float px = vx - along * dx, py = vy - along * dy, pz = vz - along * dz;
    float perp = std::sqrt(px * px + py * py + pz * pz);
    if (perp > maxPerp) maxPerp = perp;
  }
  colinear = colinear && maxPerp < 1e-3f;

  // projections onto Direction, in index order: expect arithmetic 0 .. LEN.
  bool evenSpan = atZero.size() == N;
  float firstProj = 0.0f, lastProj = 0.0f, minGap = 1e9f, maxGap = -1e9f;
  if (atZero.size() == N) {
    auto proj = [&](const SwPoint& p) {
      return (p.Position.x - cx) * dx + (p.Position.y - cy) * dy + (p.Position.z - cz) * dz;
    };
    firstProj = proj(atZero[0]);
    lastProj = proj(atZero[N - 1]);
    for (uint32_t k = 1; k < N; ++k) {
      float g = proj(atZero[k]) - proj(atZero[k - 1]);
      if (g < minGap) minGap = g;
      if (g > maxGap) maxGap = g;
    }
  }
  float span = lastProj - firstProj;
  // even = all consecutive gaps within tolerance of each other AND span == Length.
  bool spacingEven = evenSpan && std::fabs(maxGap - minGap) < 1e-3f && minGap > 1e-4f;
  bool spansLength = std::fabs(firstProj) < 1e-3f && std::fabs(span - LEN) < 1e-2f;

  // --- Pass 2: Pivot=0.5 -> line centered on Center (mean projection ~= 0) ---
  cookWithPivot(0.5f);
  std::vector<SwPoint> atHalf = captured;
  float meanProj = 0.0f;
  bool centeredOK = atHalf.size() == N;
  if (atHalf.size() == N) {
    for (const SwPoint& p : atHalf)
      meanProj += (p.Position.x - cx) * dx + (p.Position.y - cy) * dy + (p.Position.z - cz) * dz;
    meanProj /= (float)N;
    centeredOK = std::fabs(meanProj) < 1e-2f;
  }

  bool pass = colinear && spacingEven && spansLength && centeredOK;
  printf("[selftest-linepoints] n=%zu colinear(maxPerp=%.5f)=%d evenSpacing(gap=%.4f..%.4f)=%d "
         "spansLen(span=%.3f need~%.1f)=%d pivotCentered(mean=%.4f)=%d -> %s\n",
         captured.size(), maxPerp, colinear ? 1 : 0, minGap, maxGap, spacingEven ? 1 : 0,
         span, LEN, spansLength ? 1 : 0, meanProj, centeredOK ? 1 : 0, pass ? "PASS" : "FAIL");

  g_lineCap = nullptr;
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
