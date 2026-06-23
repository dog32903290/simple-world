// runtime/point_ops_executeonce — S3b ExecuteOnce: the GATED Execute. A MultiInput Command port that
// concatenates N wired Command chains in wire order (== Execute, the S2a collector) BUT only when a Trigger
// is set; otherwise it emits nothing. The driver's existing MultiInput Command collector already concatenates
// the wires into cc.inputCommand (zero cook-core change, like ExecuteOnce shares Execute's collector); this
// op just applies the Trigger gate — pass the concatenated chain through when triggered, empty when not.
//
// TiXL ground truth: flow/ExecuteOnce.cs:24-46 (the Update):
//   OutputTrigger.Value = Trigger.DirtyFlag.IsDirty;
//   if (Trigger.DirtyFlag.IsDirty) {
//     Trigger.DirtyFlag.Clear();
//     var commands = Command.GetCollectedTypedInputs();        // MultiInput, wire order
//     for i: commands[i].PrepareAction(ctx);                   // prepare ALL
//     for i: commands[i].GetValue(ctx);                        // execute ALL  ← the draws
//     for i: commands[i].RestoreAction(ctx);                   // restore ALL
//   }
// Two load-bearing facts mirrored: (a) when triggered, ALL wired Command subtrees execute in wire order ==
// Execute's concat-all (the three prepare/execute/restore passes collapse to one ordered item APPEND in
// sw's retained-mode model — the exact named fork Execute documents); (b) when NOT triggered, the whole
// loop is skipped → NO draws → an EMPTY chain. The difference from Execute is the GATE: Execute gates on
// IsEnabled (a static bool), ExecuteOnce gates on Trigger.
//
// ★FORK (named): TiXL gates on `Trigger.DirtyFlag.IsDirty` — a PER-FRAME LATCH that is true on the first
// eval after the Trigger input changes, then self-clears (`Trigger.DirtyFlag.Clear()`), so the subtree
// executes exactly ONCE per trigger edge. sw has no per-node DirtyFlag latch in the cook core, and a
// cook-pure golden cannot exercise a cross-frame self-clearing latch deterministically. So sw models the
// gate as the Trigger input VALUE: Trigger>0.5 ⇒ execute (concat-all), Trigger≤0.5 ⇒ empty. This is the
// faithful BEHAVIOUR of one trigger edge (the "execute when triggered, skip when not" semantics); the
// once-per-edge self-clear is the deferred frame-state half (the same class of deferral as ExecRepeatedly's
// SkipFrameCount per-frame counter). The OutputTrigger bool output (which mirrors IsDirty) is likewise
// dropped — sw has no bool Command-side output port; it is editor wiring, not a draw effect.
//
// ★COOK-CORE HOOK: NONE. ExecuteOnce's Command port is MultiInput → the driver's existing else-branch in
// cookCommand concatenates all wired subtrees into cc.inputCommand (the S2a Execute collector, unchanged).
// This op rides that gather and only applies the Trigger gate in the op cook — exactly like Execute rides
// it and applies IsEnabled. Zero new driver branch.
//
// runtime leaf: pure CPU + Metal (the golden cooks through PointGraph); no UI, no upward deps.
#include "runtime/point_ops.h"

#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph / Node / NodeSpec / PortSpec / pinId / setDynamicSpecs
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary)
#include "runtime/point_graph.h"          // CmdCookCtx / registerCmdOp / cookParam / PointGraph
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// ───────────────────────────── -bug DRIVER flag (the gate-drop tooth) ─────────────────────────────
// When true, the op IGNORES the Trigger gate and always passes the concatenated chain → the "not triggered ⇒
// empty" assertion goes RED. OFF in production. A CPU op flag (no shader test seam — constitution rule).
bool& executeOnceIgnoreTriggerForTest() { static bool v = false; return v; }

// ───────────────────────────── the ExecuteOnce op (gated concat-all forward) ─────────────────────────────
// THIN: the driver already concatenated all wired Command subtrees in wire order into cc.inputCommand (the
// S2a Execute collector). Apply the Trigger gate: triggered ⇒ pass the chain through; not triggered ⇒ empty.
RenderCommand cookExecuteOnce(CmdCookCtx& c) {
  RenderCommand rc;
  const bool triggered = cookParam(c, "Trigger", 1.0f) > 0.5f;  // .t3 DefaultValue=true (Trigger=new(true))
  if ((triggered || executeOnceIgnoreTriggerForTest()) && c.inputCommand)
    rc.items = c.inputCommand->items;  // wire-ordered + concatenated by the driver (== Execute when triggered)
  return rc;
}

void registerExecuteOnceOp() { registerCmdOp("ExecuteOnce", cookExecuteOnce); }

// ───────────────────────────────────────── GOLDEN ─────────────────────────────────────────
// --selftest-executeonce (S3b, BOTH legs). Topology: three stub Command ops (tags 10/20/30) → ExecuteOnce
// .Command (3 wires, wire order) → StubRenderTarget(terminal capture). Closed-form:
//   Trigger=true  → concat-all in wire order → 3 items, tags [10,20,30] (== Execute).
//   Trigger=false → gate skips the loop → EMPTY chain (0 items).
// -bug: executeOnceIgnoreTriggerForTest() forces the chain through even when Trigger=false → the
//   "Trigger=false ⇒ empty" assertion FAILS (3 items instead of 0) → RED. Both legs (resident is the
//   production leg — S2c blood lesson; a resident-only miss = a prod-only black-hole).
namespace {
RenderCommand g_capturedChain;
RenderCommand stubA(CmdCookCtx&) { RenderCommand rc; rc.items.push_back(RenderDrawItem{nullptr, 10u, 1.0f}); return rc; }
RenderCommand stubB(CmdCookCtx&) { RenderCommand rc; rc.items.push_back(RenderDrawItem{nullptr, 20u, 1.0f}); return rc; }
RenderCommand stubC(CmdCookCtx&) { RenderCommand rc; rc.items.push_back(RenderDrawItem{nullptr, 30u, 1.0f}); return rc; }
void stubRenderTarget(TexCookCtx& c) { if (c.command) g_capturedChain = *c.command; }

NodeSpec atomicSpec(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}

void installExecuteOnceSpecs() {
  std::map<std::string, NodeSpec> dyn;
  // ExecuteOnce: ONE MultiInput Command input + Trigger(Bool) + Command out (the Execute shape + Trigger).
  dyn["ExecuteOnce"] = atomicSpec("ExecuteOnce",
      {{"Command", "Command", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
       {"out", "out", "Command", false},
       {"Trigger", "Trigger", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true}});
  dyn["StubA"] = atomicSpec("StubA", {{"out", "out", "Command", false}});
  dyn["StubB"] = atomicSpec("StubB", {{"out", "out", "Command", false}});
  dyn["StubC"] = atomicSpec("StubC", {{"out", "out", "Command", false}});
  dyn["StubRenderTarget"] = atomicSpec("StubRenderTarget",
      {{"command", "command", "Command", true}, {"out", "out", "Texture2D", false}});
  setDynamicSpecs(std::move(dyn));
}

bool cookExecuteOnceGraph(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, int whichPath,
                          bool trigger, RenderCommand& outChain) {
  Graph g;
  Node a; a.id = 1; a.type = "StubA"; g.nodes.push_back(a);
  Node b; b.id = 2; b.type = "StubB"; g.nodes.push_back(b);
  Node cN; cN.id = 3; cN.type = "StubC"; g.nodes.push_back(cN);
  Node ex; ex.id = 4; ex.type = "ExecuteOnce"; ex.params["Trigger"] = trigger ? 1.0f : 0.0f; g.nodes.push_back(ex);
  Node rt; rt.id = 5; rt.type = "StubRenderTarget"; g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, 0), pinId(4, 0)});  // A → ExecuteOnce.Command (wire0)
  g.connections.push_back({102, pinId(2, 0), pinId(4, 0)});  // B → ExecuteOnce.Command (wire1)
  g.connections.push_back({103, pinId(3, 0), pinId(4, 0)});  // C → ExecuteOnce.Command (wire2)
  g.connections.push_back({104, pinId(4, 1), pinId(5, 0)});  // ExecuteOnce.out → StubRenderTarget.command

  g_capturedChain = RenderCommand{};
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 64, 64);
  if (whichPath == 0) {
    pg.cook(g, ctx, nullptr, /*terminal=*/5);
  } else {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    pg.cookResident(rg, ctx, nullptr, /*terminal path=*/"5");
  }
  outChain = g_capturedChain;
  return true;
}
}  // namespace

int runExecuteOnceSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-executeonce] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerExecuteOnceOp();  // the REAL op under test
  registerCmdOp("StubA", stubA);
  registerCmdOp("StubB", stubB);
  registerCmdOp("StubC", stubC);
  registerTexOp("StubRenderTarget", stubRenderTarget);
  installExecuteOnceSpecs();

  executeOnceIgnoreTriggerForTest() = injectBug;  // ★bug = drop the Trigger gate

  const char* pathName[2] = {"flat", "resident"};
  bool allFaithful = true;

  for (int path = 0; path < 2; ++path) {
    // Triggered → concat-all, tags [10,20,30] in wire order.
    RenderCommand cOn;
    cookExecuteOnceGraph(dev, lib, q, path, /*trigger=*/true, cOn);
    bool onOk = cOn.items.size() == 3 && cOn.items[0].count == 10u && cOn.items[1].count == 20u &&
                cOn.items[2].count == 30u;

    // NOT triggered → empty chain (the gate). Under -bug this comes back as 3 (gate dropped) → RED.
    RenderCommand cOff;
    cookExecuteOnceGraph(dev, lib, q, path, /*trigger=*/false, cOff);
    bool offOk = cOff.items.empty();

    bool legOk = onOk && offOk;
    allFaithful = allFaithful && legOk;
    std::printf("[selftest-executeonce] %s trig=on items=%zu(want 3 [10,20,30]) trig=off items=%zu(want 0) "
                "-> %s\n", pathName[path], cOn.items.size(), cOff.items.size(), legOk ? "faithful-ok" : "tripped");
  }

  executeOnceIgnoreTriggerForTest() = false;  // process hygiene
  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-executeonce] FAIL: injectBug dropped the gate but the off-chain was still "
                  "empty (the gate is not actually gating)\n");
      return 1;
    }
    std::printf("[selftest-executeonce] injectBug correctly RED (Trigger gate dropped → Trigger=false still "
                "concatenated all 3 items on BOTH legs → the not-triggered-is-empty assertion failed)\n");
    return 1;
  }
  std::printf("[selftest-executeonce] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/330, {"executeonce", runExecuteOnceSelfTest});

}  // namespace sw
