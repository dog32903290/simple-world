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
bool& getIntVarFallbackBug() {
  static bool v = false;  // OFF in production; the GetIntVar tooth flips it to simulate pre-fix (no trunc)
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

// ─────────────── S3b: value↔command LIVE-READ scope (closes the S3a hollow) ───────────────
// The ambient live map a value-rail Get*Var resolves against while cooked under a SetVarCmd scope. thread_local
// so a future multi-threaded cook can't leak one cook's scope into another (today the cook is single-threaded;
// thread_local is the correct-by-construction choice and costs nothing on the single-thread path). nullptr = no
// scope active → Get*Var reads its normal value-rail value (faithful: off-scope behaviour is unchanged).
static thread_local ContextVarMap* t_liveCtxVars = nullptr;

LiveCtxVarScope::LiveCtxVarScope(ContextVarMap* vars)
    : prev_(t_liveCtxVars), engaged_(vars != nullptr) {
  // Only ENGAGE when a real map is handed in (an inactive scope / non-writer leaves the prior live map intact —
  // so SetRequestedResolution or a non-writer Command wrapping a GetFloatVar still sees the OUTER scope's vars,
  // matching TiXL's one ambient dictionary that an inner non-push node passes through transparently).
  if (engaged_) t_liveCtxVars = vars;
}
LiveCtxVarScope::~LiveCtxVarScope() {
  if (engaged_) t_liveCtxVars = prev_;  // pop back to the enclosing scope (nests like TiXL stacked pushes)
}
ContextVarMap* liveCtxVars() { return t_liveCtxVars; }

bool isValueRailContextVarReader(const std::string& opType) {
  return opType == "GetFloatVar" || opType == "GetIntVar";
}

float liveGetVar(const std::string& opType, const std::string& varName, float fallback, bool* found) {
  if (found) *found = false;
  ContextVarMap* live = t_liveCtxVars;
  if (!live || !isValueRailContextVarReader(opType)) return fallback;  // no scope / not a reader → unchanged
  if (varName.empty()) return fallback;                                // empty name → normal lookup miss
  if (opType == "GetIntVar") {
    auto it = live->intVars.find(varName);
    if (it == live->intVars.end())
      return getIntVarFallbackBug() ? fallback : (float)(long)std::trunc(fallback);  // unset → trunc(FallbackValue) — TiXL Slot<int> + stepGetIntVar
    if (found) *found = true;
    return (float)it->second;                                          // TiXL Slot<int>; stored truncated
  }
  auto it = live->floatVars.find(varName);
  if (it == live->floatVars.end()) return fallback;                    // unset → FallbackDefault
  if (found) *found = true;
  return it->second;                                                   // TryGetValue hit (GetFloatVar.cs)
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
// --selftest-setvar-scope (S3a/S3b HARD GATE, BOTH legs). TWO teeth, each its own topology:
//
// TOOTH A (S3a, the probe tooth — the Command-rail WRITE happened): kept for direct coverage.
//   ProbeGetVar(1) → SetFloatVarCmd(2).SubGraph ; SetFloatVarCmd(2) → StubRenderTarget(3, terminal).
//   ProbeGetVar is a COMMAND op that reads cc.ctxVars->floatVars["k"] and stamps round(v*1000). It proves the
//   driver PUSHED the var around the SubGraph. (cc.ctxVars is the same map S3a threaded.)
//
// TOOTH B (S3b, THE HOLLOW-CLOSING tooth — a VALUE-RAIL Get*Var reads the LIVE scope): the blueprint's original
// proving op S3a could NOT do. A value-rail GetFloatVar("k") (evaluate==nullptr) drives the Float param of a
// node cooked INSIDE the SubGraph:
//   GetFloatVar(1, value-rail) → V param of StampParamCmd(4) ; StampParamCmd(4) → SetFloatVarCmd(2).SubGraph ;
//   SetFloatVarCmd(2) → StubRenderTarget(3). SetFloatVarCmd carries VariableName="k", FloatValue=0.7.
//   StampParamCmd stamps round(params["V"]*1000) into its item count. Its V is wired to GetFloatVar.Result, so
//   the value rail RESOLVES GetFloatVar while it is cooked under the live scope → reads ctxVars["k"]=0.7 → V=0.7.
//   This is EXACTLY "a Layer whose param is driven by a value-rail GetFloatVar" carrying the scoped 0.7.
//
//   FAITHFUL → both teeth's captured count == round(0.7*1000) == 700.
//   -bug     → setVarBugSkipWrite() skips the push on BOTH legs → the live scope never engages → ProbeGetVar
//              reads the UNSET map AND GetFloatVar's value rail falls back to its FROZEN value (FallbackDefault=0,
//              no cookStatefulValueNodes in this headless test) → count 0 ≠ 700 → RED on every leg×tooth.
// Closed-form: 0.7 exact; 700 = round(0.7*1000). No fwidth, no off-screen, no magic. Each leg is a SEPARATE
// assertion (S2c blood lesson: a resident-only miss = prod-only black-hole; production runs the resident leg).
namespace {
const char* kProbeName = "k";

// TOOTH A probe: COMMAND op, reads the scoped var off cc.ctxVars (the S3a Command-rail channel).
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
// TOOTH B stamp: COMMAND op standing in for a Layer — stamps round(params["V"]*1000). Its V param is wired to a
// VALUE-RAIL GetFloatVar, so cc.params["V"] is whatever the value rail resolved for GetFloatVar (the live scoped
// var under the push, the frozen fallback off-scope). This op itself touches NO ctxVars — the live-read is done
// entirely by the value-rail reader (evalFloat/evalResidentFloat) BEFORE this op's params are handed to it.
RenderCommand stampParamCmd(CmdCookCtx& c) {
  RenderCommand rc;
  float v = 0.0f;
  if (c.params) { auto it = c.params->find("V"); if (it != c.params->end()) v = it->second; }
  rc.items.push_back(RenderDrawItem{nullptr, (uint32_t)std::lround(v * 1000.0f), 1.0f});
  return rc;
}
RenderCommand g_capturedChain;
void stubRenderTarget(TexCookCtx& c) { if (c.command) g_capturedChain = *c.command; }

NodeSpec atomicSpec(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}

// Cook a SetVar-scope graph on whichPath (0=flat, 1=resident) and return the captured item's count. `valueRail`
// selects the tooth: false = TOOTH A (ProbeGetVar Command reads cc.ctxVars), true = TOOTH B (a value-rail
// GetFloatVar drives StampParamCmd's V — the hollow-closing proof). Returns false on a structural miss.
bool cookScope(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, int whichPath, bool valueRail,
               uint32_t& outCount) {
  Graph g;
  Node sv; sv.id = 2; sv.type = "SetFloatVarCmd";
  sv.params["FloatValue"] = 0.7f;
  sv.strParams["VariableName"] = "k";  // String channel → resident strInputs via libFromGraph
  g.nodes.push_back(sv);
  Node rt; rt.id = 3; rt.type = "StubRenderTarget"; g.nodes.push_back(rt);
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // SetFloatVarCmd.out → StubRenderTarget.command
  if (!valueRail) {
    // TOOTH A: ProbeGetVar(1) → SetFloatVarCmd(2).SubGraph
    Node p; p.id = 1; p.type = "ProbeGetVar"; g.nodes.push_back(p);
    g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  } else {
    // TOOTH B: GetFloatVar(1, value rail) → StampParamCmd(4).V ; StampParamCmd(4) → SetFloatVarCmd(2).SubGraph
    Node gv; gv.id = 1; gv.type = "GetFloatVar";
    gv.strParams["VariableName"] = "k";   // the value rail looks this name up in the live ctxVars
    gv.params["FallbackDefault"] = 0.0f;  // miss / off-scope → 0 (so -bug bites to 0, not 700)
    g.nodes.push_back(gv);
    Node st; st.id = 4; st.type = "StampParamCmd"; g.nodes.push_back(st);
    g.connections.push_back({103, pinId(1, 0), pinId(4, 1)});  // GetFloatVar.Result → StampParamCmd.V
    g.connections.push_back({101, pinId(4, 0), pinId(2, 0)});  // StampParamCmd.out → SetFloatVarCmd.SubGraph
  }

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
  registerCmdOp("ProbeGetVar", probeGetVarCmd);    // TOOTH A probe (reads cc.ctxVars)
  registerCmdOp("StampParamCmd", stampParamCmd);   // TOOTH B stamp (reads its V param = the value rail's read)
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
    // TOOTH B: a real VALUE-RAIL GetFloatVar spec (evaluate==nullptr, Result port FIRST, VariableName String +
    // FallbackDefault Float) — the SAME shape as the production node_registry_math_contextvar.cpp GetFloatVar.
    dyn["GetFloatVar"] = atomicSpec("GetFloatVar",
        {{"Result", "Result", "Float", false},
         {"VariableName", "VariableName", "String", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, false, "k"},
         {"FallbackDefault", "FallbackDefault", "Float", true, 0.0f, -1000.0f, 1000.0f}});
    // TOOTH B: StampParamCmd — a Command op with a Float "V" input (stands in for a Layer's param), wired to
    // GetFloatVar.Result; it stamps round(V*1000). V is port index 1 (after the leading "out" Command port).
    dyn["StampParamCmd"] = atomicSpec("StampParamCmd",
        {{"out", "out", "Command", false},
         {"V", "V", "Float", true, 0.0f, -1000.0f, 1000.0f}});
    dyn["StubRenderTarget"] = atomicSpec("StubRenderTarget",
        {{"command", "command", "Command", true}, {"out", "out", "Texture2D", false}});
    setDynamicSpecs(std::move(dyn));
  }

  setVarBugSkipWrite() = injectBug;  // ★bug = skip the Command-rail write → live scope never engages, BOTH legs

  const uint32_t kWant = 700u;  // round(0.7 * 1000)
  bool allFaithful = true;
  const char* pathName[2] = {"flat", "resident"};
  const char* toothName[2] = {"A:probe-cmd-read", "B:value-rail-GetVar-under-scope"};
  uint32_t got[2][2] = {{0u, 0u}, {0u, 0u}};       // [tooth][leg]
  bool structOk[2][2] = {{false, false}, {false, false}};
  for (int tooth = 0; tooth < 2; ++tooth) {
    for (int path = 0; path < 2; ++path) {
      structOk[tooth][path] = cookScope(dev, lib, q, path, /*valueRail=*/tooth == 1, got[tooth][path]);
      bool faithful = structOk[tooth][path] && got[tooth][path] == kWant;
      allFaithful = allFaithful && faithful;
      std::printf("[selftest-setvar-scope] tooth %s / %s: scopedCount=%u(want %u) struct=%s -> %s\n",
                  toothName[tooth], pathName[path], got[tooth][path], kWant,
                  structOk[tooth][path] ? "ok" : "NO-ITEM", faithful ? "faithful-ok" : "tripped");
    }
  }

  // TOOTH C — GetIntVar fallback-truncation gate (guards the liveGetVar fallback-miss fix).
  // Directly calls liveGetVar("GetIntVar") under a live scope with an EMPTY intVars map (no "q" key →
  // fallback-miss path). FallbackValue=7.9 (fractional). Faithful: result==7 (trunc). Bug: result==7.9.
  // Closed-form: trunc(7.9)=7. Both flat AND resident paths reach the same liveGetVar codepath; one
  // call covers both (the scope is thread_local, path-agnostic). getIntVarFallbackBug() = injectBug here.
  getIntVarFallbackBug() = injectBug;
  {
    ContextVarMap emptyVars;  // intVars is empty → fallback-miss
    LiveCtxVarScope scopeC(&emptyVars);
    const float fallbackC = 7.9f;
    float gotC = liveGetVar("GetIntVar", "q", fallbackC, nullptr);
    bool faithfulC = (gotC == 7.0f);  // trunc(7.9)=7; un-truncated pre-fix would return 7.9
    allFaithful = allFaithful && faithfulC;
    std::printf("[selftest-setvar-scope] tooth C:GetIntVar-fallback-trunc: got=%.6f (want 7.0 trunc) -> %s\n",
                (double)gotC, faithfulC ? "faithful-ok" : "tripped");
  }
  getIntVarFallbackBug() = false;

  setVarBugSkipWrite() = false;  // reset the global (process hygiene)
  setDynamicSpecs({});           // drop the injected test specs
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    // Every leg×tooth must FAIL: write-skip (A/B) and fallback-no-trunc (C).
    if (allFaithful) {
      std::printf("[selftest-setvar-scope] FAIL: injectBug still passed (the var was read despite the skip — "
                  "the seam is not actually scoping/reading the var)\n");
      return 1;
    }
    for (int tooth = 0; tooth < 2; ++tooth) {
      bool flatBit = !structOk[tooth][0] || got[tooth][0] != kWant;
      bool resBit = !structOk[tooth][1] || got[tooth][1] != kWant;
      std::printf("[selftest-setvar-scope] injectBug correctly RED — tooth %s (flat=%s resident=%s; skipped "
                  "write → no live scope → reader saw the UNSET map / frozen fallback)\n",
                  toothName[tooth], flatBit ? "RED" : "green?!", resBit ? "RED" : "green?!");
    }
    std::printf("[selftest-setvar-scope] injectBug correctly RED — tooth C:GetIntVar-fallback-trunc "
                "(pre-fix returned raw 7.9, not trunc 7)\n");
    return 1;
  }
  std::printf("[selftest-setvar-scope] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/320, {"setvar-scope", runSetVarScopeSelfTest});

}  // namespace sw
