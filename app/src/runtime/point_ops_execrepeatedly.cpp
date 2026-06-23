// runtime/point_ops_execrepeatedly — S3c ExecRepeatedly: the Loop SIBLING that RE-COOKS its collected
// Command wires `RepeatCount` times with NO context-var injection. Where Loop wraps ONE SubGraph and writes
// index/progress per iteration, ExecRepeatedly is MULTIINPUT (== Execute's collector) and just re-executes
// all wired subtrees `RepeatCount` times, concatenating every repetition's items. The re-cook is the same
// cook-core mechanism class as Loop's per-iteration re-cook — so this leaf rides the S3c machinery, adding a
// small ExecRepeatedly branch to the driver's MultiInput Command collector (both legs) that calls the shared
// execRepeatedlyRunRepetitions() helper.
//
// TiXL ground truth: flow/ExecRepeatedly.cs:22-55 (the Update):
//   _callsSinceLastRefresh++;
//   var repeatCount = RepeatCount.GetValue(ctx).Clamp(0, 100);   // :24
//   if (repeatCount <= 0) return;                                // :25  no repetitions
//   var skipFrames = SkipFrameCount.GetValue(ctx).Clamp(0,10000);
//   if (_callsSinceLastRefresh <= skipFrames) return;            // :29-32  per-frame skip gate
//   _callsSinceLastRefresh = 0;
//   var commands = Command.CollectedInputs;                      // MultiInput, wire order
//   for i: commands[i].PrepareAction(ctx);                       // prepare ALL
//   for (rep=0; rep<repeatCount; rep++)                          // :43  THE repeat loop
//     for i: commands[i].GetValue(ctx);                          // :46  execute ALL wires, each repetition
//   for i: commands[i].RestoreAction(ctx);                       // restore ALL
//   Command.DirtyFlag.Clear();
// Load-bearing facts mirrored: (a) RepeatCount clamped [0,100] (:24); (b) repeatCount<=0 ⇒ no execution (:25);
// (c) the wired Command subtrees execute `repeatCount` times, each repetition cooking ALL wires in wire order
// (:43-46) — the prepare/execute/restore three-pass collapses to one ordered item APPEND per the retained-
// mode named fork Execute documents; so the output chain = the concatenated wires REPEATED repeatCount times.
//
// ★FORK (named): SkipFrameCount + _callsSinceLastRefresh (:27-34) are a PER-FRAME counter that skips the
// execution on the first `skipFrames` calls after a refresh, then runs once and resets. sw has no per-node
// cross-frame call counter in the cook core, and a cook-pure golden cannot exercise a self-resetting frame
// counter deterministically. So sw ships the SkipFrameCount=0 path (the .t3 DEFAULT — :6 DefaultValue 0),
// which means "no skipping ⇒ execute every call", and the faithful re-cook count = RepeatCount. The
// frame-skip throttle is the deferred frame-state half (same deferral class as ExecuteOnce's DirtyFlag latch).
//
// ★COOK-CORE HOOK: an ExecRepeatedly branch in the driver's MultiInput Command collector (both legs, like
// Loop's branch). It reads RepeatCount (clamped [0,100]), gathers the wired Command sources in wire order,
// and calls execRepeatedlyRunRepetitions(), which owns the repeat loop + concat. cookExecRepeatedly is THIN:
// it forwards cc.inputCommand (the chain the driver already repeated+concatenated), exactly like Loop/Switch.
//
// runtime leaf: pure CPU + Metal (the golden cooks through PointGraph); no UI, no upward deps.
#include "runtime/point_ops.h"

#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph / Node / NodeSpec / PortSpec / pinId / setDynamicSpecs
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary)
#include "runtime/point_graph.h"          // CmdCookCtx / registerCmdOp / PointGraph
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem / execRepeatedlyRunRepetitions / *Bug*
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// ───────────────────────────── -bug DRIVER flag (the run-once tooth) ─────────────────────────────
bool& execRepeatedlyBugRunOnceForTest() { static bool v = false; return v; }

// ───────────────────────────── the repeat mechanism (shared by both cook legs) ─────────────────────────────
// Cook the collected wires `count` times (count clamped [0,100] by the driver before calling), concatenating
// each repetition's items. cookAllWiresOnce FRESH-cooks every wired Command source in wire order (the leg
// supplies it). count<=0 → empty (ExecRepeatedly.cs:25). The repeat loop is the cook-core re-execution; the
// per-repetition cook is fresh (the driver's cook recurses each call — no memo short-circuits a 2nd rep).
void execRepeatedlyRunRepetitions(int count, RenderCommand& out,
                                  const std::function<RenderCommand()>& cookAllWiresOnce) {
  if (count <= 0) return;  // ExecRepeatedly.cs:25 (repeatCount<=0 ⇒ no execution)

  // -bug: run the wires ONCE (drop the repeat loop) → 1×wires items instead of count×wires → RED.
  if (execRepeatedlyBugRunOnceForTest()) {
    RenderCommand one = cookAllWiresOnce();
    out.items.insert(out.items.end(), one.items.begin(), one.items.end());
    return;
  }

  // FAITHFUL (ExecRepeatedly.cs:43-46): execute all wires `count` times, concatenated in repetition order.
  for (int rep = 0; rep < count; ++rep) {
    RenderCommand sub = cookAllWiresOnce();  // re-cook ALL wires fresh this repetition
    out.items.insert(out.items.end(), sub.items.begin(), sub.items.end());
  }
}

// ───────────────────────────── the ExecRepeatedly op (forwards the repeated+concatenated chain) ─────────────────────────────
// THIN: the driver already ran the repetitions and concatenated everything into cc.inputCommand. Forward it
// (like Loop/Switch/Execute forward the chain the collector built).
RenderCommand cookExecRepeatedly(CmdCookCtx& c) {
  RenderCommand rc;
  if (c.inputCommand) rc.items = c.inputCommand->items;
  return rc;
}

void registerExecRepeatedlyOp() { registerCmdOp("ExecRepeatedly", cookExecRepeatedly); }

// ───────────────────────────────────────── GOLDEN ─────────────────────────────────────────
// --selftest-execrepeatedly (S3c, BOTH legs). Topology: two stub Command ops (tags 11/22) → ExecRepeatedly
// .Command (2 wires, wire order) → StubRenderTarget(terminal capture). Closed-form (the repeat keystone):
//   RepeatCount=3 → 2 wires × 3 reps = 6 items, sequence [11,22, 11,22, 11,22] (wire order within each rep,
//     reps in order). The make-or-break: the chain length == wires × RepeatCount (the subtree RE-EXECUTED).
//   RepeatCount=1 → 2 items [11,22] (one pass == plain Execute).
//   RepeatCount=0 → 0 items (the <=0 gate, ExecRepeatedly.cs:25).
//   RepeatCount=200 → clamped to 100 → 200 items (the [0,100] clamp, ExecRepeatedly.cs:24). The driver
//     clamps; the golden asserts 2×100=200, NOT 2×200=400.
// -bug: execRepeatedlyBugRunOnceForTest() drops the repeat loop → RepeatCount=3 yields 2 items (one pass)
//   instead of 6 → the count assertion goes RED. Both legs (resident is the production leg — S2c blood
//   lesson; a resident-only miss = a prod-only black-hole that executes the subtree once not N times).
namespace {
RenderCommand g_capturedChain;
RenderCommand stubP(CmdCookCtx&) { RenderCommand rc; rc.items.push_back(RenderDrawItem{nullptr, 11u, 1.0f}); return rc; }
RenderCommand stubQ(CmdCookCtx&) { RenderCommand rc; rc.items.push_back(RenderDrawItem{nullptr, 22u, 1.0f}); return rc; }
void stubRenderTarget(TexCookCtx& c) { if (c.command) g_capturedChain = *c.command; }

NodeSpec atomicSpec(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}

void installExecRepeatedlySpecs() {
  std::map<std::string, NodeSpec> dyn;
  // ExecRepeatedly: ONE MultiInput Command input + RepeatCount + SkipFrameCount + Command out.
  dyn["ExecRepeatedly"] = atomicSpec("ExecRepeatedly",
      {{"Command", "Command", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
       {"out", "out", "Command", false},
       {"RepeatCount", "RepeatCount", "Float", true, 1.0f, 0.0f, 100.0f},
       {"SkipFrameCount", "SkipFrameCount", "Float", true, 0.0f, 0.0f, 10000.0f}});
  dyn["StubP"] = atomicSpec("StubP", {{"out", "out", "Command", false}});
  dyn["StubQ"] = atomicSpec("StubQ", {{"out", "out", "Command", false}});
  dyn["StubRenderTarget"] = atomicSpec("StubRenderTarget",
      {{"command", "command", "Command", true}, {"out", "out", "Texture2D", false}});
  setDynamicSpecs(std::move(dyn));
}

bool cookExecRepeatedlyGraph(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, int whichPath,
                             float repeatCount, RenderCommand& outChain) {
  Graph g;
  Node p; p.id = 1; p.type = "StubP"; g.nodes.push_back(p);
  Node qN; qN.id = 2; qN.type = "StubQ"; g.nodes.push_back(qN);
  Node er; er.id = 3; er.type = "ExecRepeatedly"; er.params["RepeatCount"] = repeatCount;
  er.params["SkipFrameCount"] = 0.0f; g.nodes.push_back(er);
  Node rt; rt.id = 4; rt.type = "StubRenderTarget"; g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, 0), pinId(3, 0)});  // StubP → ExecRepeatedly.Command (wire0)
  g.connections.push_back({102, pinId(2, 0), pinId(3, 0)});  // StubQ → ExecRepeatedly.Command (wire1)
  g.connections.push_back({103, pinId(3, 1), pinId(4, 0)});  // ExecRepeatedly.out → StubRenderTarget.command

  g_capturedChain = RenderCommand{};
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 64, 64);
  if (whichPath == 0) {
    pg.cook(g, ctx, nullptr, /*terminal=*/4);
  } else {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    pg.cookResident(rg, ctx, nullptr, /*terminal path=*/"4");
  }
  outChain = g_capturedChain;
  return true;
}

// Assert the chain == [11,22] repeated `reps` times (wire order within each rep).
bool chainIsRepeated(const RenderCommand& c, int reps) {
  if ((int)c.items.size() != 2 * reps) return false;
  for (int r = 0; r < reps; ++r) {
    if (c.items[(size_t)(2 * r)].count != 11u) return false;
    if (c.items[(size_t)(2 * r + 1)].count != 22u) return false;
  }
  return true;
}
}  // namespace

int runExecRepeatedlySelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-execrepeatedly] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerExecRepeatedlyOp();  // the REAL op under test
  registerCmdOp("StubP", stubP);
  registerCmdOp("StubQ", stubQ);
  registerTexOp("StubRenderTarget", stubRenderTarget);
  installExecRepeatedlySpecs();

  execRepeatedlyBugRunOnceForTest() = injectBug;  // ★bug = drop the repeat loop (run once)

  const char* pathName[2] = {"flat", "resident"};
  bool allFaithful = true;

  for (int path = 0; path < 2; ++path) {
    if (!injectBug) {
      RenderCommand c3; cookExecRepeatedlyGraph(dev, lib, q, path, /*RepeatCount=*/3.0f, c3);
      bool rep3 = chainIsRepeated(c3, 3);  // 6 items [11,22]×3 — the re-execute keystone
      RenderCommand c1; cookExecRepeatedlyGraph(dev, lib, q, path, /*RepeatCount=*/1.0f, c1);
      bool rep1 = chainIsRepeated(c1, 1);  // 2 items == plain Execute
      RenderCommand c0; cookExecRepeatedlyGraph(dev, lib, q, path, /*RepeatCount=*/0.0f, c0);
      bool rep0 = c0.items.empty();        // <=0 gate
      RenderCommand cClamp; cookExecRepeatedlyGraph(dev, lib, q, path, /*RepeatCount=*/200.0f, cClamp);
      bool clamp = chainIsRepeated(cClamp, 100);  // 200 items (clamped to 100), NOT 400

      bool legOk = rep3 && rep1 && rep0 && clamp;
      allFaithful = allFaithful && legOk;
      std::printf("[selftest-execrepeatedly] %s rep3=%d(items %zu/want6) rep1=%d rep0=%d clamp=%d(items "
                  "%zu/want200) -> %s\n", pathName[path], rep3, c3.items.size(), rep1, rep0, clamp,
                  cClamp.items.size(), legOk ? "faithful-ok" : "tripped");
    } else {
      // -bug: run-once → RepeatCount=3 yields 2 items (one pass) instead of 6 → RED.
      RenderCommand c3; cookExecRepeatedlyGraph(dev, lib, q, path, /*RepeatCount=*/3.0f, c3);
      bool wouldBeFaithful = chainIsRepeated(c3, 3);  // would need 6 items
      allFaithful = allFaithful && wouldBeFaithful;
      std::printf("[selftest-execrepeatedly] -bug run-once %s RepeatCount=3: items=%zu (want 6 if faithful) "
                  "-> %s\n", pathName[path], c3.items.size(), wouldBeFaithful ? "green?!" : "RED");
    }
  }

  execRepeatedlyBugRunOnceForTest() = false;  // process hygiene
  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-execrepeatedly] FAIL: injectBug still produced the faithful repeated shape (the "
                  "repeat loop is not actually re-executing)\n");
      return 1;
    }
    std::printf("[selftest-execrepeatedly] injectBug correctly RED (repeat loop dropped → the subtree ran "
                "ONCE not RepeatCount times → the chain-length assertion failed on BOTH legs)\n");
    return 1;
  }
  std::printf("[selftest-execrepeatedly] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/332, {"execrepeatedly", runExecRepeatedlySelfTest});

}  // namespace sw
