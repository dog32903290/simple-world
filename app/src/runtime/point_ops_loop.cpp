// runtime/point_ops_loop — S3c Loop: the RE-COOK keystone. Cook the wired SubGraph `Count` times, each
// iteration writing index/progress context-vars FIRST then re-cooking the subtree (so a value-rail Get*Var
// inside it reads i/progress LIVE), concatenating every iteration's items into one chain. The one genuinely
// new cook-core mechanism — the collector's cook-subtree call moves INSIDE a per-iteration for-loop.
//
// TiXL ground truth: flow/Loop.cs:14-41 (the Update):
//   var indexVar = IndexVariable.GetValue(ctx);  var progVar = ProgressVariable.GetValue(ctx);
//   var end = Count.GetValue(ctx);
//   // TODO: may restore context variable after iterating.   ← Loop.cs:21 LEAKS index/progress (faithful: no restore)
//   for (i = 0; i < end; i++) {
//     ctx.FloatVariables[indexVar] = i;  ctx.IntVariables[indexVar] = i;          // :25-26  index → BOTH dicts
//     ctx.FloatVariables[progVar] = (end==1) ? 0 : i/(float)(end-1);              // :27-35  progress formula
//     DirtyFlag.GlobalInvalidationTick++; Command.InvalidateGraph(); Command.GetValue(ctx);  // :37-39  RE-COOK
//   }
// Three load-bearing facts faithfully mirrored here: (a) per-iteration write index→Float+Int, progress→Float;
// (b) progress = end==1 ? 0 : i/(end-1); (c) per-iteration RE-COOK — the subtree is cooked `end` times, each
// seeing a different index/progress, items concatenated. (d) NO restore after (Loop.cs:21 TODO) — we leak
// index/progress exactly like TiXL; a sibling Get*Var after a Loop sees the last index. Do NOT "fix" this.
//
// ★COOK-CORE HOOK (the seam): like Execute (concat-all) and Switch (sub-select), the loop lives in the
// driver's MultiInput Command collector branch, NOT the op cook. cookLoop is THIN — it forwards
// cc.inputCommand (the chain the driver already built by concatenating the per-iteration cooks). The driver,
// on a Loop node, reads Count/IndexVariable/ProgressVariable, gathers the single wired Command source, and
// calls loopRunIterations() — the SINGLE per-iteration mechanism BOTH the flat (point_graph.cpp) and resident
// (point_graph_resident.cpp) legs call so the var-write + live-scope + re-cook + concat can NEVER fork (the
// S2c/S3a blood lesson: a resident-only miss → production cooks the subtree once with the LAST index → only
// the final iteration's layer draws). The leg supplies cookOneIteration (it knows how to reach the wired
// source); the helper owns the for-loop, the per-iteration var write, the LiveCtxVarScope, and the concat.
//
// runtime leaf: pure CPU + Metal (the golden cooks through PointGraph); no UI, no upward deps.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph / Node / NodeSpec / PortSpec / pinId / setDynamicSpecs / findSpec
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary, paths == ids)
#include "runtime/point_graph.h"          // CmdCookCtx / registerCmdOp / PointGraph
#include "runtime/point_ops_setvarcmd.h"  // LiveCtxVarScope (engage the live ambient map per iteration)
#include "runtime/render_command.h"       // RenderCommand + loopRunIterations / loopBug* decls
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/stateful_value_ops.h"   // ContextVarMap (complete type — touch .floatVars/.intVars)
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// ───────────────────────────── -bug DRIVER flags (read by loopRunIterations, both legs) ─────────────────────────────
bool& loopBugCookOnceForTest()   { static bool v = false; return v; }  // (a) drop the for-loop → 1 item
bool& loopBugReuseFirstForTest() { static bool v = false; return v; }  // (b) cook once, replicate → all identical

// ───────────────────────────── the per-iteration mechanism (shared by both cook legs) ─────────────────────────────
void loopRunIterations(int count, const std::string& indexVar, const std::string& progressVar,
                       ContextVarMap* vars, RenderCommand& out,
                       const std::function<RenderCommand()>& cookOneIteration) {
  if (count <= 0) return;  // TiXL `for i<end` — end<=0 → no iterations → empty chain

  // -bug (a): cook ONCE (drop the for-loop) → only iteration 0's items. The chain has 1 item's worth instead
  // of Count → the count-of-items assertion goes RED. We still write i=0 so the single item is well-formed.
  if (loopBugCookOnceForTest()) {
    if (vars && !indexVar.empty())    { vars->floatVars[indexVar] = 0.0f; vars->intVars[indexVar] = 0; }
    if (vars && !progressVar.empty()) { vars->floatVars[progressVar] = (count == 1) ? 0.0f : 0.0f; }
    LiveCtxVarScope live(vars);
    RenderCommand sub = cookOneIteration();
    out.items.insert(out.items.end(), sub.items.begin(), sub.items.end());
    return;
  }

  // -bug (b): cook ONCE (iteration 0) then REPLICATE those items Count times WITHOUT a fresh var write or
  // re-cook → every replica carries iteration-0's index → all items identical → the per-item-distinct
  // assertion goes RED. Proves the re-cook (not just the item count) is load-bearing.
  if (loopBugReuseFirstForTest()) {
    if (vars && !indexVar.empty())    { vars->floatVars[indexVar] = 0.0f; vars->intVars[indexVar] = 0; }
    if (vars && !progressVar.empty()) { vars->floatVars[progressVar] = (count == 1) ? 0.0f : 0.0f; }
    LiveCtxVarScope live(vars);
    RenderCommand first = cookOneIteration();           // the single real cook (index=0)
    for (int i = 0; i < count; ++i)                      // replicate WITHOUT re-cook (stale geometry)
      out.items.insert(out.items.end(), first.items.begin(), first.items.end());
    return;
  }

  // FAITHFUL (Loop.cs:23-40): per iteration write index→BOTH dicts + progress→Float, engage the live scope so
  // a value-rail Get*Var inside the subtree resolves i/progress LIVE (and the driver's nodeParams memo resolves
  // the subtree's params FRESH this iteration — the scope-aware uncached branch), RE-COOK, concat. No restore.
  for (int i = 0; i < count; ++i) {
    if (vars && !indexVar.empty()) {
      vars->floatVars[indexVar] = (float)i;   // Loop.cs:25  context.FloatVariables[indexVar] = i
      vars->intVars[indexVar]   = (long)i;    // Loop.cs:26  context.IntVariables[indexVar]   = i
    }
    if (vars && !progressVar.empty()) {
      // Loop.cs:27-35  progress = (end==1) ? 0 : i/(float)(end-1)
      vars->floatVars[progressVar] = (count == 1) ? 0.0f : (float)i / (float)(count - 1);
    }
    LiveCtxVarScope live(vars);               // ambient live map for value-rail Get*Var + fresh nodeParams
    RenderCommand sub = cookOneIteration();   // RE-COOK the subtree fresh (Loop.cs:39 Command.GetValue)
    out.items.insert(out.items.end(), sub.items.begin(), sub.items.end());
  }
  // ★NO restore (Loop.cs:21 TODO) — index/progress LEAK after the loop. Faithful: a sibling Get*Var sees the
  //   last index. Do NOT remove/restore the vars here (that would diverge from TiXL).
}

// ───────────────────────────── the Loop op (forwards the concatenated per-iteration chain) ─────────────────────────────
// THIN: the driver already ran the iterations and concatenated every iteration's items into cc.inputCommand.
// Forward it — exactly like Execute/Switch forward the chain the collector built.
RenderCommand cookLoop(CmdCookCtx& c) {
  RenderCommand rc;
  if (c.inputCommand) rc.items = c.inputCommand->items;
  return rc;
}

void registerLoopOp() { registerCmdOp("Loop", cookLoop); }

// ───────────────────────────────────────── GOLDEN ─────────────────────────────────────────
// --selftest-loop (S3c HARD GATE, BOTH legs). Topology (mirror of the setvar-scope TOOTH B template — a
// value-rail GetFloatVar driving a Command op's param INSIDE the scoped SubGraph):
//   GetFloatVar(1, value-rail, "i", fallback 0) → Tx of StampTranslateCmd(2) ;
//   StampTranslateCmd(2) → Loop(3).SubGraph ;  Loop(3) → StubRenderTarget(4, terminal).
//   Loop carries Count, IndexVariable="i", ProgressVariable="p".
//   StampTranslateCmd is a Command op standing in for a Layer: it reads params["Tx"] (= the value rail's
//   GetFloatVar("i") read = the live index) and stamps an item whose position[0] (translate-X) = Tx * 0.25
//   AND whose position[1] = the live progress "p" (read off cc.ctxVars, the Command-rail channel) for the
//   progress golden. So item k carries translate-X = k*0.25 (i=0,1,2) and progressY = the iteration's progress.
//
// CLOSED-FORM (no fwidth, no off-screen, exact):
//   Count=3, "i"=0,1,2 → 3 items, item k position[0] = k*0.25 = {0.00, 0.25, 0.50}  (per-iteration re-cook
//     with a DISTINCT index each time — the make-or-break).
//   Progress Count=4 → progress = i/(4-1) = {0, 1/3, 2/3, 1}  (Loop.cs:33 end>1 branch).
//   Progress Count=1 → progress = 0                            (Loop.cs:29 end==1 branch).
// -bug (a) loopBugCookOnceForTest → 1 item (not 3) → the item-count assertion RED.
// -bug (b) loopBugReuseFirstForTest → 3 items but ALL position[0]=0 (iteration-0 replicated) → the distinct-
//          translate assertion RED.
// Each leg is a SEPARATE assertion (S2c blood lesson: a resident-only miss = a prod-only black-hole;
// production runs the resident leg). The progress teeth run on BOTH legs too.
namespace {
const char* kIndexName = "i";
const char* kProgName  = "p";

// StampTranslateCmd: Command op standing in for a Layer. position[0] = Tx*0.25 (Tx wired to the value-rail
// GetFloatVar("i") = the live index); position[1] = the live progress read off the Command-rail cc.ctxVars.
RenderCommand stampTranslateCmd(CmdCookCtx& c) {
  RenderCommand rc;
  float tx = 0.0f;
  if (c.params) { auto it = c.params->find("Tx"); if (it != c.params->end()) tx = it->second; }
  float prog = 0.0f;
  if (c.ctxVars) {
    auto it = c.ctxVars->floatVars.find(kProgName);
    if (it != c.ctxVars->floatVars.end()) prog = it->second;
  }
  RenderDrawItem item{};
  item.kind = DrawKind::Layer2d;
  item.position[0] = tx * 0.25f;  // the "Layer translated by GetFloatVar(i)*0.25" — distinct per iteration
  item.position[1] = prog;        // the live progress (the progress golden reads this)
  rc.items.push_back(item);
  return rc;
}
RenderCommand g_capturedChain;
void stubRenderTarget(TexCookCtx& c) { if (c.command) g_capturedChain = *c.command; }

NodeSpec atomicSpec(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}

void installLoopSpecs() {
  std::map<std::string, NodeSpec> dyn;
  // value-rail GetFloatVar (evaluate==nullptr; Result port FIRST) — same shape as production GetFloatVar.
  dyn["GetFloatVar"] = atomicSpec("GetFloatVar",
      {{"Result", "Result", "Float", false},
       {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "i"},
       {"FallbackDefault", "FallbackDefault", "Float", true, 0.0f, -1000.0f, 1000.0f}});
  // StampTranslateCmd: Command out + a Float "Tx" input (port index 1) wired to GetFloatVar.Result.
  dyn["StampTranslateCmd"] = atomicSpec("StampTranslateCmd",
      {{"out", "out", "Command", false},
       {"Tx", "Tx", "Float", true, 0.0f, -1000.0f, 1000.0f}});
  // Loop: Command SubGraph in + Command out + Count(Float) + IndexVariable/ProgressVariable (String).
  dyn["Loop"] = atomicSpec("Loop",
      {{"SubGraph", "SubGraph", "Command", true},
       {"out", "out", "Command", false},
       {"Count", "Count", "Float", true, 0.0f, 0.0f, 1000.0f},
       {"IndexVariable", "IndexVariable", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "i"},
       {"ProgressVariable", "ProgressVariable", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "p"}});
  dyn["StubRenderTarget"] = atomicSpec("StubRenderTarget",
      {{"command", "command", "Command", true}, {"out", "out", "Texture2D", false}});
  setDynamicSpecs(std::move(dyn));
}

// Cook a Loop graph on whichPath (0=flat, 1=resident). Returns the captured chain (caller inspects items).
bool cookLoopGraph(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, int whichPath, int loopCount,
                   RenderCommand& outChain) {
  Graph g;
  Node gv; gv.id = 1; gv.type = "GetFloatVar";
  gv.strParams["VariableName"] = kIndexName;   // value rail looks "i" up in the live ctxVars
  gv.params["FallbackDefault"] = 0.0f;         // off-scope / miss → 0
  g.nodes.push_back(gv);
  Node st; st.id = 2; st.type = "StampTranslateCmd"; g.nodes.push_back(st);
  Node lp; lp.id = 3; lp.type = "Loop";
  lp.params["Count"] = (float)loopCount;
  lp.strParams["IndexVariable"] = kIndexName;
  lp.strParams["ProgressVariable"] = kProgName;
  g.nodes.push_back(lp);
  Node rt; rt.id = 4; rt.type = "StubRenderTarget"; g.nodes.push_back(rt);

  g.connections.push_back({101, pinId(1, 0), pinId(2, 1)});  // GetFloatVar.Result → StampTranslateCmd.Tx
  g.connections.push_back({102, pinId(2, 0), pinId(3, 0)});  // StampTranslateCmd.out → Loop.SubGraph
  g.connections.push_back({103, pinId(3, 1), pinId(4, 0)});  // Loop.out → StubRenderTarget.command

  g_capturedChain = RenderCommand{};
  ContextVarMap vars;  // the live map (production's s_ctxVars analog) the driver threads in
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 64, 64);
  if (whichPath == 0) {
    pg.cook(g, ctx, nullptr, /*terminal=*/4, &vars);
  } else {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    pg.cookResident(rg, ctx, nullptr, /*StubRenderTarget path=*/"4", -1.0f, -1.0f, nullptr, &vars);
  }
  outChain = g_capturedChain;
  return true;
}

bool approx(float a, float b) { return std::fabs(a - b) < 1e-4f; }
}  // namespace

int runLoopSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-loop] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerLoopOp();                                       // the REAL op under test
  registerCmdOp("StampTranslateCmd", stampTranslateCmd);  // Layer stand-in (reads Tx param + live progress)
  registerTexOp("StubRenderTarget", stubRenderTarget);
  installLoopSpecs();

  const char* pathName[2] = {"flat", "resident"};
  bool allFaithful = true;

  // ── TOOTH A: per-iteration re-cook with a DISTINCT index — Count=3, item k position[0] == k*0.25 ──
  if (!injectBug) {
    for (int path = 0; path < 2; ++path) {
      RenderCommand chain;
      cookLoopGraph(dev, lib, q, path, /*Count=*/3, chain);
      bool ok = (chain.items.size() == 3);
      for (int k = 0; ok && k < 3; ++k) ok = approx(chain.items[(size_t)k].position[0], (float)k * 0.25f);
      allFaithful = allFaithful && ok;
      std::printf("[selftest-loop] %s Count=3 re-cook: items=%zu tx=[%.3f,%.3f,%.3f] want[0,0.25,0.5] -> %s\n",
                  pathName[path], chain.items.size(),
                  chain.items.size() > 0 ? (double)chain.items[0].position[0] : -9.0,
                  chain.items.size() > 1 ? (double)chain.items[1].position[0] : -9.0,
                  chain.items.size() > 2 ? (double)chain.items[2].position[0] : -9.0,
                  ok ? "faithful-ok" : "tripped");
    }

    // ── TOOTH B: progress formula — Count=4 → [0,1/3,2/3,1]; Count=1 → [0] (the end==1 branch) ──
    for (int path = 0; path < 2; ++path) {
      RenderCommand c4; cookLoopGraph(dev, lib, q, path, /*Count=*/4, c4);
      const float want4[4] = {0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f};
      bool ok4 = (c4.items.size() == 4);
      for (int k = 0; ok4 && k < 4; ++k) ok4 = approx(c4.items[(size_t)k].position[1], want4[k]);
      RenderCommand c1; cookLoopGraph(dev, lib, q, path, /*Count=*/1, c1);
      bool ok1 = (c1.items.size() == 1) && approx(c1.items[0].position[1], 0.0f);
      allFaithful = allFaithful && ok4 && ok1;
      std::printf("[selftest-loop] %s progress: Count=4 prog=[%.3f,%.3f,%.3f,%.3f] want[0,.333,.667,1] (%s); "
                  "Count=1 prog=%.3f want 0 (%s)\n", pathName[path],
                  c4.items.size() > 0 ? (double)c4.items[0].position[1] : -9.0,
                  c4.items.size() > 1 ? (double)c4.items[1].position[1] : -9.0,
                  c4.items.size() > 2 ? (double)c4.items[2].position[1] : -9.0,
                  c4.items.size() > 3 ? (double)c4.items[3].position[1] : -9.0, ok4 ? "ok" : "TRIP",
                  c1.items.size() > 0 ? (double)c1.items[0].position[1] : -9.0, ok1 ? "ok" : "TRIP");
    }
  } else {
    // -bug (a): cook ONCE → 1 item (not 3) on BOTH legs → RED.
    loopBugCookOnceForTest() = true;
    for (int path = 0; path < 2; ++path) {
      RenderCommand chain; cookLoopGraph(dev, lib, q, path, /*Count=*/3, chain);
      bool faithful = (chain.items.size() == 3);  // would-be faithful shape
      allFaithful = allFaithful && faithful;
      std::printf("[selftest-loop] -bug(a) cook-once %s Count=3: items=%zu (want 3 if faithful) -> %s\n",
                  pathName[path], chain.items.size(), faithful ? "green?!" : "RED");
    }
    loopBugCookOnceForTest() = false;

    // -bug (b): cook ONCE + replicate → 3 items but all position[0]=0 (no distinct index) → RED.
    loopBugReuseFirstForTest() = true;
    for (int path = 0; path < 2; ++path) {
      RenderCommand chain; cookLoopGraph(dev, lib, q, path, /*Count=*/3, chain);
      bool distinct = chain.items.size() == 3 && approx(chain.items[1].position[0], 0.25f) &&
                      approx(chain.items[2].position[0], 0.5f);
      allFaithful = allFaithful && distinct;
      std::printf("[selftest-loop] -bug(b) reuse-first %s Count=3: tx=[%.3f,%.3f,%.3f] want[0,.25,.5] if "
                  "faithful -> %s\n", pathName[path],
                  chain.items.size() > 0 ? (double)chain.items[0].position[0] : -9.0,
                  chain.items.size() > 1 ? (double)chain.items[1].position[0] : -9.0,
                  chain.items.size() > 2 ? (double)chain.items[2].position[0] : -9.0,
                  distinct ? "green?!" : "RED");
    }
    loopBugReuseFirstForTest() = false;
  }

  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-loop] FAIL: injectBug still produced the faithful shape (the re-cook is not "
                  "actually iterating with a fresh index)\n");
      return 1;
    }
    std::printf("[selftest-loop] injectBug correctly RED — (a) cook-once dropped iterations / (b) reuse-first "
                "lost the per-iteration distinct index, on BOTH legs\n");
    return 1;
  }
  std::printf("[selftest-loop] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/326, {"loop", runLoopSelfTest});

}  // namespace sw
