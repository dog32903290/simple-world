// Headless RED->GREEN proof of 修C (批次9): cook-level COMPOUND child bypass. = TiXL SetBypassFor
// on a composition Instance (Instance.Connections.cs:265-267: Outputs[0].TrySetBypassToInput(
// Inputs[0]); Slot.cs:176-179 ByPassUpdate) — the subgraph behind the main output is never
// pulled. Our flattened analog (resident_eval_graph.cpp inlineSymbol): a bypassed compound child
// is REWIRED away — consumers of its main output adopt the driver feeding its main input, and the
// subgraph leaves ZERO resident footprint (no nodes, no cook, no per-path state). This file flips
// the 批次8 leg-X contract ("compound flag must be inert") and proves the production entries:
//   Leg 1  Points->Points compound bypass effective: bag passes through point-for-point, the
//          inner mod NEVER runs, the subgraph has zero resident paths; contrast: flag off ->
//          the compound inlines and the mod's x*2 lands (the FLAG did it).
//   Leg 2  save -> load -> cook roundtrip: compound isBypassed survives .swproj v2 and the
//          loaded lib cooks the same passthrough.
//   Leg 3  Command->Command and Texture2D->Texture2D BOUNDARY compounds — the production-only
//          entry for these types (no same-typed atomic exists): the executor receives the
//          upstream chain / the upstream producer's texture; viewProducerPath steps SIDEWAYS to
//          the redirect target (the view seam, main.cpp).
//   Leg 4  nesting: an un-bypassed compound containing a bypassed compound child passes through
//          both boundaries, zero inner footprint.
//   Leg 5  SetBypassChildCommand on a compound child: no longer refused (the 修B atomic-only
//          收窄 is lifted); doIt/undo/redo each project the right cook through the rebuild path
//          (frame_cook rebuilds from lib on every revision — undo cannot strand the projection).
// App zone (leg 5 needs the command layer; app -> runtime is legal). Stub kit shared with
// --selftest-bypasscook (runtime/bypass_selftest_shared). injectBug emulates a flattener that
// ignores the compound flag (clears it before building, = the pre-修C reality) so leg 1's
// passthrough assertions FAIL (teeth).
#include "app/graph_commands.h"

#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/bypass_selftest_shared.h"  // stub kit + capture globals + symbol shapes
#include "runtime/compound_graph.h"          // SymbolLibrary / childIsBypassable / viewProducerPath
#include "runtime/compound_save.h"           // libToJsonV2 / libFromJsonAny (leg 2 roundtrip)
#include "runtime/point_graph.h"             // PointGraph::cookResident
#include "runtime/render_command.h"          // RenderCommand
#include "runtime/resident_eval_graph.h"     // buildEvalGraph
#include "runtime/tixl_point.h"              // SwPoint + EvaluationContext

namespace sw {
namespace {

using namespace bypass_st;

int fail(const char* msg) {
  std::printf("[selftest-bypasscompound] FAIL: %s\n", msg);
  return 1;
}

// Root: gen#1 -> Comp#2 (Points->Points, wraps the x*2 mod) -> draw#3. The compound shape every
// Points leg starts from; `bypassed` sets the flag on the Comp child.
SymbolLibrary makePointsLib(bool bypassed) {
  SymbolLibrary lib;
  lib.symbols["RadialPoints"] = symGen();
  lib.symbols["ParticleSystem"] = symMod();
  lib.symbols["DrawPoints"] = symDraw();
  Symbol comp; comp.id = "Comp"; comp.name = "Comp";
  comp.inputDefs = {{"in", "in", "Points", 0.0f}};
  comp.outputDefs = {{"out", "out", "Points", 0.0f}};
  SymbolChild im; im.id = 1; im.symbolId = "ParticleSystem";
  comp.children = {im};
  comp.connections = {{kSymbolBoundary, "in", 1, "emit"}, {1, "result", kSymbolBoundary, "out"}};
  lib.symbols["Comp"] = comp;
  Symbol root; root.id = "Root"; root.name = "Root";
  SymbolChild cg; cg.id = 1; cg.symbolId = "RadialPoints";
  cg.overrides["Count"] = 6.0f;  // pin count: a reloaded lib re-derives gen's defs from the registry
  SymbolChild cc; cc.id = 2; cc.symbolId = "Comp"; cc.isBypassed = bypassed;
  SymbolChild cd; cd.id = 3; cd.symbolId = "DrawPoints";
  root.children = {cg, cc, cd};
  root.connections = {{1, "points", 2, "in"}, {2, "out", 3, "points"}};
  lib.symbols["Root"] = root; lib.rootId = "Root";
  return lib;
}

// Cook draw#3 and report: did the gen bag arrive UNTOUCHED (passthrough) / DOUBLED (mod cooked)?
struct PointsCook { bool passthrough = false; bool doubled = false; int modRuns = 0; };
PointsCook cookPoints(PointGraph& pg, const SymbolLibrary& lib, const EvaluationContext& ctx) {
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  std::vector<SwPoint> bag; g_bag = &bag; g_modRuns = 0;
  pg.cookResident(rg, ctx, nullptr, "3");
  g_bag = nullptr;
  PointsCook r; r.modRuns = g_modRuns;
  if (bag.size() != 6) return r;
  r.passthrough = r.doubled = true;
  for (size_t i = 0; i < bag.size(); ++i) {
    const bool yz = bag[i].Position.y == genY((int)i) && bag[i].Position.z == genZ((int)i);
    r.passthrough = r.passthrough && yz && bag[i].Position.x == genX((int)i);
    r.doubled = r.doubled && yz && bag[i].Position.x == genX((int)i) * 2.0f;
  }
  return r;
}

}  // namespace

int runBypassCompoundSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  installStubs();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  int rc = 0;

  // ===== Leg 1: Points->Points compound bypass effective + zero footprint (leg X flipped) =====
  {
    SymbolLibrary lib = makePointsLib(true);
    if (!childIsBypassable(lib, lib.symbols["Root"].children[1]))
      rc |= fail("leg 1: compound child not bypassable (the 修B 收窄 is still in place)");
    if (injectBug) {
      // Emulate a flattener that ignores the compound flag (the pre-修C reality): the subgraph
      // inlines, the mod cooks, x*2 lands -> the passthrough assertions FAIL (teeth).
      lib.symbols["Root"].children[1].isBypassed = false;
    }
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    if (rg.node("2") || rg.node("2/1"))
      rc |= fail("leg 1: bypassed compound left a resident footprint (node 2 or 2/1 exists)");
    PointsCook r = cookPoints(pg, lib, ctx);
    if (!r.passthrough) rc |= fail("leg 1: bag != gen bag point-for-point (passthrough broken)");
    if (r.modRuns != 0) rc |= fail("leg 1: inner mod cooked (zero-cook contract broken)");

    // Contrast: flag OFF -> the compound inlines (node 2/1 exists) and the mod mutates.
    SymbolLibrary lib2 = makePointsLib(false);
    ResidentEvalGraph rg2 = buildEvalGraph(lib2, "Root");
    if (!rg2.node("2/1")) rc |= fail("leg 1 contrast: un-bypassed compound did not inline");
    PointsCook r2 = cookPoints(pg, lib2, ctx);
    if (!(r2.doubled && r2.modRuns == 1))
      rc |= fail("leg 1 contrast: un-bypassed inner mod did not cook/mutate");
  }

  // ===== Leg 2: save -> load -> cook roundtrip (.swproj v2 carries compound isBypassed) =====
  {
    SymbolLibrary lib = makePointsLib(true);
    SymbolLibrary back;
    std::vector<std::string> warn;
    if (!libFromJsonAny(libToJsonV2(lib), back, &warn) || !warn.empty()) {
      rc |= fail("leg 2: v2 roundtrip failed or warned");
    } else {
      const SymbolChild* c = childById(back.symbols["Root"], 2);
      if (!c || !c->isBypassed) rc |= fail("leg 2: compound isBypassed lost in the file");
      else if (!childIsBypassable(back, *c)) rc |= fail("leg 2: reloaded child not bypassable");
      PointsCook r = cookPoints(pg, back, ctx);
      if (!(r.passthrough && r.modRuns == 0))
        rc |= fail("leg 2: reloaded lib did not cook the passthrough (flag inert after load)");
    }
  }

  // ===== Leg 3a: Command->Command boundary compound (the production entry for Command) =====
  {
    SymbolLibrary lib;
    lib.symbols["RadialPoints"] = symGen();
    lib.symbols["DrawPoints"] = symDraw();
    lib.symbols["CmdJam"] = symJam();
    lib.symbols["RenderTarget"] = symRT();
    Symbol comp; comp.id = "CmdComp"; comp.name = "CmdComp";
    comp.inputDefs = {{"in", "in", "Command", 0.0f}};
    comp.outputDefs = {{"out", "out", "Command", 0.0f}};
    SymbolChild ij; ij.id = 1; ij.symbolId = "CmdJam";
    comp.children = {ij};
    comp.connections = {{kSymbolBoundary, "in", 1, "command"}, {1, "out", kSymbolBoundary, "out"}};
    lib.symbols["CmdComp"] = comp;
    Symbol root; root.id = "Root"; root.name = "Root";
    SymbolChild cg; cg.id = 1; cg.symbolId = "RadialPoints";
    SymbolChild cd; cd.id = 2; cd.symbolId = "DrawPoints";
    SymbolChild cc; cc.id = 3; cc.symbolId = "CmdComp"; cc.isBypassed = true;
    SymbolChild cr; cr.id = 4; cr.symbolId = "RenderTarget";
    root.children = {cg, cd, cc, cr};
    root.connections = {{1, "points", 2, "points"}, {2, "out", 3, "in"}, {3, "out", 4, "command"}};
    lib.symbols["Root"] = root; lib.rootId = "Root";
    if (!childIsBypassable(lib, root.children[2]))
      rc |= fail("leg 3a: Command->Command compound not bypassable");

    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    if (rg.node("3/1")) rc |= fail("leg 3a: bypassed cmd compound left a resident footprint");
    g_chain = RenderCommand{}; g_drawSeenBuf = nullptr; g_jamRuns = 0; g_rtRuns = 0;
    pg.cookResident(rg, ctx, nullptr, "4");
    if (g_rtRuns != 1) rc |= fail("leg 3a: tex terminal did not execute");
    if (g_jamRuns != 0) rc |= fail("leg 3a: inner cmd op cooked through the bypass");
    if (!(g_chain.items.size() == 1 && g_chain.items[0].points == g_drawSeenBuf &&
          g_chain.items[0].count == 6 && g_chain.items[0].viewExtent == 7.5f))
      rc |= fail("leg 3a: executor chain != DrawPoints' item (upstream did not pass through)");

    // Contrast: flag OFF -> the inner jam inlines and its garbage item reaches the executor.
    lib.symbols["Root"].children[2].isBypassed = false;
    ResidentEvalGraph rg2 = buildEvalGraph(lib, "Root");
    g_chain = RenderCommand{}; g_jamRuns = 0;
    pg.cookResident(rg2, ctx, nullptr, "4");
    if (!(g_jamRuns == 1 && g_chain.items.size() == 1 && g_chain.items[0].count == 12345u))
      rc |= fail("leg 3a contrast: un-bypassed inner cmd op's item did not arrive");
  }

  // ===== Leg 3b: Texture2D->Texture2D boundary compound + the view seam =====
  {
    SymbolLibrary lib;
    lib.symbols["RadialPoints"] = symGen();
    lib.symbols["DrawPoints"] = symDraw();
    lib.symbols["RenderTarget"] = symRT();
    lib.symbols["TexFilter"] = symFilter();
    Symbol comp; comp.id = "TexComp"; comp.name = "TexComp";
    comp.inputDefs = {{"in", "in", "Texture2D", 0.0f}};
    comp.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
    SymbolChild itf; itf.id = 1; itf.symbolId = "TexFilter";
    comp.children = {itf};
    comp.connections = {{kSymbolBoundary, "in", 1, "tex"}, {1, "out", kSymbolBoundary, "out"}};
    lib.symbols["TexComp"] = comp;
    Symbol root; root.id = "Root"; root.name = "Root";
    SymbolChild cg; cg.id = 1; cg.symbolId = "RadialPoints";
    SymbolChild cd; cd.id = 2; cd.symbolId = "DrawPoints";
    SymbolChild cr; cr.id = 3; cr.symbolId = "RenderTarget";
    SymbolChild cc; cc.id = 4; cc.symbolId = "TexComp"; cc.isBypassed = true;
    root.children = {cg, cd, cr, cc};
    root.connections = {{1, "points", 2, "points"}, {2, "out", 3, "command"}, {3, "out", 4, "in"}};
    lib.symbols["Root"] = root; lib.rootId = "Root";
    if (!childIsBypassable(lib, root.children[3]))
      rc |= fail("leg 3b: Texture2D->Texture2D compound not bypassable");

    // The view seam (= the targetPath main.cpp computes when 柏為 views the bypassed compound):
    // a bypassed compound has no inner producer path — viewing it steps SIDEWAYS to the upstream.
    if (viewProducerPath(lib, "", 4) != "3")
      rc |= fail("leg 3b: viewProducerPath did not redirect to the upstream RenderTarget");
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    if (rg.node("4/1")) rc |= fail("leg 3b: bypassed tex compound left a resident footprint");
    g_rtTex = nullptr; g_filterTex = nullptr; g_rtRuns = 0; g_filterRuns = 0;
    pg.cookResident(rg, ctx, nullptr, "3");
    if (g_filterRuns != 0) rc |= fail("leg 3b: inner tex op cooked through the bypass");
    if (g_rtRuns != 1 || !g_rtTex || pg.target() != g_rtTex)
      rc |= fail("leg 3b: displayed texture != upstream RenderTarget's texture");

    // Contrast: flag OFF -> viewing dives INSIDE (4/1) and the inner filter cooks its own texture.
    lib.symbols["Root"].children[3].isBypassed = false;
    if (viewProducerPath(lib, "", 4) != "4/1")
      rc |= fail("leg 3b contrast: un-bypassed view did not resolve to the inner producer");
    ResidentEvalGraph rg2 = buildEvalGraph(lib, "Root");
    g_filterTex = nullptr; g_filterRuns = 0;
    pg.cookResident(rg2, ctx, nullptr, "4/1");
    if (!(g_filterRuns == 1 && g_filterTex && pg.target() == g_filterTex))
      rc |= fail("leg 3b contrast: un-bypassed inner tex op did not cook/display its own texture");
  }

  // ===== Leg 4: nesting — a compound containing a BYPASSED compound child =====
  {
    SymbolLibrary lib = makePointsLib(false);  // brings Comp (Points->Points around the mod)
    Symbol outer; outer.id = "Outer"; outer.name = "Outer";
    outer.inputDefs = {{"in", "in", "Points", 0.0f}};
    outer.outputDefs = {{"out", "out", "Points", 0.0f}};
    SymbolChild ic; ic.id = 1; ic.symbolId = "Comp"; ic.isBypassed = true;  // bypassed INNER compound
    outer.children = {ic};
    outer.connections = {{kSymbolBoundary, "in", 1, "in"}, {1, "out", kSymbolBoundary, "out"}};
    lib.symbols["Outer"] = outer;
    lib.symbols["Root"].children[1].symbolId = "Outer";  // Root: gen -> Outer (not bypassed) -> draw
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    if (rg.node("2/1") || rg.node("2/1/1"))
      rc |= fail("leg 4: bypassed inner compound left a resident footprint");
    PointsCook r = cookPoints(pg, lib, ctx);
    if (!(r.passthrough && r.modRuns == 0))
      rc |= fail("leg 4: nested bypass did not pass through both boundaries");
  }

  // ===== Leg 5: SetBypassChildCommand on a compound child + undo/redo across rebuilds =====
  {
    SymbolLibrary lib = makePointsLib(false);
    SetBypassChildCommand cmd(lib, "Root", 2, true);
    if (cmd.refused()) {
      rc |= fail("leg 5: bypass of a wired compound child was refused (gate not widened)");
    } else {
      cmd.doIt();  // the GUI then bumps the lib revision; frame_cook rebuilds from lib (mirrored
                   // here by cookPoints building fresh) — the projection inherits the redirect.
      PointsCook r = cookPoints(pg, lib, ctx);
      if (!(r.passthrough && r.modRuns == 0)) rc |= fail("leg 5: doIt did not project the bypass");
      cmd.undo();
      if (childById(lib.symbols["Root"], 2)->isBypassed) rc |= fail("leg 5: undo left the flag set");
      PointsCook ru = cookPoints(pg, lib, ctx);
      if (!(ru.doubled && ru.modRuns == 1)) rc |= fail("leg 5: undo did not restore the inline cook");
      cmd.doIt();  // redo = doIt again (command stack contract)
      PointsCook rr = cookPoints(pg, lib, ctx);
      if (!(rr.passthrough && rr.modRuns == 0)) rc |= fail("leg 5: redo did not re-project the bypass");
    }
  }

  removeStubSpecs();
  std::printf(
      "[selftest-bypasscompound] points/roundtrip/cmd/tex/nested/undo-redo -> %s\n",
      rc == 0 ? "PASS" : "FAIL");
  q->release(); dev->release(); pool->release();
  return rc;
}

}  // namespace sw
