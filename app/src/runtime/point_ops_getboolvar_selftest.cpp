// runtime/point_ops_getboolvar_selftest — the bool context-var live-read golden (--selftest-getboolvar).
//
// WAVE-1 flow-island leaf riding the S3 live-read seam. Proves a VALUE-RAIL GetBoolVar resolves the LIVE
// ambient context-var map while it is cooked UNDERNEATH a Command-rail SetBoolVarCmd SubGraph scope — the
// same hollow-closing shape as --selftest-setvar-scope tooth B, but on the BOOL channel (which rides intVars
// 0/1; sw has no boolVars dict — NAMED FORK vs TiXL context.BoolVariables).
//
// Topology (mirrors setvar-scope tooth B):
//   GetBoolVar(1, value rail, name "k", FallbackDefault=0) → V param of StampParamCmd(4) ;
//   StampParamCmd(4) → SetBoolVarCmd(2).SubGraph ; SetBoolVarCmd(2) → StubRenderTarget(3).
//   SetBoolVarCmd carries VariableName="k", BoolValue=1. StampParamCmd stamps round(V*1000).
//   The value rail RESOLVES GetBoolVar under the live scope → reads intVars["k"]=1 → V=1 → count 1000.
//
//   FAITHFUL → captured count == 1000 (1*1000) on BOTH legs (flat + resident — S2c blood lesson: production
//              runs the RESIDENT leg, so a resident-only miss = a prod-only black-hole).
//   -bug     → setVarBugSkipWrite() skips the Command-rail push on BOTH legs → the live scope never engages →
//              GetBoolVar's value rail falls back to its FROZEN FallbackDefault=0 → count 0 ≠ 1000 → RED.
//
// TOOTH C (fallback miss): liveGetVar("GetBoolVar","q", fallback) under a live scope with an EMPTY intVars map
//   → the bool coercion of the fallback (FallbackDefault 2.0 → !=0 ⇒ 1.0). Faithful: 1.0. Closed-form, no GPU.
//
// runtime leaf: pure CPU + Metal (cooks through PointGraph); no UI, no upward deps.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph / Node / NodeSpec / PortSpec / pinId / setDynamicSpecs
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary)
#include "runtime/point_graph.h"          // CmdCookCtx / registerCmdOp / PointGraph
#include "runtime/point_ops_setvarcmd.h"  // registerSetVarCmdOps / setVarBugSkipWrite / liveGetVar / LiveCtxVarScope
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/stateful_value_ops.h"   // ContextVarMap
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// StampParamCmd stands in for a Layer whose Float param is wired to the value rail's GetBoolVar.Result. It
// stamps round(params["V"]*1000) — V is whatever the value rail resolved (the live scoped 1, the frozen 0).
RenderCommand gbvStampParamCmd(CmdCookCtx& c) {
  RenderCommand rc;
  float v = 0.0f;
  if (c.params) { auto it = c.params->find("V"); if (it != c.params->end()) v = it->second; }
  rc.items.push_back(RenderDrawItem{nullptr, (uint32_t)std::lround(v * 1000.0f), 1.0f});
  return rc;
}
RenderCommand g_gbvChain;
void gbvStubRenderTarget(TexCookCtx& c) { if (c.command) g_gbvChain = *c.command; }

NodeSpec gbvAtomic(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}

// Cook the SetBoolVarCmd-scope graph on whichPath (0=flat, 1=resident); returns the captured stamp count.
bool gbvCookScope(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, int whichPath, uint32_t& outCount) {
  Graph g;
  Node sv; sv.id = 2; sv.type = "SetBoolVarCmd";
  sv.params["BoolValue"] = 1.0f;
  sv.strParams["VariableName"] = "k";
  g.nodes.push_back(sv);
  Node rt; rt.id = 3; rt.type = "StubRenderTarget"; g.nodes.push_back(rt);
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // SetBoolVarCmd.out → StubRenderTarget.command

  Node gv; gv.id = 1; gv.type = "GetBoolVar";
  gv.strParams["VariableName"] = "k";   // the value rail looks this name up in the live intVars
  gv.params["FallbackDefault"] = 0.0f;  // miss / off-scope → 0 (so -bug bites to 0, not 1000)
  g.nodes.push_back(gv);
  Node st; st.id = 4; st.type = "StampParamCmd"; g.nodes.push_back(st);
  g.connections.push_back({103, pinId(1, 0), pinId(4, 1)});  // GetBoolVar.Result → StampParamCmd.V
  g.connections.push_back({101, pinId(4, 0), pinId(2, 0)});  // StampParamCmd.out → SetBoolVarCmd.SubGraph

  g_gbvChain = RenderCommand{};
  ContextVarMap vars;
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 64, 64);
  if (whichPath == 0) {
    pg.cook(g, ctx, nullptr, /*terminal=*/3, &vars);
  } else {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    pg.cookResident(rg, ctx, nullptr, /*StubRenderTarget path=*/"3", -1.0f, -1.0f, nullptr, &vars);
  }
  if (g_gbvChain.items.empty()) return false;
  outCount = g_gbvChain.items[0].count;
  return true;
}

void gbvInstallSpecs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["SetBoolVarCmd"] = gbvAtomic("SetBoolVarCmd",
      {{"SubGraph", "SubGraph", "Command", true},
       {"out", "out", "Command", false},
       {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "k"},
       {"BoolValue", "BoolValue", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
       {"ClearAfterExecution", "ClearAfterExecution", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}});
  // A real VALUE-RAIL GetBoolVar spec (evaluate==nullptr, Result port FIRST) — SAME shape as production
  // node_registry_math_contextvar.cpp GetBoolVar.
  dyn["GetBoolVar"] = gbvAtomic("GetBoolVar",
      {{"Result", "Result", "Float", false},
       {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "k"},
       {"FallbackDefault", "FallbackDefault", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}});
  dyn["StampParamCmd"] = gbvAtomic("StampParamCmd",
      {{"out", "out", "Command", false},
       {"V", "V", "Float", true, 0.0f, -1000.0f, 1000.0f}});
  dyn["StubRenderTarget"] = gbvAtomic("StubRenderTarget",
      {{"command", "command", "Command", true}, {"out", "out", "Texture2D", false}});
  setDynamicSpecs(std::move(dyn));
}

}  // namespace

int runGetBoolVarSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-getboolvar] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerSetVarCmdOps();                            // the REAL SetBoolVarCmd command op
  registerCmdOp("StampParamCmd", gbvStampParamCmd);  // reads its V param = the value rail's read
  registerTexOp("StubRenderTarget", gbvStubRenderTarget);
  gbvInstallSpecs();

  setVarBugSkipWrite() = injectBug;  // ★bug = skip the Command-rail write → live scope never engages, BOTH legs

  const uint32_t kWant = 1000u;  // round(1 * 1000) — the live scoped bool true
  bool allFaithful = true;
  const char* pathName[2] = {"flat", "resident"};
  uint32_t got[2] = {0u, 0u};
  bool structOk[2] = {false, false};
  for (int path = 0; path < 2; ++path) {
    structOk[path] = gbvCookScope(dev, lib, q, path, got[path]);
    bool faithful = structOk[path] && got[path] == kWant;
    allFaithful = allFaithful && faithful;
    std::printf("[selftest-getboolvar] value-rail GetBoolVar under live SetBoolVarCmd scope / %s: "
                "scopedCount=%u(want %u) struct=%s -> %s\n",
                pathName[path], got[path], kWant, structOk[path] ? "ok" : "NO-ITEM",
                faithful ? "faithful-ok" : "tripped");
  }

  // TOOTH C — fallback-miss bool coercion: liveGetVar("GetBoolVar","q", 2.0) under a live scope with an EMPTY
  // intVars map → fallback !=0 ⇒ 1.0. Faithful: 1.0. (A 0 fallback would yield 0.0 — exercised by the -bug path
  // collapsing the scoped read to its 0 fallback above.) Path-agnostic (thread_local scope), one call covers both.
  {
    ContextVarMap emptyVars;  // intVars empty → fallback-miss
    LiveCtxVarScope scopeC(&emptyVars);
    const float gotC = liveGetVar("GetBoolVar", "q", /*fallback=*/2.0f, nullptr);
    const bool faithfulC = (gotC == 1.0f);  // 2.0 !=0 ⇒ 1.0
    allFaithful = allFaithful && faithfulC;
    std::printf("[selftest-getboolvar] tooth C:fallback-bool-coerce: liveGetVar(\"q\",fb=2.0)=%.3f (want 1.0) -> %s\n",
                (double)gotC, faithfulC ? "faithful-ok" : "tripped");
  }

  setVarBugSkipWrite() = false;  // reset (process hygiene)
  setDynamicSpecs({});           // drop the injected test specs
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-getboolvar] FAIL: injectBug still passed (the bool var was read despite the "
                  "skip — the seam is not actually scoping/reading the var)\n");
      return 1;
    }
    for (int path = 0; path < 2; ++path) {
      bool bit = !structOk[path] || got[path] != kWant;
      std::printf("[selftest-getboolvar] injectBug correctly RED — %s leg (%s; skipped write → no live scope "
                  "→ GetBoolVar saw the frozen FallbackDefault=0)\n",
                  pathName[path], bit ? "RED" : "green?!");
    }
    return 1;
  }
  std::printf("[selftest-getboolvar] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/334, {"getboolvar", runGetBoolVarSelfTest});

}  // namespace sw
