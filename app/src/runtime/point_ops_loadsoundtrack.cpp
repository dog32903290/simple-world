// runtime/point_ops_loadsoundtrack — LoadSoundtrack: TiXL's soundtrack ANCHOR op. At RUNTIME it is
// the Execute twin — a MultiInput Command port whose wired chains concatenate in wire order, gated
// by IsEnabled. That is the WHOLE .cs:
//
// TiXL ground truth: flow/LoadSoundtrack.cs:14-39 (the Update — verbatim the Execute.cs loop):
//   var commands = Command.CollectedInputs;
//   if (IsEnabled.GetValue(context)) {
//     for i: commands[i].PrepareAction(ctx);   // prepare ALL
//     for i: commands[i].GetValue(ctx);        // execute ALL  ← the draws
//     for i: commands[i].RestoreAction(ctx);   // restore ALL
//   }
//   Command.DirtyFlag.Clear();
//   Inputs: Command = MultiInputSlot<Command>; IsEnabled = InputSlot<bool>.
//   LoadSoundtrack.t3 (re-read & confirmed): IsEnabled DefaultValue = true.
//
// ★WHERE IS THE AUDIO? NOT in the op — in TiXL the soundtrack FILE lives in the composition's
// PlaybackSettings (editor-side; the op instance is the timeline's anchor for it). sw has already
// built that machine as the composition-level soundtrack: `lib.composition.soundtrackPath` +
// app/soundtrack.{h,cpp} (the transport-follow rule, resync thresholds — the whole
// SoundtrackClipStream.cs port, --selftest-soundtrack). This op therefore wires NO audio: it is the
// faithful Command forwarder the .cs is, and the audio attachment rides the EXISTING app-side
// machine. NAMED FORK fork-loadsoundtrack-settings-attachment: TiXL's editor associates the
// PlaybackSettings audio clip with this op instance (timeline anchor UI); sw's soundtrackPath is a
// composition field with no per-op anchor — the graph-side anchor affordance is deferred to the
// editor lane (zero runtime behavior difference: in BOTH systems the op's cook only forwards
// commands, and playback is driven by the settings machine, not the op).
//
// ★COOK-CORE HOOK: NONE. The MultiInput Command port rides the driver's existing generic collector
// (cookCommand's Command-input branch — concat all wires in wire order); this op only applies the
// IsEnabled gate, exactly like Execute (point_ops_execute.cpp).
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

// ─────────────────────── -bug DRIVER flag (the gate-drop tooth, the Execute family shape) ───────────────────────
// When true, the op IGNORES IsEnabled and always passes the chain → the "disabled ⇒ empty" assertion
// goes RED. OFF in production. A CPU op flag (no shader test seam — constitution rule).
bool& loadSoundtrackIgnoreEnabledForTest() { static bool v = false; return v; }

// ─────────────────────── the LoadSoundtrack op (gated concat-all forward == Execute) ───────────────────────
RenderCommand cookLoadSoundtrack(CmdCookCtx& c) {
  RenderCommand rc;
  const bool enabled = cookParam(c, "IsEnabled", 1.0f) > 0.5f;  // .t3 DefaultValue = true
  if ((enabled || loadSoundtrackIgnoreEnabledForTest()) && c.inputCommand)
    rc.items = c.inputCommand->items;  // wire-ordered + concatenated by the driver (== Execute)
  return rc;
}

void registerLoadSoundtrackOp() { registerCmdOp("LoadSoundtrack", cookLoadSoundtrack); }

// ───────────────────────────────────────── GOLDEN ─────────────────────────────────────────
// --selftest-loadsoundtrack (BOTH legs). Topology: three stub Command ops (tags 11/22/33) →
// LoadSoundtrack.Command (3 wires, wire order) → StubRenderTarget (terminal capture). Closed-form
// (LoadSoundtrack.cs:17-36):
//   IsEnabled=true (.t3 default) → concat-all in wire order → 3 items, tags [11,22,33].
//   IsEnabled=false → the prepare/execute/restore loop is skipped → EMPTY chain (0 items).
// -bug: loadSoundtrackIgnoreEnabledForTest() forces the chain through when disabled → the
//   "disabled ⇒ empty" assertion FAILS → RED on the REAL op cook (both legs; resident = production).
namespace {
RenderCommand g_lsChain;
RenderCommand lsStubA(CmdCookCtx&) { RenderCommand rc; rc.items.push_back(RenderDrawItem{nullptr, 11u, 1.0f}); return rc; }
RenderCommand lsStubB(CmdCookCtx&) { RenderCommand rc; rc.items.push_back(RenderDrawItem{nullptr, 22u, 1.0f}); return rc; }
RenderCommand lsStubC(CmdCookCtx&) { RenderCommand rc; rc.items.push_back(RenderDrawItem{nullptr, 33u, 1.0f}); return rc; }
void lsStubRenderTarget(TexCookCtx& c) { if (c.command) g_lsChain = *c.command; }

NodeSpec lsAtomicSpec(const char* type, std::vector<PortSpec> ports) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr;
  return s;
}

void installLoadSoundtrackSpecs() {
  std::map<std::string, NodeSpec> dyn;
  dyn["LoadSoundtrack"] = lsAtomicSpec("LoadSoundtrack",
      {{"Command", "Command", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
       {"out", "out", "Command", false},
       {"IsEnabled", "IsEnabled", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool, {}, true}});
  dyn["LsStubA"] = lsAtomicSpec("LsStubA", {{"out", "out", "Command", false}});
  dyn["LsStubB"] = lsAtomicSpec("LsStubB", {{"out", "out", "Command", false}});
  dyn["LsStubC"] = lsAtomicSpec("LsStubC", {{"out", "out", "Command", false}});
  dyn["StubRenderTarget"] = lsAtomicSpec("StubRenderTarget",
      {{"command", "command", "Command", true}, {"out", "out", "Texture2D", false}});
  setDynamicSpecs(std::move(dyn));
}

bool cookLoadSoundtrackGraph(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, int whichPath,
                             bool enabled, RenderCommand& outChain) {
  Graph g;
  Node a; a.id = 1; a.type = "LsStubA"; g.nodes.push_back(a);
  Node b; b.id = 2; b.type = "LsStubB"; g.nodes.push_back(b);
  Node cN; cN.id = 3; cN.type = "LsStubC"; g.nodes.push_back(cN);
  Node ls; ls.id = 4; ls.type = "LoadSoundtrack"; ls.params["IsEnabled"] = enabled ? 1.0f : 0.0f;
  g.nodes.push_back(ls);
  Node rt; rt.id = 5; rt.type = "StubRenderTarget"; g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, 0), pinId(4, 0)});  // A → .Command (wire0)
  g.connections.push_back({102, pinId(2, 0), pinId(4, 0)});  // B → .Command (wire1)
  g.connections.push_back({103, pinId(3, 0), pinId(4, 0)});  // C → .Command (wire2)
  g.connections.push_back({104, pinId(4, 1), pinId(5, 0)});  // .out → StubRenderTarget.command

  g_lsChain = RenderCommand{};
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
  outChain = g_lsChain;
  return true;
}
}  // namespace

int runLoadSoundtrackSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-loadsoundtrack] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerLoadSoundtrackOp();  // the REAL op under test
  registerCmdOp("LsStubA", lsStubA);
  registerCmdOp("LsStubB", lsStubB);
  registerCmdOp("LsStubC", lsStubC);
  registerTexOp("StubRenderTarget", lsStubRenderTarget);
  installLoadSoundtrackSpecs();

  loadSoundtrackIgnoreEnabledForTest() = injectBug;  // ★bug = drop the IsEnabled gate

  const char* pathName[2] = {"flat", "resident"};
  bool allFaithful = true;

  for (int path = 0; path < 2; ++path) {
    RenderCommand cOn;
    cookLoadSoundtrackGraph(dev, lib, q, path, /*enabled=*/true, cOn);
    bool onOk = cOn.items.size() == 3 && cOn.items[0].count == 11u && cOn.items[1].count == 22u &&
                cOn.items[2].count == 33u;

    RenderCommand cOff;
    cookLoadSoundtrackGraph(dev, lib, q, path, /*enabled=*/false, cOff);
    bool offOk = cOff.items.empty();

    bool legOk = onOk && offOk;
    allFaithful = allFaithful && legOk;
    std::printf("[selftest-loadsoundtrack] %s enabled items=%zu(want 3 [11,22,33]) disabled items=%zu"
                "(want 0) -> %s\n", pathName[path], cOn.items.size(), cOff.items.size(),
                legOk ? "faithful-ok" : "tripped");
  }

  loadSoundtrackIgnoreEnabledForTest() = false;  // process hygiene
  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-loadsoundtrack] injectBug did NOT trip (the IsEnabled gate is not "
                  "actually gating)\n");
      return 0;  // did-not-trip → 0 so --bite's NO-BITE list surfaces the dead tooth
    }
    std::printf("[selftest-loadsoundtrack] injectBug correctly RED (gate dropped → disabled still "
                "concatenated all 3 items on BOTH legs)\n");
    return 1;
  }
  std::printf("[selftest-loadsoundtrack] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/331, {"loadsoundtrack", runLoadSoundtrackSelfTest});

}  // namespace sw
