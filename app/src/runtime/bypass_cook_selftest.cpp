// Headless RED->GREEN proof of S2 bypass on the GPU cook paths (修B, 批次8): a bypassed ATOMIC
// node's MAIN output passes its MAIN input's upstream value through on the buffer (Points),
// Command and Texture2D flows of cookResident — the EXECUTOR half of the honest whitelist rule
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
//   Leg R (cycle)     修2 (批次8): a bypass redirect CYCLE (A↔B mutually bypassed) on the buffer
//                     and command flows fail-safes at the depth cap (null buffer / empty chain,
//                     one stderr warn) instead of the pre-修2 bare-recursion stack overflow.
// COMPOUND child bypass (the 批次8 leg X "flag must be inert" contract) FLIPPED in 批次9 修C:
// the cook-level compound legs live in --selftest-bypasscompound (app/bypass_compound_selftest.
// cpp); the stub kit is shared (bypass_selftest_shared). injectBug emulates a cook that ignores
// the bypass flag (clears it on the leg-P resident node before cooking, = the pre-修B reality)
// so the passthrough assertion FAILS (teeth).
#include "runtime/point_graph.h"

#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/bypass_selftest_shared.h"  // stub kit + capture globals + symbol shapes
#include "runtime/compound_graph.h"          // SymbolLibrary / childIsBypassable
#include "runtime/render_command.h"          // RenderCommand / RenderDrawItem
#include "runtime/resident_eval_graph.h"     // buildEvalGraph / ResidentEvalGraph
#include "runtime/tixl_point.h"              // SwPoint + EvaluationContext

namespace sw {
namespace {

using namespace bypass_st;

int fail(const char* msg) {
  std::printf("[selftest-bypasscook] FAIL: %s\n", msg);
  return 1;
}

}  // namespace

int runBypassCookSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  installStubs();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  int rc = 0;

  // ===== Leg P: Points bypass — upstream buffer passes through point-for-point =====
  {
    SymbolLibrary lib;
    lib.symbols["RadialPoints"] = symGen();
    lib.symbols["ParticleSystem"] = symMod();
    lib.symbols["DrawPoints"] = symDraw();
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
      if (bag[i].Position.x != genX((int)i) || bag[i].Position.y != genY((int)i) ||
          bag[i].Position.z != genZ((int)i)) {
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
      mutated = bag2[i].Position.x == genX((int)i) * 2.0f;
    if (!mutated) rc |= fail("leg P contrast: un-bypassed mod did not cook/mutate");
  }

  // ===== Leg C: Command bypass — upstream chain passes through unchanged =====
  {
    SymbolLibrary lib;
    lib.symbols["RadialPoints"] = symGen();
    lib.symbols["DrawPoints"] = symDraw();
    lib.symbols["CmdJam"] = symJam();
    lib.symbols["RenderTarget"] = symRT();
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
    lib.symbols["RadialPoints"] = symGen();
    lib.symbols["DrawPoints"] = symDraw();
    lib.symbols["RenderTarget"] = symRT();
    lib.symbols["TexFilter"] = symFilter();
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

  // ===== Leg R: bypass redirect cycle -> safe fail, no crash (修2, 批次8) =====
  // Pre-修2 both flows recursed bare (cookNode's memo lands only after the recursion returns;
  // cookCommand has no memo at all) = stack overflow. The contract is "returns, with safe empty
  // output" — REACHING the assertions below is the load-bearing half of the proof.
  {
    // Buffer flow: A↔B mutually bypassed Points ops, pulled by a Draw terminal.
    SymbolLibrary lib;
    lib.symbols["RadialPoints"] = symGen();
    lib.symbols["ParticleSystem"] = symMod();
    lib.symbols["DrawPoints"] = symDraw();
    Symbol root; root.id = "Root"; root.name = "Root";
    SymbolChild a; a.id = 1; a.symbolId = "ParticleSystem"; a.isBypassed = true;
    SymbolChild b; b.id = 2; b.symbolId = "ParticleSystem"; b.isBypassed = true;
    SymbolChild cd; cd.id = 3; cd.symbolId = "DrawPoints";
    root.children = {a, b, cd};
    root.connections = {{2, "result", 1, "emit"}, {1, "result", 2, "emit"},  // the A↔B cycle
                        {1, "result", 3, "points"}};
    lib.symbols["Root"] = root; lib.rootId = "Root";
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    std::vector<SwPoint> bag; g_bag = &bag; g_modRuns = 0; g_rtRuns = 0; g_chain = RenderCommand{};
    pg.cookResident(rg, ctx, nullptr, "3");
    g_bag = nullptr;
    if (g_modRuns != 0) rc |= fail("leg R: a bypassed op in the cycle cooked");
    if (!bag.empty()) rc |= fail("leg R: cycle produced a bag (expected null buffer / empty)");
    if (g_rtRuns != 1 || g_chain.items.size() != 1 || g_chain.items[0].points != nullptr ||
        g_chain.items[0].count != 0)
      rc |= fail("leg R: draw over the cycle did not yield the safe null/0 item");
  }
  {
    // Command flow: jam1↔jam2 mutually bypassed Command ops, pulled by a RenderTarget terminal.
    SymbolLibrary lib;
    lib.symbols["CmdJam"] = symJam();
    lib.symbols["RenderTarget"] = symRT();
    Symbol root; root.id = "Root"; root.name = "Root";
    SymbolChild j1; j1.id = 1; j1.symbolId = "CmdJam"; j1.isBypassed = true;
    SymbolChild j2; j2.id = 2; j2.symbolId = "CmdJam"; j2.isBypassed = true;
    SymbolChild rt; rt.id = 3; rt.symbolId = "RenderTarget";
    root.children = {j1, j2, rt};
    root.connections = {{2, "out", 1, "command"}, {1, "out", 2, "command"},  // the cycle
                        {1, "out", 3, "command"}};
    lib.symbols["Root"] = root; lib.rootId = "Root";
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    g_chain = RenderCommand{}; g_jamRuns = 0; g_rtRuns = 0;
    pg.cookResident(rg, ctx, nullptr, "3");
    if (g_jamRuns != 0) rc |= fail("leg R cmd: a bypassed cmd op in the cycle cooked");
    if (g_rtRuns != 1) rc |= fail("leg R cmd: tex terminal did not execute");
    if (!g_chain.items.empty())
      rc |= fail("leg R cmd: cycle produced chain items (expected the safe empty chain)");
  }

  removeStubSpecs();
  std::printf("[selftest-bypasscook] P/C/T passthrough + contrasts + cycle-cap -> %s\n",
              rc == 0 ? "PASS" : "FAIL");
  q->release(); dev->release(); pool->release();
  return rc;
}

}  // namespace sw
