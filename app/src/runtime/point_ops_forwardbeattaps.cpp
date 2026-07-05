// point_ops_forwardbeattaps — ForwardBeatTaps (publish beat/resync/slide into TapProvider, forward SubTree).
//
// TiXL authority: external/tixl/Operators/Lib/numbers/anim/vj/ForwardBeatTaps.cs:17-39.
//   BeatTapTriggered = MathUtils.WasTriggered(TriggerBeatTap.GetValue(context), ref _wasBeatTriggered);  // :22
//   tapProvider.BeatTapTriggered = BeatTapTriggered;                                                       // :23
//   ResyncTriggered  = MathUtils.WasTriggered(TriggerResync.GetValue(context), ref _wasResyncTriggered);  // :26
//   tapProvider.ResyncTriggered = ResyncTriggered;                                                          // :27
//   var offset = SlideSyncTimeOffset.GetValue(context);                                                     // :29
//   if (!float.IsNaN(offset)) { SlideSyncTime = offset; tapProvider.SlideSyncTime = SlideSyncTime; }        // :30-35
//   SubTree.GetValue(context);                                                                              // :38 execute subtree
//
// The provider write is a PRE-SUBTREE side effect the cook DRIVER performs (forwardBeatTapsApply), exactly
// like cmdVarPush / the SetRequestedResolution push — the driver owns the subtree recursion so it owns the
// ordering (triggers BEFORE SubTree.GetValue). The op cook itself only FORWARDS the cooked items.
//
// runtime leaf: pure CPU (the TapProvider is host state); no UI, no upward deps.
#include "runtime/point_ops_forwardbeattaps.h"

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
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/tap_provider.h"         // TapProvider (the process-global mailbox)
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

namespace {
float paramOr(const std::map<std::string, float>& m, const char* id, float def) {
  auto it = m.find(id);
  return it != m.end() ? it->second : def;
}
}  // namespace

bool isForwardBeatTaps(const std::string& opType) { return opType == "ForwardBeatTaps"; }

bool& forwardBeatTapsBugSkipWrite() {
  static bool v = false;  // OFF in production; the golden flips it to sever the pre-subtree write
  return v;
}

void forwardBeatTapsApply(const std::map<std::string, float>& params) {
  if (forwardBeatTapsBugSkipWrite()) return;  // -bug: skip the write → provider never edges (RED)
  TapProvider& tp = TapProvider::instance();
  // Bool inputs dissolved to Float (Cut 32: no Bool port type) → >0.5 = true (cs:22,26).
  const bool beatLevel = paramOr(params, "TriggerBeatTap", 0.0f) > 0.5f;
  const bool resyncLevel = paramOr(params, "TriggerResync", 0.0f) > 0.5f;
  tp.setTriggers(beatLevel, resyncLevel);                            // cs:22-27 edge-detect + publish
  tp.setSlideSyncTime(paramOr(params, "SlideSyncTimeOffset", 0.0f)); // cs:29-35 (NaN-guarded inside)
}

// ForwardBeatTaps cook: Command SubTree in → Command out. The driver already WROTE the provider + cooked
// the subtree, so this op only forwards the cooked items. Unwired SubTree → empty chain.
RenderCommand cookForwardBeatTaps(CmdCookCtx& c) {
  RenderCommand rc;
  if (c.inputCommand) rc.items = c.inputCommand->items;
  return rc;
}

void registerForwardBeatTapsOp() { registerCmdOp("ForwardBeatTaps", cookForwardBeatTaps); }

// ───────────────────────────────── GOLDEN ─────────────────────────────────
// --selftest-forwardbeattaps. The make-or-break is the RISING-EDGE triggered-pull (a held-high trigger
// must NOT re-fire) + the NaN-guarded slide-sync. Drives the PRODUCTION pre-subtree write
// (forwardBeatTapsApply) frame by frame against the process-global TapProvider, inspecting the published
// pulses/value — the SAME frame-by-frame provider battery as setbpm_golden.
//
// The teeth (-bug): setForwardBeatTapsBug severs the driver-side write (forwardBeatTapsApply no-ops) while
// every expected value below stays FIXED at the production-correct answer (GOLDEN_STANDARD 特徵 3: corrupt
// the cook path, never flip the want). Under -bug every CASE that expected a pulse/slide loses it → RED.
namespace {
// One ForwardBeatTaps cook's PRE-SUBTREE write (the production side effect the driver performs).
void applyFrame(float beat, float resync, float slide) {
  forwardBeatTapsApply({{"TriggerBeatTap", beat}, {"TriggerResync", resync}, {"SlideSyncTimeOffset", slide}});
}

// The full case battery with FIXED production-correct wants. Returns the number of failed assertions.
int runCases(const char* tag) {
  const float eps = 1e-4f;
  int fails = 0;
  TapProvider& tp = TapProvider::instance();
  tp.resetForTest();  // clean slate per run (edge memory starts false)

  auto expectBeat = [&](bool want, const char* label) {
    if (tp.beatTapTriggered() != want) {
      std::printf("[selftest-forwardbeattaps] %s %s: beatPulse=%d want %d\n", tag, label,
                  (int)tp.beatTapTriggered(), (int)want);
      ++fails;
    }
  };
  auto expectResync = [&](bool want, const char* label) {
    if (tp.resyncTriggered() != want) {
      std::printf("[selftest-forwardbeattaps] %s %s: resyncPulse=%d want %d\n", tag, label,
                  (int)tp.resyncTriggered(), (int)want);
      ++fails;
    }
  };
  auto expectSlide = [&](float want, const char* label) {
    if (std::fabs(tp.slideSyncTime() - want) > eps) {
      std::printf("[selftest-forwardbeattaps] %s %s: slide=%.4f want %.4f\n", tag, label,
                  (double)tp.slideSyncTime(), (double)want);
      ++fails;
    }
  };

  // CASE 1: first frame, beat LOW → no edge (prev starts false, level false → no change).
  applyFrame(0.0f, 0.0f, 0.0f);
  expectBeat(false, "case1-low"); expectResync(false, "case1-low"); expectSlide(0.0f, "case1-low");

  // CASE 2: beat rises false→true → PULSE. slide 1.5 (not NaN) → SlideSyncTime=1.5.
  applyFrame(1.0f, 0.0f, 1.5f);
  expectBeat(true, "case2-edge"); expectResync(false, "case2-edge"); expectSlide(1.5f, "case2-edge");

  // CASE 3: beat HELD high (still true) → NO re-fire (edge, not level). slide NaN → KEEP 1.5.
  applyFrame(1.0f, 0.0f, std::nanf(""));
  expectBeat(false, "case3-held"); expectSlide(1.5f, "case3-held");

  // CASE 4: beat drops true→false (no pulse), resync rises false→true → resync PULSE. slide 2.25.
  applyFrame(0.0f, 1.0f, 2.25f);
  expectBeat(false, "case4-drop"); expectResync(true, "case4-drop"); expectSlide(2.25f, "case4-drop");

  // CASE 5: beat rises AGAIN false→true → PULSE (re-arms after the drop). resync held → no re-fire.
  applyFrame(1.0f, 1.0f, std::nanf(""));  // slide NaN → keep 2.25
  expectBeat(true, "case5-rearm"); expectResync(false, "case5-rearm"); expectSlide(2.25f, "case5-rearm");

  return fails;
}
}  // namespace

int runForwardBeatTapsSelfTest(bool injectBug) {
  forwardBeatTapsBugSkipWrite() = injectBug;  // ★bug = sever the driver-side write on BOTH legs

  const int fails = runCases(injectBug ? "bug" : "clean");

  // TOOTH: the driver-side write is REAL (both cook legs call forwardBeatTapsApply). Prove the WIRING with a
  // through-graph cook on BOTH legs: a ForwardBeatTaps whose SubTree forwards a probe item, so the op's forward
  // is exercised and (faithfully) the provider is written by the driver before the subtree cooks.
  int wiringFails = 0;
  {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    MTL::CommandQueue* q = dev->newCommandQueue();
    NS::Error* err = nullptr;
    MTL::Library* lib =
        dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
    if (!lib) {
      std::printf("[selftest-forwardbeattaps] FAIL: no metallib\n");
      q->release(); dev->release(); pool->release();
      forwardBeatTapsBugSkipWrite() = false;
      return 1;
    }
    registerForwardBeatTapsOp();
    // A probe Command op stamped into the SubTree so the ForwardBeatTaps forward has an item to chain.
    static uint32_t g_capturedCount;
    g_capturedCount = 0;
    registerCmdOp("FbtProbe", [](CmdCookCtx&) {
      RenderCommand rc; rc.items.push_back(RenderDrawItem{nullptr, 42u, 1.0f}); return rc;
    });
    registerTexOp("FbtStubTarget", [](TexCookCtx& tc) { if (tc.command && !tc.command->items.empty()) g_capturedCount = tc.command->items[0].count; });

    std::map<std::string, NodeSpec> dyn;
    auto atomic = [](const char* type, std::vector<PortSpec> ports) {
      NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = nullptr; return s;
    };
    dyn["ForwardBeatTaps"] = atomic("ForwardBeatTaps",
        {{"SubTree", "SubTree", "Command", true},
         {"out", "out", "Command", false},
         {"TriggerBeatTap", "TriggerBeatTap", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
         {"TriggerResync", "TriggerResync", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
         {"SlideSyncTimeOffset", "SlideSyncTimeOffset", "Float", true, 0.0f, -1000.0f, 1000.0f}});
    dyn["FbtProbe"] = atomic("FbtProbe", {{"out", "out", "Command", false}});
    dyn["FbtStubTarget"] = atomic("FbtStubTarget", {{"command", "command", "Command", true}, {"out", "out", "Texture2D", false}});
    setDynamicSpecs(std::move(dyn));

    const char* pathName[2] = {"flat", "resident"};
    for (int path = 0; path < 2; ++path) {
      TapProvider::instance().resetForTest();
      Graph g;
      Node fbt; fbt.id = 2; fbt.type = "ForwardBeatTaps"; fbt.params["TriggerBeatTap"] = 1.0f;  // rising edge
      g.nodes.push_back(fbt);
      Node rt; rt.id = 3; rt.type = "FbtStubTarget"; g.nodes.push_back(rt);
      Node p; p.id = 1; p.type = "FbtProbe"; g.nodes.push_back(p);
      g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // FbtProbe → ForwardBeatTaps.SubTree
      g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // ForwardBeatTaps.out → FbtStubTarget.command

      g_capturedCount = 0;
      EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
      PointGraph pg(dev, lib, q, 64, 64);
      if (path == 0) {
        pg.cook(g, ctx, nullptr, /*terminal=*/3);
      } else {
        SymbolLibrary slib = libFromGraph(g);
        ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
        pg.cookResident(rg, ctx, nullptr, /*terminal=*/"3", -1.0f, -1.0f, nullptr, nullptr);
      }
      const bool forwarded = (g_capturedCount == 42u);            // the op forwarded the subtree item
      const bool wrote = TapProvider::instance().beatTapTriggered();  // the driver wrote the provider (edge)
      const bool ok = injectBug ? (forwarded && !wrote)          // -bug: forward still works, write severed
                                : (forwarded && wrote);
      if (!ok) ++wiringFails;
      std::printf("[selftest-forwardbeattaps] wiring/%s: forwarded=%d beatWrote=%d -> %s\n",
                  pathName[path], (int)forwarded, (int)wrote, ok ? "ok" : "tripped");
    }
    setDynamicSpecs({});
    lib->release(); q->release(); dev->release(); pool->release();
  }

  forwardBeatTapsBugSkipWrite() = false;  // reset the global (process hygiene)
  TapProvider::instance().resetForTest();

  if (injectBug) {
    // Every leg must FAIL: the case battery loses its pulses/slides AND the wiring write is severed.
    if (fails == 0 && wiringFails == 0) {
      std::printf("[selftest-forwardbeattaps] FAIL: injectBug still passed (the provider was written despite "
                  "the severed driver write — the seam is not actually publishing the taps)\n");
      return 1;
    }
    std::printf("[selftest-forwardbeattaps] injectBug correctly RED (severed pre-subtree write → no pulses "
                "published; battery fails=%d wiring fails=%d)\n", fails, wiringFails);
    return 1;
  }
  const bool pass = (fails == 0 && wiringFails == 0);
  std::printf("[selftest-forwardbeattaps] battery fails=%d wiring fails=%d -> %s\n", fails, wiringFails,
              pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/321, {"forwardbeattaps", runForwardBeatTapsSelfTest});

}  // namespace sw
