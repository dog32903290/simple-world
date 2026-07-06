// runtime/point_ops_timeclip — TimeClip timeline-window scope: see point_ops_timeclip.h for the full
// mechanism doc (window gate + linear remap, riding the shared SetTime time-scope chain via
// LiveTimeRemapScope) + the two NAMED FORKS (fork-timeclip-flat-fxclock-gate /
// fork-timeclip-normalizedtime-ctxvar).
//
// runtime leaf: pure CPU (the scope is host thread-state); no UI, no upward deps.
#include "runtime/point_ops_timeclip.h"

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
#include "runtime/point_graph.h"          // CmdCookCtx / registerCmdOp / registerTexOp / PointGraph
#include "runtime/point_ops_settime.h"    // scopedTimeOr / LiveTimeRemapScope / TimeRemapScopeSpec
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem
#include "runtime/resident_eval_graph.h"  // buildEvalGraph / ResidentNode::clipOut
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/stateful_value_ops.h"   // ContextVarMap (the _normalizedTime publish target)
#include "runtime/t3_import.h"            // importT3Symbol (the importer-read leg)
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

TimeClipScopeSpec resolveTimeClipScope(const ClipTimeData& clip) {
  TimeClipScopeSpec spec;
  spec.active = true;
  spec.timeStart = clip.timeStart;
  spec.timeEnd = clip.timeEnd;
  spec.sourceStart = clip.sourceStart;
  spec.sourceEnd = clip.sourceEnd;
  return spec;
}

// = TiXL MathUtils.cs:368-373 float Remap (UNCLAMPED). Zero-width input window → outMin (guarded /0).
float timeClipRemap(float v, float inMin, float inMax, float outMin, float outMax) {
  const float span = inMax - inMin;
  if (span == 0.0f) return outMin;
  const float factor = (v - inMin) / span;
  return factor * (outMax - outMin) + outMin;
}

// TiXL TimeClipSlot.cs:54 gate: in-window iff NOT(ambient < start) AND NOT(ambient >= end) → [start, end).
bool timeClipInWindow(const TimeClipScopeSpec& spec, float ambient) {
  return !(ambient < spec.timeStart) && !(ambient >= spec.timeEnd);
}

// TiXL TimeClip.cs:21 normalizedTime = (ambient - start)/(end - start). Degenerate window → 0.
float timeClipNormalized(const TimeClipScopeSpec& spec, float ambient) {
  const float span = spec.timeEnd - spec.timeStart;
  if (span == 0.0f) return 0.0f;
  return (ambient - spec.timeStart) / span;
}

bool isTimeClipOp(const std::string& opType) { return opType == "TimeClip"; }
const char* timeClipNormalizedVarName() { return "_normalizedTime"; }

void timeClipPublishNormalized(const TimeClipScopeSpec& spec, float ambient, ContextVarMap* vars) {
  if (!vars) return;
  vars->floatVars[timeClipNormalizedVarName()] = timeClipNormalized(spec, ambient);
}

TimeClipScopeSpec& flatTimeClipTestScope() {
  static thread_local TimeClipScopeSpec s;  // active=false in production; the flat golden sets it
  return s;
}

bool& timeClipBugSkipScope() {
  static bool v = false;  // OFF in production; the golden flips it around a cook then resets
  return v;
}

// TimeClip cook: SubTree Command in → Command out. The driver already applied the window GATE (empty items
// out-of-window) and pushed the LiveTimeRemapScope; this op only forwards the cooked items (like SetTime).
RenderCommand cookTimeClip(CmdCookCtx& c) {
  RenderCommand rc;
  if (c.inputCommand) rc.items = c.inputCommand->items;
  return rc;
}

void registerTimeClipOp() { registerCmdOp("TimeClip", cookTimeClip); }

// ───────────────────────────────────────── GOLDEN ─────────────────────────────────────────
// --selftest-timeclip (HARD GATE; flat + resident — window gate at 5 playhead positions + the source
// remap mid-value + the restore proof).
//
// The clip: TimeRange [2,6], SourceRange [10,30] (a 4-bar window mapping onto a 20-bar source region → a
// ×5 speed remap, Speed = 20/4 = 5, TimeClip.cs:41). Authored on the TimeClip child's output.
//
// Topology (per ambient time A):
//   FxProbe(1, value-rail: evaluate returns ctx.localFxTime) → StampT(4).V   [INSIDE the SubTree]
//   StampT(4) → TimeClip(2).SubTree ; FxProbe(1) → StampT(6).V                [OUTSIDE, restore proof]
//   ExecT(5) gathers [TimeClip(2).out, StampT(6).out] → StubT(3, terminal captures the chain).
// StampT stamps round(V*1000). Its V is wired to FxProbe.Result, so INSIDE reads the REMAPPED fx clock,
// OUTSIDE reads the ambient (restore proof, TimeClipSlot.cs:80-81).
//
// Closed-form (TimeClipSlot.cs:54-68 @ TimeRange [2,6] → SourceRange [10,30]):
//   A=1  : 1 < 2       → OUT of window → SubTree contributes NOTHING (0 inside items)
//   A=2  : 2 in [2,6)  → remap(2)=10  → stamp 10000 (start edge INCLUSIVE)
//   A=4  : 4 in [2,6)  → remap(4)=Remap(4,2,6,10,30)=(2/4)*20+10=20 → stamp 20000 (mid, the ×5 speed)
//   A=6  : 6 >= 6      → OUT of window (end edge EXCLUSIVE) → NOTHING
//   A=7  : 7 >= 6      → OUT → NOTHING
// OUTSIDE (post-restore) always stamps round(A*1000) — the ambient, un-remapped.
//   ★injectBug: timeClipBugSkipScope() skips the gate+remap on BOTH legs → EVERY A cooks the SubTree
//     un-gated + un-remapped → inside reads the ambient A (not the remap, and fires even out-of-window)
//     → RED on the in-window remap legs (A=2/4: ambient≠remap) and on the out-of-window legs (items
//     appear where none should). did-not-trip → return 0.
namespace {
float evalFxProbe(int, const float*, int, const EvaluationContext& ctx) { return ctx.localFxTime; }

RenderCommand stampTimeCmd(CmdCookCtx& c) {
  RenderCommand rc;
  float v = 0.0f;
  if (c.params) { auto it = c.params->find("V"); if (it != c.params->end()) v = it->second; }
  rc.items.push_back(RenderDrawItem{nullptr, (uint32_t)std::lround(v * 1000.0f), 1.0f});
  return rc;
}
RenderCommand g_chain;
void stubTarget(TexCookCtx& c) { if (c.command) g_chain = *c.command; }

NodeSpec atom(const char* type, std::vector<PortSpec> ports,
              float (*ev)(int, const float*, int, const EvaluationContext&) = nullptr) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = ev;
  return s;
}

// The authored clip window under test (TimeRange [2,6] → SourceRange [10,30]).
const ClipTimeData kClip = {/*timeStart*/ 2.0f, /*timeEnd*/ 6.0f,
                            /*sourceStart*/ 10.0f, /*sourceEnd*/ 30.0f, /*layerIndex*/ 0};

// Cook one ambient time on one leg; capture [insideItemCount, insideStamp, outsideStamp].
// insideItemCount distinguishes gated-out (0 inside items) from in-window (1 inside item).
bool cookOne(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, int whichPath, float ambient,
             uint32_t& insideCount, uint32_t& insideStamp, uint32_t& outsideStamp) {
  Graph g;
  Node tc; tc.id = 2; tc.type = "TimeClip"; g.nodes.push_back(tc);
  Node pr; pr.id = 1; pr.type = "FxProbe"; g.nodes.push_back(pr);
  Node si; si.id = 4; si.type = "StampT"; g.nodes.push_back(si);  // inside stamp
  Node so; so.id = 6; so.type = "StampT"; g.nodes.push_back(so);  // outside stamp
  Node ex; ex.id = 5; ex.type = "ExecT"; g.nodes.push_back(ex);
  Node rt; rt.id = 3; rt.type = "StubT"; g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, 0), pinId(4, 1)});  // FxProbe.Result → StampT_in.V
  g.connections.push_back({102, pinId(4, 0), pinId(2, 0)});  // StampT_in.out → TimeClip.SubTree
  g.connections.push_back({103, pinId(1, 0), pinId(6, 1)});  // FxProbe.Result → StampT_out.V
  g.connections.push_back({104, pinId(2, 1), pinId(5, 0)});  // TimeClip.out → ExecT (wire 0)
  g.connections.push_back({105, pinId(6, 0), pinId(5, 0)});  // StampT_out.out → ExecT (wire 1)
  g.connections.push_back({106, pinId(5, 1), pinId(3, 0)});  // ExecT.out → StubT.command

  g_chain = RenderCommand{};
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  ctx.localFxTime = ambient;  // the ambient fx clock (bars); flat gates+remaps on THIS (the fork)
  PointGraph pg(dev, lib, q, 64, 64);
  if (whichPath == 0) {
    // Flat leg (test-only): author the clip via the flat test seam (the flat Node carries no clip data).
    flatTimeClipTestScope() = resolveTimeClipScope(kClip);
    pg.cook(g, ctx, nullptr, /*terminal=*/3);
    flatTimeClipTestScope() = TimeClipScopeSpec{};  // clear so it never leaks into another cook
  } else {
    SymbolLibrary slib = libFromGraph(g);
    // Author the clip onto the TimeClip child's output slot (the resident projection reads clipOut).
    Symbol& root = slib.symbols.at(slib.rootId);
    for (SymbolChild& ch : root.children)
      if (ch.symbolId == "TimeClip") ch.clips["out"] = kClip;
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    // localTimeBars = the playhead (the faithful gate clock, resident leg); localFxTimeBars = fx clock.
    // Golden drives them EQUAL (== ambient) so both legs gate identically (fork-timeclip-flat-fxclock-gate).
    pg.cookResident(rg, ctx, nullptr, /*terminal=*/"3", /*localTimeBars=*/ambient,
                    /*localFxTimeBars=*/ambient, nullptr, nullptr);
  }
  // Wire order: item 0 = TimeClip subtree (0 items if gated out), then outside stamp. The gate makes the
  // inside contribution vanish, so we detect it by whether the FIRST item equals the outside stamp.
  if (g_chain.items.empty()) return false;
  // Reconstruct inside/outside by count: in-window → 2 items [inside, outside]; gated → 1 item [outside].
  if (g_chain.items.size() >= 2) {
    insideCount = 1;
    insideStamp = g_chain.items[0].count;
    outsideStamp = g_chain.items[1].count;
  } else {
    insideCount = 0;                          // gated out: only the outside stamp survived
    insideStamp = 0;
    outsideStamp = g_chain.items[0].count;
  }
  return true;
}
}  // namespace

namespace {
// IMPORTER-READ leg (pure CPU): a synthetic .t3 root symbol containing one TimeClip child whose Output
// carries authored TimeClip OutputData (= SymbolJson.cs:112-131 shape). Asserts importT3Symbol lands the
// window on child.clips["out"] with the exact TimeRange/SourceRange/LayerIndex. This is the data-model half
// of the seam (the importer read); the cook legs below are the behavior half. injectBug flips the EXPECTED
// values (a structural parse assertion has no cook seam to corrupt — the parse either reads the JSON or it
// doesn't; the -bug proves the assert discriminates, GOLDEN_STANDARD checklist "no seam" honest note).
bool importReadLegOk(bool injectBug) {
  // TimeClip symbol guid + Output slot guid (flow/TimeClip.cs:5 / :7). One child instancing it, authored
  // TimeRange [2,6] SourceRange [10,30] LayerIndex 3 on its Output. crude_json tolerates the trailing
  // comment-strip; we keep it comment-free for directness.
  const std::string t3 = R"T3({
    "Id": "11111111-1111-1111-1111-111111111111",
    "Name": "TimeClipHost",
    "Children": [
      {
        "Id": "22222222-2222-2222-2222-222222222222",
        "SymbolId": "3036067a-a4c2-434b-b0e3-ac95c5c943f4",
        "InputValues": [],
        "Outputs": [
          {
            "Id": "de6ff8b5-40fe-47fa-b9f2-d926b17f9a7f",
            "OutputData": {
              "Type": "T3.Core.Animation.TimeClip",
              "TimeClip": {
                "TimeRange": { "Start": 2.0, "End": 6.0 },
                "SourceRange": { "Start": 10.0, "End": 30.0 },
                "LayerIndex": 3
              }
            }
          }
        ]
      }
    ],
    "Connections": [],
    "Inputs": [],
    "Outputs": []
  })T3";
  SymbolLibrary lib;
  std::string symId;
  if (!importT3Symbol(t3, lib, &symId, nullptr)) return false;
  const Symbol* root = lib.find(symId);
  if (!root || root->children.empty()) return false;
  const SymbolChild& ch = root->children[0];
  auto it = ch.clips.find("out");
  if (it == ch.clips.end()) return false;
  const ClipTimeData& c = it->second;
  // Expected authored window (injectBug flips the expected to prove the assert discriminates the read).
  const float wTimeStart = injectBug ? 0.0f : 2.0f;
  const float wTimeEnd = 6.0f, wSrcStart = 10.0f, wSrcEnd = 30.0f;
  const int wLayer = 3;
  return c.timeStart == wTimeStart && c.timeEnd == wTimeEnd && c.sourceStart == wSrcStart &&
         c.sourceEnd == wSrcEnd && c.layerIndex == wLayer;
}
}  // namespace

int runTimeClipSelfTest(bool injectBug) {
  // Importer-read leg first (pure CPU): faithful → must be true; injectBug → the flipped-expected must FAIL.
  const bool importOk = importReadLegOk(injectBug);
  std::printf("[selftest-timeclip] importer-read: clips[\"out\"] TimeRange[2,6] SourceRange[10,30] Layer 3 -> %s\n",
              importOk ? (injectBug ? "read-matches-real(bug wanted mismatch)" : "ok") : "mismatch");

  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-timeclip] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerTimeClipOp();                       // the REAL op under test
  registerCmdOp("StampT", stampTimeCmd);
  registerTexOp("StubT", stubTarget);
  {
    std::map<std::string, NodeSpec> dyn;
    dyn["TimeClip"] = atom("TimeClip",
        {{"SubTree", "SubTree", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
         {"out", "out", "Command", false}});
    dyn["FxProbe"] = atom("FxProbe", {{"Result", "Result", "Float", false}}, evalFxProbe);
    dyn["StampT"] = atom("StampT",
        {{"out", "out", "Command", false},
         {"V", "V", "Float", true, 0.0f, -100000.0f, 100000.0f}});
    dyn["ExecT"] = atom("ExecT",
        {{"Commands", "Commands", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
         {"out", "out", "Command", false}});
    dyn["StubT"] = atom("StubT",
        {{"command", "command", "Command", true}, {"out", "out", "Texture2D", false}});
    setDynamicSpecs(std::move(dyn));
  }
  registerCmdOp("ExecT", [](CmdCookCtx& c) {
    RenderCommand rc;
    if (c.inputCommand) rc.items = c.inputCommand->items;
    return rc;
  });

  timeClipBugSkipScope() = injectBug;  // ★bug = skip the gate+remap on BOTH legs

  bool allFaithful = true;
  const char* pathName[2] = {"flat", "resident"};
  // Closed-form @ TimeRange[2,6]→SourceRange[10,30] (see the block comment): {ambient, inWindow, insideStamp}.
  struct Case { float ambient; uint32_t inWindow; uint32_t insideStamp; };
  const Case kCases[5] = {
      {1.0f, 0u, 0u},        // out (before)
      {2.0f, 1u, 10000u},    // start edge inclusive → remap(2)=10
      {4.0f, 1u, 20000u},    // mid → remap(4)=20 (the ×5 speed body — kills a wrong remap)
      {6.0f, 0u, 0u},        // end edge exclusive → out
      {7.0f, 0u, 0u},        // out (after)
  };
  for (const Case& tc : kCases) {
    for (int path = 0; path < 2; ++path) {
      uint32_t insideCount = 0, insideStamp = 0, outsideStamp = 0;
      bool structOk = cookOne(dev, lib, q, path, tc.ambient, insideCount, insideStamp, outsideStamp);
      const uint32_t wantOutside = (uint32_t)std::lround(tc.ambient * 1000.0f);  // restored ambient
      bool gateOk = structOk && insideCount == tc.inWindow;
      bool remapOk = tc.inWindow ? (insideStamp == tc.insideStamp) : true;  // no inside stamp when gated
      bool restoreOk = structOk && outsideStamp == wantOutside;
      bool faithful = gateOk && remapOk && restoreOk;
      allFaithful = allFaithful && faithful;
      std::printf("[selftest-timeclip] A=%.0f/%s: inWindow=%u(want %u) inStamp=%u(want %u) "
                  "outStamp=%u(want %u) -> %s\n",
                  tc.ambient, pathName[path], insideCount, tc.inWindow, insideStamp, tc.insideStamp,
                  outsideStamp, wantOutside, faithful ? "faithful-ok" : "tripped");
    }
  }

  timeClipBugSkipScope() = false;  // reset the global (process hygiene)
  setDynamicSpecs({});
  lib->release(); q->release(); dev->release(); pool->release();

  // All-green = the importer read the window AND every cook leg is faithful. injectBug corrupts BOTH the
  // cook scope (timeClipBugSkipScope) AND the importer expected (flipped) → allGreen must go false.
  const bool allGreen = allFaithful && importOk;

  if (injectBug) {
    if (allGreen) {
      std::printf("[selftest-timeclip] FAIL: injectBug still passed (the SubTree cooked gated/remapped "
                  "despite the skipped scope — the seam is not actually windowing/remapping time)\n");
      return 1;
    }
    std::printf("[selftest-timeclip] injectBug correctly RED (skipped scope → the SubTree ignored the "
                "window gate and read the un-remapped ambient clock)\n");
    return 1;
  }
  std::printf("[selftest-timeclip] %s\n", allGreen ? "PASS" : "FAIL");
  return allGreen ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/323, {"timeclip", runTimeClipSelfTest});

}  // namespace sw
