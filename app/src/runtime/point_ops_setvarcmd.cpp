// runtime/point_ops_setvarcmd — S3a context-var bridge: the Command-rail SetFloatVarCmd / SetIntVarCmd
// SubGraph-scope ops + the shared push/restore helper both cook drivers call, + the --selftest-setvar-scope
// HARD-GATE golden (flat AND resident legs; the resident -bug is a distinct RED tooth = the S2c blood lesson).
//
// TiXL ground truth: flow/context/SetFloatVar.cs:26-45 + SetIntVar.cs:38-64 (the SubGraph branch):
//   hadPrev = TryGetValue(name, out prev);  vars[name] = newValue;
//   SubGraph.GetValue(ctx);                                   // driver cooks the subtree here
//   if (hadPrev) vars[name] = prev;  else if (!clearAfterExecution) vars.Remove(name);
// The driver (cookCommand, flat + resident) cooks the SubGraph BETWEEN cmdVarPush and cmdVarRestore — the
// identical save/mutate/cook/restore shape S1's requestedResolution already uses in that branch. The op cook
// itself only FORWARDS the cooked subtree's items (the var effect is realized by the time it runs), exactly
// like SetRequestedResolution / Camera forward their subtree.
//
// NAMED FORK: TiXL's ONE SetFloatVar node has BOTH a no-SubGraph float write (flat) and a SubGraph scope
// (Command). sw's two-rail model can't put a Float output and a Command output on one node-spec, so the
// no-SubGraph half stayed the value-rail "SetFloatVar" (stateful_value_ops_context_vars.cpp); THIS is the
// SubGraph half as the Command type "SetFloatVarCmd". Behaviour-faithful, spelling-forked.
//
// runtime leaf: pure CPU + Metal (the golden cooks through PointGraph); no UI, no upward deps.
#include "runtime/point_ops_setvarcmd.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph / Node / NodeSpec / PortSpec / pinId / setDynamicSpecs / findSpec
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary)
#include "runtime/point_graph.h"          // CmdCookCtx / registerCmdOp / PointGraph / cookParam
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/stateful_value_ops.h"   // ContextVarMap (complete type — touch .floatVars/.intVars)
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// ───────────────────────────── push/restore helper (shared by both cook legs) ─────────────────────────────

bool isCmdContextVarWriter(const std::string& opType) {
  return opType == "SetFloatVarCmd" || opType == "SetIntVarCmd";
}

bool& setVarBugSkipWrite() {
  static bool v = false;  // OFF in production; the golden flips it around a cook then resets
  return v;
}

namespace {
float paramOr(const std::map<std::string, float>& m, const char* id, float def) {
  auto it = m.find(id);
  return it != m.end() ? it->second : def;
}
}  // namespace

CmdVarScope cmdVarPush(const std::string& opType, const std::map<std::string, float>& params,
                       const std::string& varName, ContextVarMap* vars) {
  CmdVarScope s;
  if (!vars || !isCmdContextVarWriter(opType)) return s;  // not a writer / no map → inactive
  if (varName.empty()) return s;                          // TiXL string.IsNullOrEmpty(name) → no-op
  s.active = true;
  s.name = varName;
  s.isInt = (opType == "SetIntVarCmd");
  s.clearAfter = paramOr(params, "ClearAfterExecution", 0.0f) > 0.5f;
  if (s.isInt) {
    const long newValue = (long)std::trunc(paramOr(params, "Value", 0.0f));  // C# (int) cast = truncate
    auto it = vars->intVars.find(varName);
    s.hadPrev = (it != vars->intVars.end());
    if (s.hadPrev) s.prevI = it->second;
    vars->intVars[varName] = newValue;  // context.IntVariables[name] = newValue (SetIntVar.cs:41)
  } else {
    const float newValue = paramOr(params, "FloatValue", 0.0f);
    auto it = vars->floatVars.find(varName);
    s.hadPrev = (it != vars->floatVars.end());
    if (s.hadPrev) s.prevF = it->second;
    vars->floatVars[varName] = newValue;  // context.FloatVariables[name] = newValue (SetFloatVar.cs:29)
  }
  return s;
}

void cmdVarRestore(const CmdVarScope& s, ContextVarMap* vars) {
  if (!s.active || !vars) return;
  if (s.isInt) {
    if (s.hadPrev) vars->intVars[s.name] = s.prevI;            // restore previous (SetIntVar.cs:50)
    else if (!s.clearAfter) vars->intVars.erase(s.name);       // else Remove (cs:57-58); clearAfter → leak
  } else {
    if (s.hadPrev) vars->floatVars[s.name] = s.prevF;          // restore previous (SetFloatVar.cs:35)
    else if (!s.clearAfter) vars->floatVars.erase(s.name);     // else Remove (cs:37-39); clearAfter → leak
  }
}

// ───────────────────────────── the Command ops (forward the cooked subtree) ─────────────────────────────
// Both forward cc.inputCommand (the SubGraph the driver cooked UNDER the var push) — exactly like
// SetRequestedResolution. The var write/restore already happened in the driver; this only chains the items.
RenderCommand cookSetFloatVarCmd(CmdCookCtx& c) {
  RenderCommand rc;
  if (c.inputCommand) rc.items = c.inputCommand->items;
  return rc;
}
RenderCommand cookSetIntVarCmd(CmdCookCtx& c) {
  RenderCommand rc;
  if (c.inputCommand) rc.items = c.inputCommand->items;
  return rc;
}

void registerSetVarCmdOps() {
  registerCmdOp("SetFloatVarCmd", cookSetFloatVarCmd);
  registerCmdOp("SetIntVarCmd", cookSetIntVarCmd);
}

// ───────────────────────────────────────── GOLDEN ─────────────────────────────────────────
// --selftest-setvar-scope (S3a HARD GATE, BOTH legs). Topology:
//   ProbeGetVar(1) → SetFloatVarCmd(2).SubGraph ; SetFloatVarCmd(2) → StubRenderTarget(3, the terminal)
// SetFloatVarCmd carries VariableName="k", FloatValue=0.7. When the terminal gathers its Command input it
// recurses into SetFloatVarCmd → the driver's S3a branch PUSHES ctxVars.floatVars["k"]=0.7 BEFORE cooking the
// SubGraph (ProbeGetVar), then RESTORES after. ProbeGetVar, cooked UNDER the push, reads cc.ctxVars->
// floatVars["k"] and stamps round(value*1000) into its emitted item's `count` (the wire-order witness trick
// the Execute golden uses). StubRenderTarget captures the chain.
//   FAITHFUL → captured item count == round(0.7*1000) == 700 (the Command-rail scoped write was visible).
//   -bug     → setVarBugSkipWrite() makes BOTH legs skip the push → ProbeGetVar reads the UNSET map → 0.0
//              → count == 0 ≠ 700 → RED. The resident leg is a SEPARATE assertion (the S2c blood lesson:
//              a resident-only mirror miss = prod-only black-hole; production runs the resident leg).
// Closed-form: 0.7 is the exact pushed value; 700 = round(0.7*1000). No fwidth, no off-screen, no magic.
namespace {
const char* kProbeName = "k";

// Probe Command op: read the scoped var off cc.ctxVars and stamp round(v*1000) into a 1-item chain. This is
// the EXACT TiXL mechanism a SubGraph child uses — read context.FloatVariables[name] during its own Update.
RenderCommand probeGetVarCmd(CmdCookCtx& c) {
  RenderCommand rc;
  float v = 0.0f;
  if (c.ctxVars) {
    auto it = c.ctxVars->floatVars.find(kProbeName);
    if (it != c.ctxVars->floatVars.end()) v = it->second;
  }
  rc.items.push_back(RenderDrawItem{nullptr, (uint32_t)std::lround(v * 1000.0f), 1.0f});
  return rc;
}
RenderCommand g_capturedChain;
void stubRenderTarget(TexCookCtx& c) { if (c.command) g_capturedChain = *c.command; }

NodeSpec atomicSpec(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}

// Cook the SetVar-scope graph on whichPath (0=flat, 1=resident) and return the captured item's count.
// Returns false on a structural miss (no captured item).
bool cookScope(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, int whichPath,
               uint32_t& outCount) {
  // Build: ProbeGetVar(1) → SetFloatVarCmd(2).SubGraph ; SetFloatVarCmd(2) → StubRenderTarget(3).
  Graph g;
  Node p; p.id = 1; p.type = "ProbeGetVar"; g.nodes.push_back(p);
  Node sv; sv.id = 2; sv.type = "SetFloatVarCmd";
  sv.params["FloatValue"] = 0.7f;
  sv.strParams["VariableName"] = "k";  // String channel → resident strInputs via libFromGraph
  g.nodes.push_back(sv);
  Node rt; rt.id = 3; rt.type = "StubRenderTarget"; g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // ProbeGetVar.out → SetFloatVarCmd.SubGraph
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // SetFloatVarCmd.out → StubRenderTarget.command

  g_capturedChain = RenderCommand{};
  ContextVarMap vars;  // the live map (production's s_ctxVars analog) the driver threads in
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
  if (g_capturedChain.items.empty()) return false;
  outCount = g_capturedChain.items[0].count;
  return true;
}
}  // namespace

int runSetVarScopeSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-setvar-scope] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerSetVarCmdOps();                          // the REAL ops under test
  registerCmdOp("ProbeGetVar", probeGetVarCmd);    // test probe (reads cc.ctxVars)
  registerTexOp("StubRenderTarget", stubRenderTarget);
  {
    std::map<std::string, NodeSpec> dyn;
    // SetFloatVarCmd spec: Command SubGraph in + out, VariableName(String, strDef "k" inert here — the
    // graph sets strParams), FloatValue, ClearAfterExecution. PortSpec positional tail = strDef field.
    dyn["SetFloatVarCmd"] = atomicSpec("SetFloatVarCmd",
        {{"SubGraph", "SubGraph", "Command", true},
         {"out", "out", "Command", false},
         {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "k"},
         {"FloatValue", "FloatValue", "Float", true, 0.0f, -1000.0f, 1000.0f},
         {"ClearAfterExecution", "ClearAfterExecution", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true}});
    dyn["ProbeGetVar"] = atomicSpec("ProbeGetVar", {{"out", "out", "Command", false}});
    dyn["StubRenderTarget"] = atomicSpec("StubRenderTarget",
        {{"command", "command", "Command", true}, {"out", "out", "Texture2D", false}});
    setDynamicSpecs(std::move(dyn));
  }

  setVarBugSkipWrite() = injectBug;  // ★bug = skip the Command-rail write on BOTH legs

  const uint32_t kWant = 700u;  // round(0.7 * 1000)
  bool allFaithful = true;
  const char* pathName[2] = {"flat", "resident"};
  uint32_t got[2] = {0u, 0u};
  bool structOk[2] = {false, false};
  for (int path = 0; path < 2; ++path) {
    structOk[path] = cookScope(dev, lib, q, path, got[path]);
    bool faithful = structOk[path] && got[path] == kWant;
    allFaithful = allFaithful && faithful;
    std::printf("[selftest-setvar-scope] %s: scopedCount=%u(want %u) struct=%s -> %s\n",
                pathName[path], got[path], kWant, structOk[path] ? "ok" : "NO-ITEM",
                faithful ? "faithful-ok" : "tripped");
  }

  setVarBugSkipWrite() = false;  // reset the global (process hygiene)
  setDynamicSpecs({});           // drop the injected test specs
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    // Both legs must FAIL the write-skip (each a distinct tooth: a resident-only mirror miss = prod black-hole).
    if (allFaithful) {
      std::printf("[selftest-setvar-scope] FAIL: injectBug still passed (the Command-rail write happened "
                  "despite skip — the seam is not actually pushing the var)\n");
      return 1;
    }
    // Confirm BOTH legs read the unset map (count 0), not just one — the resident tooth is separate.
    bool flatBit = !structOk[0] || got[0] != kWant;
    bool resBit = !structOk[1] || got[1] != kWant;
    std::printf("[selftest-setvar-scope] injectBug correctly RED (flat tooth=%s resident tooth=%s — "
                "skipped write → ProbeGetVar read the UNSET map)\n",
                flatBit ? "RED" : "green?!", resBit ? "RED" : "green?!");
    return 1;
  }
  std::printf("[selftest-setvar-scope] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/320, {"setvar-scope", runSetVarScopeSelfTest});

}  // namespace sw
