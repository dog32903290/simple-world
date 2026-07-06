// runtime/point_ops_settime — SetTime subtree-time scope: see point_ops_settime.h for the full mechanism
// doc (Arm A thread_local chain, the S3b LiveCtxVarScope shape) + the two NAMED FORKS
// (fork-settime-flat-fxclock-only / fork-settime-globalabsolute-unscoped-unsupported).
//
// runtime leaf: pure CPU (the scope is host thread-state); no UI, no upward deps.
#include "runtime/point_ops_settime.h"

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
#include "runtime/render_command.h"       // RenderCommand / RenderDrawItem
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (production resident path)
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// ───────────────────────────── the thread_local scope chain ─────────────────────────────
namespace {
// One pushed scope node. `prev` = the enclosing scope (nesting, like TiXL's stacked context mutations).
// TWO kinds share the chain: OFFSET (SetTime — absolute set / relative add) and REMAP (TimeClip — an
// unclamped linear map TimeRange→SourceRange, TimeClipSlot.cs:64-68). scopedTimeOr composes innermost-out.
enum class TimeScopeKind { Offset, Remap };
struct TimeScopeNode {
  TimeScopeKind kind = TimeScopeKind::Offset;
  bool absolute = false;   // Offset: set vs add
  float newTime = 0.0f;    // Offset: the NewTime delta/set
  float inMin = 0.0f, inMax = 0.0f, outMin = 0.0f, outMax = 0.0f;  // Remap: TimeRange→SourceRange
  const TimeScopeNode* prev = nullptr;
};
// The innermost active scope, or nullptr. thread_local for the same correct-by-construction reason as
// S3b's t_liveCtxVars (single-threaded cook today; a future multi-threaded cook can't cross-leak).
thread_local const TimeScopeNode* t_timeScope = nullptr;

float paramOr(const std::map<std::string, float>& m, const char* id, float def) {
  auto it = m.find(id);
  return it != m.end() ? it->second : def;
}

// Compose the chain innermost-out. OFFSET: Absolute CUTS (ignores everything outer), Relative ADDS onto the
// enclosing effective clock (SetTime.cs:28-37). REMAP: applies the unclamped linear map to the enclosing
// effective clock (TimeClipSlot.cs:64-68 — the remap operates on the already-mutated context clock, so it
// composes onto the enclosing scope's output, MathUtils.cs:368-373). Degenerate remap window → outMin.
float applyChain(const TimeScopeNode* s, float ambient) {
  if (!s) return ambient;
  if (s->kind == TimeScopeKind::Remap) {
    const float inner = applyChain(s->prev, ambient);
    const float span = s->inMax - s->inMin;
    if (span == 0.0f) return s->outMin;  // zero-width window is gated out before remap; guard /0 anyway
    const float factor = (inner - s->inMin) / span;
    return factor * (s->outMax - s->outMin) + s->outMin;
  }
  return s->absolute ? s->newTime : applyChain(s->prev, ambient) + s->newTime;
}
}  // namespace

SetTimeScopeSpec resolveSetTimeScope(const std::map<std::string, float>& params) {
  SetTimeScopeSpec spec;
  spec.active = true;
  // OffsetMode (C# enum, (int) trunc): 0=Absolute → set; 1=Relative / 2=GlobalAbsolute → add (the
  // SubTree-branch else, SetTime.cs:33-37 — GlobalAbsolute WITH a subtree behaves like Relative).
  spec.absolute = ((int)paramOr(params, "OffsetMode", 0.0f) == 0);
  spec.newTime = paramOr(params, "NewTime", 0.0f);
  return spec;
}

LiveTimeScope::LiveTimeScope(const SetTimeScopeSpec& spec)
    : prev_(t_timeScope), engaged_(spec.active) {
  if (!engaged_) return;
  // The node must outlive the guard body — store it in a heap-free slot on the guard itself is not
  // possible (header hides the node type), so we own a tiny heap node for the scope's duration.
  auto* n = new TimeScopeNode();
  n->absolute = spec.absolute;
  n->newTime = spec.newTime;
  n->prev = t_timeScope;
  t_timeScope = n;
}
LiveTimeScope::~LiveTimeScope() {
  if (!engaged_) return;
  const TimeScopeNode* n = t_timeScope;
  t_timeScope = static_cast<const TimeScopeNode*>(prev_);
  delete n;
}

LiveTimeRemapScope::LiveTimeRemapScope(const TimeRemapScopeSpec& spec)
    : prev_(t_timeScope), engaged_(spec.active) {
  if (!engaged_) return;
  auto* n = new TimeScopeNode();
  n->kind = TimeScopeKind::Remap;
  n->inMin = spec.inMin; n->inMax = spec.inMax;
  n->outMin = spec.outMin; n->outMax = spec.outMax;
  n->prev = t_timeScope;
  t_timeScope = n;
}
LiveTimeRemapScope::~LiveTimeRemapScope() {
  if (!engaged_) return;
  const TimeScopeNode* n = t_timeScope;
  t_timeScope = static_cast<const TimeScopeNode*>(prev_);
  delete n;
}

bool liveTimeScopeActive() { return t_timeScope != nullptr; }
float scopedTimeOr(float ambient) { return applyChain(t_timeScope, ambient); }
bool isSetTimeScopeWriter(const std::string& opType) { return opType == "SetTime"; }

bool& setTimeBugSkipPush() {
  static bool v = false;  // OFF in production; the golden flips it around a cook then resets
  return v;
}

// SetTime cook: SubTree Command in → Command out. The driver already PUSHED the time scope around the
// subtree cook; this op only forwards the cooked items (like SetRequestedResolution / SetFloatVarCmd).
RenderCommand cookSetTime(CmdCookCtx& c) {
  RenderCommand rc;
  if (c.inputCommand) rc.items = c.inputCommand->items;
  return rc;
}

void registerSetTimeOp() { registerCmdOp("SetTime", cookSetTime); }

// ───────────────────────────────────────── GOLDEN ─────────────────────────────────────────
// --selftest-settime (HARD GATE; flat + resident × Absolute/Relative — 4 assert legs + outside-restore).
//
// Topology (per mode):
//   FxProbe(1, value-rail: evaluate returns ctx.localFxTime) → StampT(4).V   [INSIDE the SubTree]
//   StampT(4) → SetTime(2).SubTree ; FxProbe(1) → StampT(6).V                 [OUTSIDE, restore proof]
//   Execute(5) gathers [SetTime(2).out, StampT(6).out] → StubTimeTarget(3, terminal captures the chain).
// StampT stamps round(V*1000) into its item count; its V is wired to FxProbe.Result, so the value rail
// resolves the fx clock LIVE where the stamp cooks: INSIDE the scope = the SetTime-transformed clock,
// OUTSIDE = the ambient 2.0 (proves the RAII restore, .cs:41-42).
//
// Ambient fx clock = 2.0 bars (flat: ctx.localFxTime; resident: localFxTimeBars=2 → rc.localFxTime).
// Closed-form (SetTime.cs:28-37, NewTime=5):
//   Absolute (OffsetMode 0): inside = 5.0   → stamp 5000 ; outside = 2.0 → 2000
//   Relative (OffsetMode 1): inside = 2+5=7 → stamp 7000 ; outside = 2.0 → 2000
//   ★injectBug: setTimeBugSkipPush() skips the driver push on BOTH legs → inside reads the ambient 2.0
//     → 2000 ≠ 5000/7000 → RED on every leg×mode. did-not-trip → return 0 (--bite NO-BITE catches it).
namespace {
// Value-rail probe: evaluate returns the fx clock the seam hands it. On flat this is the (scoped) 16-byte
// ctx.localFxTime evalFloat passes evaluate(); on resident it is the (scoped) transient-ec localFxTime
// evalResidentFloat builds from rc — i.e. EXACTLY the clock the production anim/oscillate/perlin ops read.
float evalFxProbe(int, const float*, int, const EvaluationContext& ctx) { return ctx.localFxTime; }

RenderCommand stampTimeCmd(CmdCookCtx& c) {
  RenderCommand rc;
  float v = 0.0f;
  if (c.params) { auto it = c.params->find("V"); if (it != c.params->end()) v = it->second; }
  rc.items.push_back(RenderDrawItem{nullptr, (uint32_t)std::lround(v * 1000.0f), 1.0f});
  return rc;
}
RenderCommand g_timeChain;
void stubTimeTarget(TexCookCtx& c) { if (c.command) g_timeChain = *c.command; }

NodeSpec timeAtomicSpec(const char* type, std::vector<PortSpec> ports,
                        float (*ev)(int, const float*, int, const EvaluationContext&) = nullptr) {
  NodeSpec s; s.type = type; s.title = type; s.ports = std::move(ports); s.evaluate = ev;
  return s;
}

// Cook one mode on one leg; capture [insideCount, outsideCount]. Returns false on a structural miss.
bool cookTimeScope(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, int whichPath,
                   float offsetMode, uint32_t& inside, uint32_t& outside) {
  Graph g;
  Node st; st.id = 2; st.type = "SetTime";
  st.params["NewTime"] = 5.0f; st.params["OffsetMode"] = offsetMode;
  g.nodes.push_back(st);
  Node pr; pr.id = 1; pr.type = "FxProbe"; g.nodes.push_back(pr);
  Node si; si.id = 4; si.type = "StampT"; g.nodes.push_back(si);  // inside stamp
  Node so; so.id = 6; so.type = "StampT"; g.nodes.push_back(so);  // outside stamp
  Node ex; ex.id = 5; ex.type = "ExecT"; g.nodes.push_back(ex);
  Node rt; rt.id = 3; rt.type = "StubTimeTarget"; g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, 0), pinId(4, 1)});  // FxProbe.Result → StampT_in.V
  g.connections.push_back({102, pinId(4, 0), pinId(2, 0)});  // StampT_in.out → SetTime.SubTree
  g.connections.push_back({103, pinId(1, 0), pinId(6, 1)});  // FxProbe.Result → StampT_out.V
  g.connections.push_back({104, pinId(2, 1), pinId(5, 0)});  // SetTime.out → ExecT.Commands (wire 0)
  g.connections.push_back({105, pinId(6, 0), pinId(5, 0)});  // StampT_out.out → ExecT.Commands (wire 1)
  g.connections.push_back({106, pinId(5, 1), pinId(3, 0)});  // ExecT.out → StubTimeTarget.command

  g_timeChain = RenderCommand{};
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  ctx.localFxTime = 2.0f;  // the ambient fx clock (bars) — flat reads THIS
  PointGraph pg(dev, lib, q, 64, 64);
  if (whichPath == 0) {
    pg.cook(g, ctx, nullptr, /*terminal=*/3);
  } else {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    // localFxTimeBars=2 → rc.localFxTime=2 (the resident ambient the transient ec is built from).
    pg.cookResident(rg, ctx, nullptr, /*terminal=*/"3", /*localTimeBars=*/2.0f,
                    /*localFxTimeBars=*/2.0f, nullptr, nullptr);
  }
  if (g_timeChain.items.size() < 2) return false;
  inside = g_timeChain.items[0].count;   // wire 0 = the SetTime subtree's stamp
  outside = g_timeChain.items[1].count;  // wire 1 = the post-restore stamp
  return true;
}
}  // namespace

int runSetTimeSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-settime] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  registerSetTimeOp();                        // the REAL op under test
  registerCmdOp("StampT", stampTimeCmd);
  registerTexOp("StubTimeTarget", stubTimeTarget);
  {
    std::map<std::string, NodeSpec> dyn;
    dyn["SetTime"] = timeAtomicSpec("SetTime",
        {{"SubTree", "SubTree", "Command", true},
         {"out", "out", "Command", false},
         {"NewTime", "NewTime", "Float", true, 0.0f, -10000.0f, 10000.0f},
         {"OffsetMode", "OffsetMode", "Float", true, 0.0f, 0.0f, 2.0f}});
    dyn["FxProbe"] = timeAtomicSpec("FxProbe",
        {{"Result", "Result", "Float", false}}, evalFxProbe);  // value-rail: reads the (scoped) fx clock
    dyn["StampT"] = timeAtomicSpec("StampT",
        {{"out", "out", "Command", false},
         {"V", "V", "Float", true, 0.0f, -100000.0f, 100000.0f}});
    // ExecT: a MultiInput Command concat (the Execute shape) so wire 0 cooks the scoped subtree and
    // wire 1 cooks AFTER the scope died — the restore proof rides the wire order.
    dyn["ExecT"] = timeAtomicSpec("ExecT",
        {{"Commands", "Commands", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
         {"out", "out", "Command", false}});
    dyn["StubTimeTarget"] = timeAtomicSpec("StubTimeTarget",
        {{"command", "command", "Command", true}, {"out", "out", "Texture2D", false}});
    setDynamicSpecs(std::move(dyn));
  }
  registerCmdOp("ExecT", [](CmdCookCtx& c) {  // plain concat-forward (the driver already gathered)
    RenderCommand rc;
    if (c.inputCommand) rc.items = c.inputCommand->items;
    return rc;
  });

  setTimeBugSkipPush() = injectBug;  // ★bug = skip the driver push on BOTH legs

  bool allFaithful = true;
  const char* pathName[2] = {"flat", "resident"};
  struct ModeCase { const char* name; float mode; uint32_t wantInside; };
  // SetTime.cs:28-37 closed-form @ ambient 2.0, NewTime 5.0: Absolute → 5.0; Relative → 7.0.
  const ModeCase kModes[2] = {{"Absolute", 0.0f, 5000u}, {"Relative", 1.0f, 7000u}};
  const uint32_t kWantOutside = 2000u;  // the restored ambient (round(2.0*1000))
  for (const ModeCase& m : kModes) {
    for (int path = 0; path < 2; ++path) {
      uint32_t inside = 0, outside = 0;
      bool structOk = cookTimeScope(dev, lib, q, path, m.mode, inside, outside);
      bool faithful = structOk && inside == m.wantInside && outside == kWantOutside;
      allFaithful = allFaithful && faithful;
      std::printf("[selftest-settime] %s/%s: inside=%u(want %u) outside=%u(want %u) struct=%s -> %s\n",
                  m.name, pathName[path], inside, m.wantInside, outside, kWantOutside,
                  structOk ? "ok" : "NO-ITEMS", faithful ? "faithful-ok" : "tripped");
    }
  }

  setTimeBugSkipPush() = false;  // reset the global (process hygiene)
  setDynamicSpecs({});           // drop the injected test specs
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-settime] FAIL: injectBug still passed (the subtree read the scoped clock "
                  "despite the skipped push — the seam is not actually scoping time)\n");
      return 1;
    }
    std::printf("[selftest-settime] injectBug correctly RED (skipped push → the inside stamp read the "
                "ambient 2.0 clock, not the SetTime-transformed one)\n");
    return 1;
  }
  std::printf("[selftest-settime] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/322, {"settime", runSetTimeSelfTest});

}  // namespace sw
