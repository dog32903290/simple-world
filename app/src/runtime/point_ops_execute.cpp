// Execute command op + the S2a MultiInput-Command-collector golden — TiXL Operators/Lib/flow/Execute.cs
// (the render-graph sequencer). Execute is the KEYSTONE of the S2 render island (~155 Slot<Command>
// ops): a MultiInput Command port that concatenates N wired Command chains in wire-declaration order
// into one chain. Almost every render op outputs Slot<Command> and they only compose through a
// MultiInput Group/Execute — without this collector the cook core could gather exactly ONE Command
// input (Camera/SetRequestedResolution), so multi-layer scenes were un-composable.
//
// BACKWARD-TRACE (Execute.cs:14-39, the Update):
//   var commands = Command.CollectedInputs;            // MultiInput, wire-declaration order
//   if (IsEnabled) {
//     for i: commands[i].Value?.PrepareAction(ctx)     // 1) prepare ALL, in order
//     for i: commands[i].GetValue(ctx)                 // 2) execute ALL, in order ← the draws
//     for i: commands[i].Value?.RestoreAction(ctx)     // 3) restore ALL, in order
//   }
//   Command.DirtyFlag.Clear();
// Three sequential passes (prepare-all → execute-all → restore-all), each iterating CollectedInputs in
// wire order. The EXECUTE pass IS the draw-command ordering: items append to the render target in input
// order. IsEnabled (.t3 DefaultValue = true) gates the whole thing — disabled ⇒ no draws.
//
// ★INTEGRATION MECHANISM — driver-side concat (the blueprint's named fork, chosen over op-side concat):
// SW is retained-mode. There is no immediate-mode DX11 device context to Prepare/Restore — each
// RenderTarget executor (cookRenderTarget) opens its own commandBuffer→encoder→endEncoding, so
// Prepare/Restore are render-pass boundaries OWNED BY THE EXECUTOR, not callbacks carried per Command
// (render_command.h's "DATA RECORD, not a closure" rationale). The three TiXL passes therefore collapse
// into ONE ordered item APPEND: "execute all commands in wire order" == "all items concatenated in wire
// order". The cook DRIVER (cookCommand's MultiInput Command branch) owns the concat because it already
// owns subtree cooking + the S1 RequestedResolution push/pop guard — the natural home (mirrors how the
// driver concatenates a chain across multi-inputs for RenderTarget). So cookExecute is THIN: it returns
// the already-concatenated chain the driver handed it via cc.inputCommand, gated only by IsEnabled.
//   FORK (named): op-side concat (CmdCookCtx gaining an inputCommands[] vector the op walks) was
//   rejected — it duplicates the driver's gather, needs an ABI field, and gains nothing because the
//   driver already cooks+owns each subtree. Cross-sibling PrepareAction state (Execute.cs risk #1) is
//   N/A: no sw render op uses a Prepare side-effect visible to a later sibling (all retained-mode data
//   stamps); if one ever does, the three-pass split must be reinstated — flagged in the report.
#include "runtime/point_ops.h"

#include "runtime/point_graph.h"     // CmdCookCtx, registerCmdOp, cookParam
#include "runtime/render_command.h"  // RenderCommand / RenderDrawItem / executeCollectFirstOnlyForTest

#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"       // Graph / Node / NodeSpec / setDynamicSpecs / pinId
#include "runtime/tixl_point.h"  // EvaluationContext

namespace sw {

// S2a test-only DRIVER flag (decl in render_command.h): forces the collector to first-wire-only so the
// --selftest-execute -bug leg drops layers past the first → the chain loses items → RED. OFF in prod.
bool& executeCollectFirstOnlyForTest() {
  static bool v = false;
  return v;
}

// Execute (TiXL flow/Execute.cs): MultiInput Command in → Command out. The driver has already collected
// + concatenated all wired Command subtrees in wire-declaration order into cc.inputCommand (the S2a
// keystone branch in cookCommand). This op only applies IsEnabled: enabled ⇒ pass the concatenated chain
// through; disabled ⇒ an EMPTY chain (no draws, TiXL: the prepare/execute/restore loop is skipped). No
// transform, no camera — VisibleGizmos = Execute without transform = this exact op; Group adds an SRT
// push (S2b, out of scope). Unwired Command input ⇒ inputCommand null ⇒ empty chain (faithful: an empty
// CollectedInputs evals nothing).
RenderCommand cookExecute(CmdCookCtx& c) {
  RenderCommand rc;
  const bool enabled = cookParam(c, "IsEnabled", 1.0f) > 0.5f;  // .t3 DefaultValue = true
  if (enabled && c.inputCommand) rc.items = c.inputCommand->items;  // already wire-ordered + concatenated
  return rc;
}

void registerExecuteOp() { registerCmdOp("Execute", cookExecute); }

// ───────────────────────────────── GOLDEN ─────────────────────────────────
// --selftest-execute (the S2a STRUCTURAL collector golden, load-bearing): drive the REAL cookCommand
// MultiInput Command collector via a real Graph cooked THROUGH PointGraph::cook (the runRenderTargetWired
// precedent), and prove the resulting RenderCommand has all N subtrees' items, in WIRE-DECLARATION order.
//
// We register three stub Command ops, each emitting ONE distinguishable item (a DrawPoints item carrying
// a unique `count` tag = 100, 200, 300), and a stub RenderTarget executor that CAPTURES the chain it is
// handed (the bypass-selftest capture precedent). Wire: stubA → Execute.Command[wire0], stubB →
// Execute.Command[wire1], stubC → Execute.Command[wire2], Execute → RenderTarget. After cook():
//   FAITHFUL  → captured chain has 3 items, counts == [100,200,300] (wire order). Count == 3.
//   -bug      → executeCollectFirstOnlyForTest() forces the collector to first-wire-only → 1 item
//               (count 100) → the size/order assertion FAILS → RED. (The bug is in the PRODUCTION
//               collector loop, not the test emit — a genuine collector tooth.)
//
// Closed-form discipline: the expected values (item count 3, the [100,200,300] tag sequence) are the
// EXACT tags the stub ops stamp + the EXACT wire order the graph declares — no fwidth, no off-screen,
// no made-up magic numbers. The tags are deterministic integers chosen distinct so a reorder/drop bites.
namespace {
RenderCommand g_capturedChain;  // the chain the stub RenderTarget executor was handed (the introspection)
// Three stub Command ops, each a 1-item chain tagged by a unique count (the wire-order witness).
RenderCommand stubCmdA(CmdCookCtx&) { RenderCommand rc; rc.items.push_back(RenderDrawItem{nullptr, 100u, 1.0f}); return rc; }
RenderCommand stubCmdB(CmdCookCtx&) { RenderCommand rc; rc.items.push_back(RenderDrawItem{nullptr, 200u, 1.0f}); return rc; }
RenderCommand stubCmdC(CmdCookCtx&) { RenderCommand rc; rc.items.push_back(RenderDrawItem{nullptr, 300u, 1.0f}); return rc; }
// Stub RenderTarget (the tex executor terminal): capture the concatenated chain; draw nothing.
void stubRenderTarget(TexCookCtx& c) { if (c.command) g_capturedChain = *c.command; }

NodeSpec atomicSpec(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}
}  // namespace

int runExecuteSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-execute] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Install the stub kit (test-only specs + cooks). Execute is the REAL builtin op under test.
  registerExecuteOp();
  registerCmdOp("StubCmdA", stubCmdA);
  registerCmdOp("StubCmdB", stubCmdB);
  registerCmdOp("StubCmdC", stubCmdC);
  registerTexOp("StubRenderTarget", stubRenderTarget);
  {
    std::map<std::string, NodeSpec> dyn;
    // Execute: ONE MultiInput Command input port + IsEnabled + a Command output. PortSpec positional init
    // through multiInput=true (the FloatsToList/Values precedent {..., false, 1, true}); the Command port
    // carries no Float fields so def/min/max are inert placeholders.
    dyn["Execute"] = atomicSpec("Execute",
        {{"Command", "Command", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
         {"out", "out", "Command", false},
         {"IsEnabled", "IsEnabled", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true}});
    dyn["StubCmdA"] = atomicSpec("StubCmdA", {{"out", "out", "Command", false}});
    dyn["StubCmdB"] = atomicSpec("StubCmdB", {{"out", "out", "Command", false}});
    dyn["StubCmdC"] = atomicSpec("StubCmdC", {{"out", "out", "Command", false}});
    dyn["StubRenderTarget"] = atomicSpec("StubRenderTarget",
        {{"command", "command", "Command", true}, {"out", "out", "Texture2D", false}});
    setDynamicSpecs(std::move(dyn));
  }

  // Build the graph: A,B,C → Execute.Command (3 wires, wire-declaration order = push order) → RenderTarget.
  Graph g;
  Node a; a.id = 1; a.type = "StubCmdA"; g.nodes.push_back(a);
  Node b; b.id = 2; b.type = "StubCmdB"; g.nodes.push_back(b);
  Node cN; cN.id = 3; cN.type = "StubCmdC"; g.nodes.push_back(cN);
  Node ex; ex.id = 4; ex.type = "Execute"; g.nodes.push_back(ex);
  Node rt; rt.id = 5; rt.type = "StubRenderTarget"; g.nodes.push_back(rt);
  // Execute spec port 0 = the MultiInput Command input. Push wires in A,B,C order (= the witness order).
  g.connections.push_back({101, pinId(1, 0), pinId(4, 0)});  // A.out → Execute.Command
  g.connections.push_back({102, pinId(2, 0), pinId(4, 0)});  // B.out → Execute.Command
  g.connections.push_back({103, pinId(3, 0), pinId(4, 0)});  // C.out → Execute.Command
  g.connections.push_back({104, pinId(4, 1), pinId(5, 0)});  // Execute.out → RenderTarget.command

  g_capturedChain = RenderCommand{};
  executeCollectFirstOnlyForTest() = injectBug;  // ★bug = collapse the collector to wire 0 only

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 64, 64);
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/5);  // the StubRenderTarget terminal captures the chain

  executeCollectFirstOnlyForTest() = false;  // reset the global (process hygiene)

  const size_t n = g_capturedChain.items.size();
  // Expected (closed-form): 3 items, the [100,200,300] tag sequence in wire order.
  bool count3 = (n == 3);
  bool order = count3 &&
               g_capturedChain.items[0].count == 100u &&
               g_capturedChain.items[1].count == 200u &&
               g_capturedChain.items[2].count == 300u;
  bool faithful = count3 && order;

  std::printf("[selftest-execute] items=%zu(want 3) tags=[%u,%u,%u](want 100,200,300) -> %s\n", n,
              n > 0 ? g_capturedChain.items[0].count : 0u,
              n > 1 ? g_capturedChain.items[1].count : 0u,
              n > 2 ? g_capturedChain.items[2].count : 0u,
              faithful ? "faithful-ok" : "tripped");

  setDynamicSpecs({});  // drop the injected test specs
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (faithful) {
      std::printf("[selftest-execute] FAIL: injectBug collapsed nothing (collector still concatenated)\n");
      return 1;
    }
    std::printf("[selftest-execute] injectBug correctly RED (collector collapsed to wire 0 → only 1 "
                "item survived → wire-order/count assertion failed)\n");
    return 1;
  }
  std::printf("[selftest-execute] %s\n", faithful ? "PASS" : "FAIL");
  return faithful ? 0 : 1;
}

}  // namespace sw
