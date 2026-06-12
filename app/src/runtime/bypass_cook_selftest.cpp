// Headless RED->GREEN proof of S2 bypass on the GPU cook paths (修B, 批次8): a bypassed node's
// MAIN output passes its MAIN input's upstream value through on the buffer (Points), Command and
// Texture2D flows of cookResident — the EXECUTOR half of the honest whitelist rule
// (compoundBypassableType: a type is whitelisted only when the executor passes through too).
// Float was proven on the value paths (--selftest-childstate leg 1); this file proves the other
// three. Semantics = TiXL Slot.ByPassUpdate (Slot.cs:176-179: Value = main input's GetValue),
// executed per-type by Instance.Connections.cs SetBypassFor's switch (:265-389).
//   Leg P (Points)    gen -> mod(bypassed) -> capture: the captured bag == gen's bag point-for-
//                     point (positions byte-identical, count carried); un-bypassed contrast: the
//                     mod's x*2 shows the FLAG is what did it.
//   Leg C (Command)   DrawPoints -> CmdJam(bypassed) -> RenderTarget terminal: the chain the
//                     executor receives IS DrawPoints' item (buffer identity + count + extent) —
//                     the upstream command list passes through unchanged, CmdJam's own cmd fn
//                     never runs; un-bypassed contrast: CmdJam's garbage item arrives instead.
//   Leg T (Texture2D) gen -> DrawPoints -> RenderTarget -> TexFilter(bypassed terminal): the
//                     displayed texture is the UPSTREAM RenderTarget's own texture and the
//                     filter's tex fn never runs; un-bypassed contrast: the filter cooks and its
//                     own texture is displayed.
// Test-only op shapes with no builtin spec (CmdJam Command->Command, TexFilter Texture2D->
// Texture2D) get their NodeSpec via specFromSymbol + setDynamicSpecs (blessed for headless
// selftests, graph.h). injectBug emulates a cook that ignores the bypass flag (clears it on the
// leg-P resident node before cooking, = the pre-修B reality) so the passthrough assertion FAILS.
#include "runtime/point_graph.h"

#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"       // SymbolLibrary / Symbol / SymbolChild / childIsBypassable
#include "runtime/graph.h"                // NodeSpec / setDynamicSpecs
#include "runtime/graph_bridge.h"         // specFromSymbol (canonical Symbol -> NodeSpec)
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / ResidentEvalGraph
#include "runtime/tixl_point.h"           // SwPoint + EvaluationContext

namespace sw {
namespace {

// --- capture globals (one selftest process; reset per leg) ---
std::vector<SwPoint>* g_bag = nullptr;       // leg P: the bag DrawPoints saw
const MTL::Buffer* g_drawSeenBuf = nullptr;  // leg C: the buffer DrawPoints' item borrows
RenderCommand g_chain;                       // leg C/T: the chain the RenderTarget executor got
MTL::Texture* g_rtTex = nullptr;             // leg T: the texture RenderTarget cooked into
MTL::Texture* g_filterTex = nullptr;         // leg T: the texture TexFilter cooked into
int g_rtRuns = 0, g_filterRuns = 0, g_jamRuns = 0, g_modRuns = 0;

// Generator stub: per-point DISTINCT positions so "passes through point-for-point" is a real
// claim (an all-equal fill would pass under reordering/recount bugs).
void bpGen(PointCookCtx& c) {
  if (!c.output || c.count == 0) return;
  SwPoint* dst = (SwPoint*)c.output->contents();
  for (uint32_t i = 0; i < c.count; ++i) {
    dst[i] = SwPoint{};
    dst[i].Position = {1.0f + 10.0f * (float)i, 0.5f * (float)i, (float)i};
  }
}
// Modifier stub: x *= 2 — the mutation a working bypass must SKIP.
void bpMul(PointCookCtx& c) {
  ++g_modRuns;
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
// DrawPoints stub: capture the upstream bag (leg P) + emit a recognizable 1-item chain (leg C).
RenderCommand bpDraw(CmdCookCtx& c) {
  g_drawSeenBuf = c.points;
  if (g_bag && c.points && c.count > 0) {
    g_bag->assign(c.count, SwPoint{});
    std::memcpy(g_bag->data(), const_cast<MTL::Buffer*>(c.points)->contents(),
                (size_t)c.count * sizeof(SwPoint));
  }
  RenderCommand rc;
  rc.items.push_back(RenderDrawItem{c.points, c.count, 7.5f});
  return rc;
}
// CmdJam stub (Command -> Command): a cmd op whose OWN output is unmistakable garbage. A working
// Command bypass means this never runs and the upstream chain arrives instead.
RenderCommand bpJam(CmdCookCtx&) {
  ++g_jamRuns;
  RenderCommand rc;
  rc.items.push_back(RenderDrawItem{nullptr, 12345u, 1.0f});
  return rc;
}
// RenderTarget stub (tex executor): record the chain + the texture it was asked to fill.
void bpRenderTarget(TexCookCtx& c) {
  ++g_rtRuns;
  if (c.command) g_chain = *c.command;
  g_rtTex = c.output;
}
// TexFilter stub (Texture2D -> Texture2D): a tex op a working Texture2D bypass must SKIP.
void bpTexFilter(TexCookCtx& c) {
  ++g_filterRuns;
  g_filterTex = c.output;
}

Symbol atomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

int fail(const char* msg) {
  std::printf("[selftest-bypasscook] FAIL: %s\n", msg);
  return 1;
}

}  // namespace

int runBypassCookSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  // Stubs under real builtin spec names (cook walks the NodeSpec ports) + the two test-only
  // shapes whose specs are injected below.
  registerPointOp("RadialPoints", bpGen);
  registerPointOp("ParticleSystem", bpMul);
  registerCmdOp("DrawPoints", bpDraw);
  registerCmdOp("CmdJam", bpJam);
  registerTexOp("RenderTarget", bpRenderTarget);
  registerTexOp("TexFilter", bpTexFilter);

  // Shared atomic symbol shapes (= the whitelist gate's input: childIsBypassable reads THESE).
  const Symbol symGen = atomicOp("RadialPoints", {{"Count", "Count", "Float", 6.0f}},
                                 {{"points", "points", "Points", 0.0f}});
  const Symbol symMod = atomicOp("ParticleSystem",
      {{"emit", "emit", "Points", 0.0f}, {"forces", "forces", "ParticleForce", 0.0f}},
      {{"result", "result", "Points", 0.0f}});
  const Symbol symDraw = atomicOp("DrawPoints", {{"points", "points", "Points", 0.0f}},
                                  {{"out", "out", "Command", 0.0f}});
  const Symbol symJam = atomicOp("CmdJam", {{"command", "command", "Command", 0.0f}},
                                 {{"out", "out", "Command", 0.0f}});
  const Symbol symRT = atomicOp("RenderTarget", {{"command", "command", "Command", 0.0f}},
                                {{"out", "out", "Texture2D", 0.0f}});
  const Symbol symFilter = atomicOp("TexFilter", {{"tex", "tex", "Texture2D", 0.0f}},
                                    {{"out", "out", "Texture2D", 0.0f}});

  // Inject NodeSpecs for the shapes with no builtin (canonical builder, blessed for selftests).
  {
    std::map<std::string, NodeSpec> dyn;
    dyn["CmdJam"] = specFromSymbol(symJam);
    dyn["TexFilter"] = specFromSymbol(symFilter);
    setDynamicSpecs(std::move(dyn));
  }

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  int rc = 0;

  // ===== Leg P: Points bypass — upstream buffer passes through point-for-point =====
  {
    SymbolLibrary lib;
    lib.symbols["RadialPoints"] = symGen;
    lib.symbols["ParticleSystem"] = symMod;
    lib.symbols["DrawPoints"] = symDraw;
    Symbol root; root.id = "Root"; root.name = "Root";
    SymbolChild cg; cg.id = 1; cg.symbolId = "RadialPoints";
    SymbolChild cm; cm.id = 2; cm.symbolId = "ParticleSystem"; cm.isBypassed = true;
    SymbolChild cd; cd.id = 3; cd.symbolId = "DrawPoints";
    root.children = {cg, cm, cd};
    root.connections = {{1, "points", 2, "emit"}, {2, "result", 3, "points"}};
    lib.symbols["Root"] = root; lib.rootId = "Root";
    if (!childIsBypassable(lib, root.children[1]))
      rc |= fail("leg P: Points main I/O not whitelisted (compoundBypassableType regressed)");

    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    if (injectBug) {
      // Emulate a cook that ignores the flag (the pre-修B broken knob): clear it on the resident
      // node — the mod cooks, x*2 lands, the passthrough assertion below FAILS (teeth).
      for (ResidentNode& n : rg.nodes)
        if (n.path == "2") n.bypassed = false;
    }
    std::vector<SwPoint> bag; g_bag = &bag; g_modRuns = 0;
    pg.cookResident(rg, ctx, nullptr, "3");
    g_bag = nullptr;
    if (bag.size() != 6) rc |= fail("leg P: bypassed count != upstream count (6)");
    for (size_t i = 0; i < bag.size(); ++i) {
      const float ex = 1.0f + 10.0f * (float)i;
      if (bag[i].Position.x != ex || bag[i].Position.y != 0.5f * (float)i ||
          bag[i].Position.z != (float)i) {
        rc |= fail("leg P: bypassed bag != upstream bag point-for-point (mod mutated it?)");
        break;
      }
    }
    if (g_modRuns != 0) rc |= fail("leg P: bypassed op still cooked (its fn ran)");

    // Contrast: same lib, flag OFF -> the mod cooks and x*2 shows up (proves the FLAG did it).
    lib.symbols["Root"].children[1].isBypassed = false;
    ResidentEvalGraph rg2 = buildEvalGraph(lib, "Root");
    std::vector<SwPoint> bag2; g_bag = &bag2; g_modRuns = 0;
    pg.cookResident(rg2, ctx, nullptr, "3");
    g_bag = nullptr;
    bool mutated = bag2.size() == 6 && g_modRuns == 1;
    for (size_t i = 0; mutated && i < bag2.size(); ++i)
      mutated = bag2[i].Position.x == (1.0f + 10.0f * (float)i) * 2.0f;
    if (!mutated) rc |= fail("leg P contrast: un-bypassed mod did not cook/mutate");
  }

  // ===== Leg C: Command bypass — upstream chain passes through unchanged =====
  {
    SymbolLibrary lib;
    lib.symbols["RadialPoints"] = symGen;
    lib.symbols["DrawPoints"] = symDraw;
    lib.symbols["CmdJam"] = symJam;
    lib.symbols["RenderTarget"] = symRT;
    Symbol root; root.id = "Root"; root.name = "Root";
    SymbolChild cg; cg.id = 1; cg.symbolId = "RadialPoints";
    SymbolChild cd; cd.id = 2; cd.symbolId = "DrawPoints";
    SymbolChild cj; cj.id = 3; cj.symbolId = "CmdJam"; cj.isBypassed = true;
    SymbolChild cr; cr.id = 4; cr.symbolId = "RenderTarget";
    root.children = {cg, cd, cj, cr};
    root.connections = {{1, "points", 2, "points"}, {2, "out", 3, "command"},
                        {3, "out", 4, "command"}};
    lib.symbols["Root"] = root; lib.rootId = "Root";
    if (!childIsBypassable(lib, root.children[2]))
      rc |= fail("leg C: Command main I/O not whitelisted");

    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    g_chain = RenderCommand{}; g_drawSeenBuf = nullptr; g_jamRuns = 0; g_rtRuns = 0;
    pg.cookResident(rg, ctx, nullptr, "4");
    if (g_rtRuns != 1) rc |= fail("leg C: tex terminal did not execute");
    if (g_jamRuns != 0) rc |= fail("leg C: bypassed cmd op still cooked (its fn ran)");
    if (g_chain.items.size() != 1) rc |= fail("leg C: chain item count != 1 (passthrough lost/duped)");
    else if (g_chain.items[0].points != g_drawSeenBuf || g_chain.items[0].count != 6 ||
             g_chain.items[0].viewExtent != 7.5f)
      rc |= fail("leg C: executor chain != DrawPoints' item (upstream list did not pass through)");

    // Contrast: flag OFF -> CmdJam's own garbage item reaches the executor.
    lib.symbols["Root"].children[2].isBypassed = false;
    ResidentEvalGraph rg2 = buildEvalGraph(lib, "Root");
    g_chain = RenderCommand{}; g_jamRuns = 0;
    pg.cookResident(rg2, ctx, nullptr, "4");
    if (!(g_jamRuns == 1 && g_chain.items.size() == 1 && g_chain.items[0].points == nullptr &&
          g_chain.items[0].count == 12345u))
      rc |= fail("leg C contrast: un-bypassed cmd op's own item did not arrive");
  }

  // ===== Leg T: Texture2D bypass — the upstream texture producer is displayed =====
  {
    SymbolLibrary lib;
    lib.symbols["RadialPoints"] = symGen;
    lib.symbols["DrawPoints"] = symDraw;
    lib.symbols["RenderTarget"] = symRT;
    lib.symbols["TexFilter"] = symFilter;
    Symbol root; root.id = "Root"; root.name = "Root";
    SymbolChild cg; cg.id = 1; cg.symbolId = "RadialPoints";
    SymbolChild cd; cd.id = 2; cd.symbolId = "DrawPoints";
    SymbolChild cr; cr.id = 3; cr.symbolId = "RenderTarget";
    SymbolChild cf; cf.id = 4; cf.symbolId = "TexFilter"; cf.isBypassed = true;
    root.children = {cg, cd, cr, cf};
    root.connections = {{1, "points", 2, "points"}, {2, "out", 3, "command"},
                        {3, "out", 4, "tex"}};
    lib.symbols["Root"] = root; lib.rootId = "Root";
    if (!childIsBypassable(lib, root.children[3]))
      rc |= fail("leg T: Texture2D main I/O not whitelisted");

    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    g_rtTex = nullptr; g_filterTex = nullptr; g_rtRuns = 0; g_filterRuns = 0;
    pg.cookResident(rg, ctx, nullptr, "4");
    if (g_filterRuns != 0) rc |= fail("leg T: bypassed tex op still cooked (its fn ran)");
    if (g_rtRuns != 1) rc |= fail("leg T: upstream RenderTarget did not cook as the terminal");
    if (!g_rtTex || pg.target() != g_rtTex)
      rc |= fail("leg T: displayed texture != upstream RenderTarget's texture");
    MTL::Texture* rtTexBypassed = g_rtTex;  // the upstream's own texture, for the contrast compare

    // Contrast: flag OFF -> TexFilter cooks as the terminal, its OWN texture is displayed.
    // (The upstream RenderTarget does NOT cook here — Texture2D inputs aren't gathered, textures
    // exist only at the terminal — so only the filter's run counter and texture identity matter.)
    lib.symbols["Root"].children[3].isBypassed = false;
    ResidentEvalGraph rg2 = buildEvalGraph(lib, "Root");
    g_filterTex = nullptr; g_filterRuns = 0;
    pg.cookResident(rg2, ctx, nullptr, "4");
    if (!(g_filterRuns == 1 && g_filterTex && pg.target() == g_filterTex &&
          g_filterTex != rtTexBypassed))
      rc |= fail("leg T contrast: un-bypassed tex op did not cook/display its own texture");
  }

  setDynamicSpecs({});  // drop the injected test specs (leave the table as the process found it)
  std::printf("[selftest-bypasscook] Points/Command/Texture2D passthrough + contrasts -> %s\n",
              rc == 0 ? "PASS" : "FAIL");
  q->release(); dev->release(); pool->release();
  return rc;
}

}  // namespace sw
