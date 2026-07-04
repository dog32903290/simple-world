// runtime/point_ops_resetsubtreetrigger — ResetSubtreeTrigger: TiXL's subtree CACHE-BUSTER. A single
// (non-MultiInput) Command SubGraph passthrough whose Trigger, when set, recursively INVALIDATES every
// dirty flag under the wired subtree so TiXL's pull-graph re-cooks it from scratch.
//
// TiXL ground truth: flow/ResetSubtreeTrigger.cs:14-69 (the Update + the recursive Invalidate):
//   if (Trigger.GetValue(context)) {
//     DirtyFlag.GlobalInvalidationTick++;          // :18
//     Invalidate(Command);                          // :19 — recursive dirty-flag walk (:26-69)
//     Trigger.TypedInputValue.Value = false;        // :20-21 — SELF-CLEARING (a one-shot button)
//   }
//   Command.GetValue(context);                      // :23 — cook the subtree (the passthrough)
//   Output has DirtyFlagTrigger.Always (:6).
//   ResetSubtreeTrigger.t3 (re-read & confirmed): Trigger DefaultValue = false.
//
// ★NAMED FORK fork-resetsubtree-invalidation-noop: the ENTIRE Invalidate machinery (:18-21, :26-69)
// is TiXL pull-graph dirty-flag bookkeeping — it exists to FORCE a re-cook of cached slots. sw's
// cook drivers (flat cook + resident frame_cook) re-cook the graph EVERY FRAME with no cross-frame
// slot cache to bust, so invalidation is a structural no-op: the subtree is already recooked fresh
// each frame — the POST-invalidation observable state (subtree freshly cooked, items forwarded) is
// what sw produces every frame. The Trigger input + its self-clear are therefore inert here (the
// STATEFUL ops that DO carry cross-frame memory — Damp/Once/particles — keep their state by design;
// busting those is TiXL's per-op Reset inputs' job, not this op's dirty-flag walk). If sw ever grows
// cross-frame slot caching, this op is the seam where the bust hook lands.
// DirtyFlagTrigger.Always (:6) is likewise pull-graph plumbing — no-op under cook-every-frame.
//
// What REMAINS load-bearing (and what the golden pins): the op is a TRANSPARENT SubGraph passthrough
// (:23) — the wired subtree's items flow through UNCHANGED (the LogMessage passthrough shape, minus
// the log). ★COOK-CORE HOOK: NONE — the single Command input rides the driver's existing generic
// collector (single-input collapse branch of cookCommand).
//
// runtime leaf: pure CPU + Metal (the golden cooks through PointGraph); no UI, no upward deps.
#include "runtime/point_ops.h"

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

// ─────────────── -bug DRIVER flag (the passthrough-drop tooth — the load-bearing half) ───────────────
// When true, the op DROPS the forwarded chain (emits empty) → the "subtree items flow through
// unchanged" assertion goes RED. OFF in production. A CPU op flag (constitution rule).
bool& resetSubtreeDropChainForTest() { static bool v = false; return v; }

// ─────────────── the ResetSubtreeTrigger op (transparent SubGraph passthrough) ───────────────
// The Trigger/Invalidate half is the named no-op fork (header); the passthrough (:23) is the cook.
RenderCommand cookResetSubtreeTrigger(CmdCookCtx& c) {
  RenderCommand rc;
  if (!resetSubtreeDropChainForTest() && c.inputCommand)
    rc.items = c.inputCommand->items;  // Command.GetValue(context) — the transparent forward (:23)
  return rc;
}

void registerResetSubtreeTriggerOp() {
  registerCmdOp("ResetSubtreeTrigger", cookResetSubtreeTrigger);
}

// ───────────────────────────────────────── GOLDEN ─────────────────────────────────────────
// --selftest-resetsubtreetrigger (BOTH legs). Topology: one stub Command op (tag 555) →
// ResetSubtreeTrigger.Command (single SubGraph wire) → StubRenderTarget (terminal capture).
// Closed-form (ResetSubtreeTrigger.cs:23): the subtree's item passes through UNCHANGED — tag 555
// survives, count 1 — with Trigger both false AND true (the invalidation half is the named no-op
// fork; the observable passthrough must be identical on both values).
// -bug: resetSubtreeDropChainForTest() drops the forward → 0 items → RED on the REAL op cook.
namespace {
RenderCommand g_rstChain;
RenderCommand rstStub(CmdCookCtx&) { RenderCommand rc; rc.items.push_back(RenderDrawItem{nullptr, 555u, 1.0f}); return rc; }
void rstStubRenderTarget(TexCookCtx& c) { if (c.command) g_rstChain = *c.command; }

NodeSpec rstAtomicSpec(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}

void installResetSubtreeSpecs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["ResetSubtreeTrigger"] = rstAtomicSpec("ResetSubtreeTrigger",
      {{"Command", "Command", "Command", true},
       {"out", "out", "Command", false},
       {"Trigger", "Trigger", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}});
  dyn["RstStub"] = rstAtomicSpec("RstStub", {{"out", "out", "Command", false}});
  dyn["StubRenderTarget"] = rstAtomicSpec("StubRenderTarget",
      {{"command", "command", "Command", true}, {"out", "out", "Texture2D", false}});
  setDynamicSpecs(std::move(dyn));
}

bool cookResetSubtreeGraph(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, int whichPath,
                           bool trigger, RenderCommand& outChain) {
  Graph g;
  Node a; a.id = 1; a.type = "RstStub"; g.nodes.push_back(a);
  Node rs; rs.id = 4; rs.type = "ResetSubtreeTrigger"; rs.params["Trigger"] = trigger ? 1.0f : 0.0f;
  g.nodes.push_back(rs);
  Node rt; rt.id = 5; rt.type = "StubRenderTarget"; g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, 0), pinId(4, 0)});  // stub → .Command (single SubGraph wire)
  g.connections.push_back({104, pinId(4, 1), pinId(5, 0)});  // .out → StubRenderTarget.command

  g_rstChain = RenderCommand{};
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
  outChain = g_rstChain;
  return true;
}
}  // namespace

int runResetSubtreeTriggerSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-resetsubtreetrigger] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerResetSubtreeTriggerOp();  // the REAL op under test
  registerCmdOp("RstStub", rstStub);
  registerTexOp("StubRenderTarget", rstStubRenderTarget);
  installResetSubtreeSpecs();

  resetSubtreeDropChainForTest() = injectBug;  // ★bug = drop the passthrough

  const char* pathName[2] = {"flat", "resident"};
  bool allFaithful = true;

  for (int path = 0; path < 2; ++path) {
    // Trigger=false AND Trigger=true must BOTH forward the subtree unchanged (:23 runs either way;
    // the invalidation is the named no-op fork — a Trigger that EATS the chain would be a real bug).
    bool legOk = true;
    for (int trig = 0; trig < 2; ++trig) {
      RenderCommand chain;
      cookResetSubtreeGraph(dev, lib, q, path, /*trigger=*/trig == 1, chain);
      const bool passOk = chain.items.size() == 1 && chain.items[0].count == 555u;
      legOk = legOk && passOk;
      std::printf("[selftest-resetsubtreetrigger] %s trig=%d items=%zu(want 1 [555]) -> %s\n",
                  pathName[path], trig, chain.items.size(), passOk ? "faithful-ok" : "tripped");
    }
    allFaithful = allFaithful && legOk;
  }

  resetSubtreeDropChainForTest() = false;  // process hygiene
  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-resetsubtreetrigger] injectBug did NOT trip (the passthrough drop is "
                  "not reaching the cook)\n");
      return 0;  // did-not-trip → 0 so --bite's NO-BITE list surfaces the dead tooth
    }
    std::printf("[selftest-resetsubtreetrigger] injectBug correctly RED (passthrough dropped → the "
                "subtree item vanished on BOTH legs)\n");
    return 1;
  }
  std::printf("[selftest-resetsubtreetrigger] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/332, {"resetsubtreetrigger", runResetSubtreeTriggerSelfTest});

}  // namespace sw
