// app/frame_cook_setboolvar_selftest — the bool context-var write-then-read golden (--selftest-setboolvar).
//
// WAVE-1 flow-island leaf riding the S3 live-read seam. The TWO-RAIL black-hole guard (S2c blood lesson):
// SetBoolVar landed on BOTH rails, and a reader on each rail reads back the written bool. Bool rides the INT
// channel as 0/1 (sw has no boolVars dict — NAMED FORK vs TiXL context.BoolVariables).
//
//   RAIL 1 (value-rail, no-SubGraph): SetBoolVar("b",1) → ContextVarMap.intVars → GetBoolVar("b") reads 1,
//           through the REAL cookStatefulValueNodes 2-pass (the same flat-map seam the value-rail SetFloatVar/
//           GetFloatVar use). Cooked on the RESIDENT graph (production runs resident; ResidentNode.extOut).
//           A GetBoolVar("missing", fallback=1) reads 1 (TryGetValue miss → FallbackDefault coercion).
//   RAIL 2 (command-rail, SubGraph): SetBoolVarCmd("b",1) SubGraph scope → a value-rail GetBoolVar("b") cooked
//           UNDER the live scope reads 1, asserted on BOTH the flat AND resident PointGraph cook legs.
//
//   FAITHFUL → every reader reads 1.
//   -bug     → RAIL 1: the writer aims at "b_BUG" so the real cook writes the wrong key → GetBoolVar("b")
//              misses the map → reads its FallbackDefault=0 → RED.  RAIL 2: setVarBugSkipWrite() skips the
//              Command-rail push on both legs → the live scope never engages → GetBoolVar falls back to 0 → RED.
//
// app leaf: drives the REAL seams (cookStatefulValueNodes + PointGraph), not mocks.
#include "app/frame_cook.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // findSpec / Graph / Node / pinId / setDynamicSpecs
#include "runtime/graph_bridge.h"         // atomicSymbolFromSpec / libFromGraph
#include "runtime/point_graph.h"          // PointGraph / CmdCookCtx / registerCmdOp / registerTexOp
#include "runtime/point_ops_setvarcmd.h"  // registerSetVarCmdOps / setVarBugSkipWrite
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / initResidentCache / ResidentNode
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/stateful_value_ops.h"   // ContextVarMap / StatefulValueState
#include "runtime/tixl_point.h"           // EvaluationContext
#include "runtime/transport.h"            // Transport

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

int g_sbvFail = 0;
void sbvExpect(const char* what, bool ok) {
  if (!ok) { ++g_sbvFail; std::printf("  [setboolvar] FAIL %s\n", what); }
  else std::printf("  [setboolvar] ok   %s\n", what);
}

SymbolChild sbvChild(int id, const char* type, const std::string& name) {
  SymbolChild c; c.id = id; c.symbolId = type;
  if (!name.empty()) c.strOverrides["VariableName"] = name;  // real string sub-seam channel
  return c;
}
void sbvEnsureSymbols(SymbolLibrary& lib) {
  for (const char* t : {"SetBoolVar", "GetBoolVar"})
    if (const NodeSpec* s = findSpec(t)) lib.symbols[t] = atomicSymbolFromSpec(*s);
}
float sbvExtOut0(const ResidentEvalGraph& g, const char* path) {
  const ResidentNode* n = g.node(path); return n ? n->extOut[0] : -999.0f;
}

// ── RAIL 2 (command-rail) harness: cook a SetBoolVarCmd scope with a value-rail GetBoolVar inside ──
RenderCommand sbvStampParamCmd(CmdCookCtx& c) {
  RenderCommand rc; float v = 0.0f;
  if (c.params) { auto it = c.params->find("V"); if (it != c.params->end()) v = it->second; }
  rc.items.push_back(RenderDrawItem{nullptr, (uint32_t)std::lround(v * 1000.0f), 1.0f});
  return rc;
}
RenderCommand g_sbvChain;
void sbvStubRenderTarget(TexCookCtx& c) { if (c.command) g_sbvChain = *c.command; }
NodeSpec sbvAtomic(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr; return s;
}
bool sbvCookCmdScope(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, int whichPath, uint32_t& out) {
  Graph g;
  Node sv; sv.id = 2; sv.type = "SetBoolVarCmd"; sv.params["BoolValue"] = 1.0f;
  sv.strParams["VariableName"] = "b"; g.nodes.push_back(sv);
  Node rt; rt.id = 3; rt.type = "StubRenderTarget"; g.nodes.push_back(rt);
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});
  Node gv; gv.id = 1; gv.type = "GetBoolVar"; gv.strParams["VariableName"] = "b";
  gv.params["FallbackDefault"] = 0.0f; g.nodes.push_back(gv);
  Node st; st.id = 4; st.type = "StampParamCmd"; g.nodes.push_back(st);
  g.connections.push_back({103, pinId(1, 0), pinId(4, 1)});
  g.connections.push_back({101, pinId(4, 0), pinId(2, 0)});
  g_sbvChain = RenderCommand{};
  ContextVarMap vars; EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 64, 64);
  if (whichPath == 0) pg.cook(g, ctx, nullptr, 3, &vars);
  else {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    pg.cookResident(rg, ctx, nullptr, "3", -1.0f, -1.0f, nullptr, &vars);
  }
  if (g_sbvChain.items.empty()) return false;
  out = g_sbvChain.items[0].count; return true;
}
void sbvInstallCmdSpecs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["SetBoolVarCmd"] = sbvAtomic("SetBoolVarCmd",
      {{"SubGraph", "SubGraph", "Command", true}, {"out", "out", "Command", false},
       {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "b"},
       {"BoolValue", "BoolValue", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
       {"ClearAfterExecution", "ClearAfterExecution", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}});
  dyn["GetBoolVar"] = sbvAtomic("GetBoolVar",
      {{"Result", "Result", "Float", false},
       {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "b"},
       {"FallbackDefault", "FallbackDefault", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}});
  dyn["StampParamCmd"] = sbvAtomic("StampParamCmd",
      {{"out", "out", "Command", false}, {"V", "V", "Float", true, 0.0f, -1000.0f, 1000.0f}});
  dyn["StubRenderTarget"] = sbvAtomic("StubRenderTarget",
      {{"command", "command", "Command", true}, {"out", "out", "Texture2D", false}});
  setDynamicSpecs(std::move(dyn));
}

}  // namespace

namespace framecook {

int runSetBoolVarSelfTest(bool injectBug) {
  g_sbvFail = 0;
  std::printf("[selftest] setboolvar (bool context-var two-rail write-then-read; bool rides intVars 0/1)\n");

  // ===== RAIL 1: value-rail no-SubGraph SetBoolVar → GetBoolVar through the REAL cookStatefulValueNodes. ====
  // Children: 1=SetBoolVar("b")=1, 2=GetBoolVar("b") fb 0, 3=GetBoolVar("missing") fb 1. injectBug aims the
  // writer at "b_BUG" so the real cook writes the wrong key → GetBoolVar("b") misses → reads 0 → RED.
  {
    SymbolLibrary lib; sbvEnsureSymbols(lib);
    Symbol root; root.id = "R"; root.name = "R"; root.atomic = false;
    SymbolChild setB = sbvChild(1, "SetBoolVar", injectBug ? "b_BUG" : "b");
    setB.overrides["BoolValue"] = 1.0f;
    SymbolChild getB = sbvChild(2, "GetBoolVar", "b");       getB.overrides["FallbackDefault"] = 0.0f;
    SymbolChild getM = sbvChild(3, "GetBoolVar", "missing"); getM.overrides["FallbackDefault"] = 1.0f;
    root.children = {setB, getB, getM}; root.nextChildId = 4;
    lib.symbols["R"] = root; lib.rootId = "R";

    ResidentEvalGraph g = buildEvalGraph(lib, lib.rootId);
    initResidentCache(g);
    // string sub-seam projected the var name (not a float hack).
    const char* setName = injectBug ? "b_BUG" : "b";
    const ResidentNode* gn = g.node("1");
    sbvExpect("string sub-seam: SetBoolVar.strInputs[\"VariableName\"] projected via strOverrides",
              gn && gn->strInputs.count("VariableName") && gn->strInputs.at("VariableName") == setName);

    ContextVarMap vars; std::map<std::string, StatefulValueState> state;
    Transport t; t.bpm = 120.0; t.rate = 1.0; t.play();
    cookStatefulValueNodes(g, 1.0f / 60.0f, 0.0f, 0.0, t, 0, &lib, state, vars, /*bug=*/0);

    // Expectation stays CORRECT (1). -bug corrupts the REAL cook (writer aimed at "b_BUG") → reader misses → 0.
    sbvExpect("RAIL1 value-rail: SetBoolVar(\"b\",1)→GetBoolVar(\"b\")==1 (writer→intVars→reader)",
              std::fabs(sbvExtOut0(g, "2") - 1.0f) < 1e-5f);
    sbvExpect("RAIL1 echo: SetBoolVar.Output echoes 1", std::fabs(sbvExtOut0(g, "1") - 1.0f) < 1e-5f);
    sbvExpect("RAIL1 unset: GetBoolVar(\"missing\", fb 1)==1 (TryGetValue miss → fallback coerce)",
              std::fabs(sbvExtOut0(g, "3") - 1.0f) < 1e-5f);
  }

  // ===== RAIL 2: command-rail SetBoolVarCmd SubGraph → a value-rail GetBoolVar reads the live scope. =======
  // Asserted on BOTH the flat AND resident PointGraph legs (the black-hole guard — production runs resident).
  // -bug: setVarBugSkipWrite() skips the Command-rail push → live scope never engages → GetBoolVar reads 0.
  {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    MTL::CommandQueue* q = dev->newCommandQueue();
    NS::Error* err = nullptr;
    MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
    if (!lib) {
      std::printf("  [setboolvar] FAIL RAIL2: no metallib\n"); ++g_sbvFail;
      q->release(); dev->release(); pool->release();
    } else {
      registerSetVarCmdOps();
      registerCmdOp("StampParamCmd", sbvStampParamCmd);
      registerTexOp("StubRenderTarget", sbvStubRenderTarget);
      sbvInstallCmdSpecs();
      setVarBugSkipWrite() = injectBug;

      const uint32_t kWant = 1000u;  // round(1 * 1000)
      const char* pathName[2] = {"flat", "resident"};
      for (int path = 0; path < 2; ++path) {
        uint32_t got = 0u;
        bool ok = sbvCookCmdScope(dev, lib, q, path, got);
        char msg[160];
        std::snprintf(msg, sizeof(msg),
                      "RAIL2 command-rail %s leg: GetBoolVar under SetBoolVarCmd scope reads 1 (count==1000)",
                      pathName[path]);
        sbvExpect(msg, ok && got == kWant);
      }

      setVarBugSkipWrite() = false;
      setDynamicSpecs({});
      lib->release(); q->release(); dev->release(); pool->release();
    }
  }

  std::printf("[selftest] setboolvar %s (%d failures)\n", g_sbvFail ? "FAIL" : "PASS", g_sbvFail);
  return g_sbvFail ? 1 : 0;
}

}  // namespace framecook

REGISTER_SELFTESTS(/*orderBase=*/336, {"setboolvar", framecook::runSetBoolVarSelfTest});

}  // namespace sw
