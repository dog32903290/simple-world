// Headless proof that cookResident (resident-graph walk) == cook (flat-graph walk). Mirrors
// point_graph_selftest.cpp: CPU-fill stub ops under real type names, a capture cmd op grabs the
// terminal bag. Builds a SymbolLibrary (gen -> mod -> capture, with a reuse sibling) AND the
// equivalent flat Graph; cooks both; asserts the captured bags are byte-identical. injectBug
// flips the resident expectation so a regression in the walk FAILS.
#include "runtime/point_graph.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"      // SymbolLibrary / Symbol / SymbolChild / SymbolConnection
#include "runtime/graph.h"               // Graph / Node / pinId
#include "runtime/resident_eval_graph.h" // buildEvalGraph / ResidentEvalGraph
#include "runtime/tixl_point.h"          // SwPoint + EvaluationContext

namespace sw {
namespace {

std::vector<SwPoint>* g_resCap = nullptr;   // capture target for whichever cook runs
bool g_resBug = false;

// Generator: fill `count` points, Position.x = 1.
void rcGen(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  for (uint32_t i = 0; i < c.count; ++i) { dst[i] = SwPoint{}; dst[i].Position = {1.0f, 0.0f, 0.0f}; }
}
// Modifier: copy input[0] -> output, Position.x *= 2 (proves input threading + count).
void rcMul(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  const MTL::Buffer* in0 = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  const SwPoint* src = in0 ? (const SwPoint*)const_cast<MTL::Buffer*>(in0)->contents() : nullptr;
  for (uint32_t i = 0; i < c.count; ++i) {
    if (!src) { dst[i] = SwPoint{}; continue; }
    dst[i] = src[i];
    dst[i].Position.x = src[i].Position.x * 2.0f;
  }
}
// Capture cmd op: memcpy the upstream bag into g_resCap.
RenderCommand rcCapture(CmdCookCtx& c) {
  RenderCommand rc;
  if (g_resCap && c.points && c.count > 0) {
    g_resCap->assign(c.count, SwPoint{});
    std::memcpy(g_resCap->data(), const_cast<MTL::Buffer*>(c.points)->contents(),
                (size_t)c.count * sizeof(SwPoint));
  }
  return rc;
}

// atomic symbol whose id == a registered op type; ins/outs are the op's buffer/float slots.
Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

}  // namespace

int runResidentCookSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  g_resBug = injectBug;

  registerPointOp("RadialPoints", rcGen);
  registerPointOp("ParticleSystem", rcMul);
  registerCmdOp("DrawPoints", rcCapture);

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  // --- FLAT reference: RadialPoints(Count=8) -> ParticleSystem -> DrawPoints ---
  Graph fg;
  Node fgN; fgN.id = 1; fgN.type = "RadialPoints"; fgN.params["Count"] = 8.0f; fg.nodes.push_back(fgN);
  Node fmN; fmN.id = 2; fmN.type = "ParticleSystem"; fg.nodes.push_back(fmN);
  Node fdN; fdN.id = 3; fdN.type = "DrawPoints"; fg.nodes.push_back(fdN);
  fg.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // gen.points -> mod.emit
  fg.connections.push_back({102, pinId(2, 2), pinId(3, 0)});  // mod.result -> draw.points
  std::vector<SwPoint> flatBag; g_resCap = &flatBag;
  pg.cook(fg, ctx, nullptr, pg.defaultDrawTarget(fg));

  // --- RESIDENT: equivalent SymbolLibrary, root holds gen/mod/draw children + same wiring ---
  // atomic op symbols mirror the NodeSpec ports the cook walks (Count Float + Points buffers).
  SymbolLibrary lib;
  lib.symbols["RadialPoints"] = atomicOp("RadialPoints", {{"Count", "Count", "Float", 8.0f}},
                                         {{"points", "points", "Points", 0.0f}});
  lib.symbols["ParticleSystem"] = atomicOp("ParticleSystem",
      {{"emit", "emit", "Points", 0.0f}, {"forces", "forces", "ParticleForce", 0.0f}},
      {{"result", "result", "Points", 0.0f}});
  lib.symbols["DrawPoints"] = atomicOp("DrawPoints", {{"points", "points", "Points", 0.0f}},
                                       {{"out", "out", "Command", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Command", 0.0f}};
  SymbolChild cg; cg.id = 1; cg.symbolId = "RadialPoints";  // Count default 8 (no override)
  SymbolChild cm; cm.id = 2; cm.symbolId = "ParticleSystem";
  SymbolChild cd; cd.id = 3; cd.symbolId = "DrawPoints";
  root.children = {cg, cm, cd};
  root.connections = {
      {1, "points", 2, "emit"},                 // gen.points -> mod.emit
      {2, "result", 3, "points"},               // mod.result -> draw.points
      {3, "out", kSymbolBoundary, "out"},        // draw.out -> root output
  };
  lib.symbols["Root"] = root; lib.rootId = "Root";

  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  std::vector<SwPoint> resBag; g_resCap = &resBag;
  pg.cookResident(rg, ctx, nullptr, /*targetPath=*/"3");  // DrawPoints resident node path

  // Compare: same count (8) and same per-point Position.x (1 * 2 = 2). injectBug flips the
  // expectation so any real divergence (or the deliberate bug) FAILS.
  bool sizeOk = (flatBag.size() == 8) && (resBag.size() == flatBag.size());
  bool valOk = sizeOk;
  for (size_t i = 0; i < resBag.size() && i < flatBag.size(); ++i)
    valOk = valOk && (resBag[i].Position.x == flatBag[i].Position.x) && (resBag[i].Position.x == 2.0f);
  bool match = sizeOk && valOk;
  if (injectBug) match = !match;  // bug variant must observe a MISMATCH to "pass" its RED intent

  float fx = flatBag.empty() ? -1.0f : flatBag[0].Position.x;
  float rx = resBag.empty() ? -1.0f : resBag[0].Position.x;
  printf("[selftest-residentcook] flat=%zu(x=%.1f) resident=%zu(x=%.1f) match=%d -> %s\n",
         flatBag.size(), fx, resBag.size(), rx, (sizeOk && valOk), match ? "PASS" : "FAIL");

  g_resCap = nullptr;
  q->release(); dev->release(); pool->release();
  return match ? 0 : 1;
}

}  // namespace sw
