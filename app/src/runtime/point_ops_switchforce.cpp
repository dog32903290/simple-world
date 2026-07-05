// runtime/point_ops_switchforce — SwitchParticleForce: the FORCE-rail SUB-SELECT + the shared selection
// helper both force-gather legs call + the --selftest-switchparticleforce HARD-GATE golden (flat AND
// resident legs; the resident -bug is a distinct RED tooth = the S2c/S3a blood lesson — production runs the
// resident leg).
//
// TiXL ground truth: particle/force/SwitchParticleForce.cs:16-27 (the Update):
//   var index = Index.GetValue(context);
//   if (index == -1) return;                              // cs:19-20 — none (Selected stays null)
//   var connections = Input.GetCollectedTypedInputs();    // MultiInput, wire-declaration order
//   if (connections == null || connections.Count == 0) return;   // cs:23-24 — none
//   Selected.Value = connections[index.Mod(connections.Count)].GetValue(context);  // cs:26 — pick ONE
// .Mod (MathUtils.cs:273-282): repeat==0 → 0; x = val % repeat; if (x < 0) x += repeat — negative-safe wrap.
// .t3 DEFAULT: Index = 0 (SwitchParticleForce.t3). Ctor: Index = new(0) (cs:33).
//
// UNLIKE flow/Switch (the Command twin, point_ops_switch.cpp): Switch has a -2 = "cook ALL wires" (concat);
// SwitchParticleForce has NO -2 — a single force is SCALAR (the ParticleSystem runs ONE kernel per frame,
// point_ops.cpp:203), not a concat. So the sentinel set is smaller: -1 → NONE, else index.Mod(count). This is
// why switchParticleForceSelectIndex() is a SEPARATE helper from switchSelectIndex() (which carries the -2/all
// branch): faithful to the two different .cs Update bodies, not a shared-with-a-flag fiction.
//
// ★COOK-CORE HOOK (the seam): sw's ParticleSystem reads a SINGLE wired force (point_ops.cpp:166 —
// inputParams[1] + _ForceKind). A force op is param-only (no cook fn, no buffer — the force currency is the
// upstream node's params map: _ForceKind + force params). So the SELECTION lives in the force-gather, the SAME
// place insParams[1] is resolved (point_graph.cpp / point_graph_resident.cpp): when the wired `forces` source
// IS a SwitchParticleForce, the gather re-resolves to the Index-selected upstream force node and reads ITS
// params. The math is factored into switchParticleForceSelectIndex() so the FLAT and RESIDENT legs call the
// IDENTICAL function — a single source of truth defeats the off-by-one trap (resident wires = primary +
// extraConns; the same §3 trap flow/Switch flagged). resolveSwitchedForceSource{Flat,Resident}() do the wire
// gather + select in the two graph representations and return the effective source node (id / path).
//
// runtime leaf: pure CPU + Metal (the golden cooks through PointGraph); no UI, no upward deps.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph / Node / Connection / pinId / pinNode / findSpec
#include "runtime/graph_bridge.h"         // libFromGraph (flat Graph → SymbolLibrary, paths == ids)
#include "runtime/point_graph.h"          // PointGraph / registerBuiltinPointOps / registerTexOp / TexCookCtx
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / ResidentNode / buildEvalGraph
// The SwitchParticleForce decls (selectIndex / resolvers / -bug flag) come from point_ops.h (included above).
#include "runtime/selftest_registry.h"    // REGISTER_SELFTESTS
#include "runtime/tixl_point.h"           // EvaluationContext / SwPoint

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// ───────────────────────── selection math (shared by both cook legs) ─────────────────────────
// Returns the index of the SINGLE wire to forward, OR the sentinel kSwitchForceSelectNone (-1) when no force
// is selected (TiXL index==-1 OR count==0). Else the wrapped, negative-safe index in [0, count) — cs:26
// index.Mod(count). ONE function so the two legs can NEVER diverge on the wrap/negative/empty edges.
//   `rawIndex` = the (truncated-to-int) SwitchParticleForce.Index param.
//   `count`    = number of wired ParticleForce inputs the gather collected.
int switchParticleForceSelectIndex(int rawIndex, int count) {
  if (count <= 0 || rawIndex == kSwitchForceSelectNone) return kSwitchForceSelectNone;  // none (empty / -1)
  int idx = rawIndex % count;   // MathUtils.Mod: x = val % repeat
  if (idx < 0) idx += count;    // negative-safe (MathUtils.cs:280-281)
  return idx;                   // single selection in [0, count)
}

namespace {
// TiXL's Index is an int slot (SwitchParticleForce.cs:33 InputSlot<int>); sw carries it as a float param, so
// the float→int is TRUNCATION TOWARD ZERO (== a C# (int) cast, NOT round-half — so Index=-1.0 stays -1, the
// NONE sentinel, and never rounds to -2 → wire0). A small epsilon nudge absorbs float storage of exact ints.
int truncIndex(float v) { return (int)(v + (v >= 0.0f ? 1e-4f : -1e-4f)); }
}  // namespace

// Test-only DRIVER flag (the SwitchParticleForce sub-select tooth): true → the force-gather IGNORES the
// selection and forwards the FIRST wired force (index 0), so --selftest-switchparticleforce's -bug leg runs the
// wrong force kernel → the meanY assertion goes RED for every leg whose correct force is NOT wire0. OFF in
// production. A CPU DRIVER flag (NOT a shader bug-branch — constitution rule); read by both force-gather legs,
// parallel to switchIgnoreIndexForTest().
bool& switchForceIgnoreIndexForTest() {
  static bool v = false;
  return v;
}

// ───────────── the resolvers: re-resolve a ParticleForce source THROUGH a SwitchParticleForce ─────────────
// The force-gather (point_graph.cpp / point_graph_resident.cpp) calls these with the node it found wired into a
// ParticleSystem's `forces` input. If that node is NOT a SwitchParticleForce, the resolver returns it unchanged
// (byte-identical: every pre-existing single-force graph is untouched). If it IS, the resolver gathers the N
// wired ParticleForce inputs in wire-declaration order, reads Index, selects (switchParticleForceSelectIndex or
// the -bug's "always wire0"), and returns the selected upstream force node — or the NONE sentinel (flat: -1 /
// resident: "") when no force is selected, so the caller drops the force (inputParams[1] = null → no kernel).

// FLAT: `srcNodeId` = the node wired into ParticleSystem.forces. Returns the effective force node id, or -1 for
// NONE. `nodeParams(id)` = the caller's per-cook param memo (Index reads through the SAME resolution the cook
// uses, so an animated / var-driven Index is honoured).
int resolveSwitchedForceSourceFlat(const Graph& g, int srcNodeId,
                                   const std::function<const std::map<std::string, float>*(int)>& nodeParams) {
  const Node* src = g.node(srcNodeId);
  if (!src || src->type != "SwitchParticleForce") return srcNodeId;  // not a switch → unchanged

  // The Input MultiInput port index on the SwitchParticleForce spec.
  const NodeSpec* spec = findSpec("SwitchParticleForce");
  int inPort = -1;
  if (spec)
    for (size_t i = 0; i < spec->ports.size(); ++i)
      if (spec->ports[i].isInput && spec->ports[i].dataType == "ParticleForce") { inPort = (int)i; break; }
  if (inPort < 0) return kSwitchForceSelectNone;

  // Gather wired sources into the Input port in WIRE-DECLARATION order (= connection order in g.connections,
  // the same order every flat MultiInput gather uses).
  std::vector<int> wired;
  const int inPin = pinId(srcNodeId, inPort);
  for (const Connection& c : g.connections)
    if (c.toPin == inPin) wired.push_back(pinNode(c.fromPin));
  const int count = (int)wired.size();
  if (count == 0) return kSwitchForceSelectNone;  // no forces wired (cs:23-24)

  int rawIndex = 0;  // .t3 default (SwitchParticleForce.t3 Index=0)
  if (const auto* p = nodeParams(srcNodeId)) {
    auto it = p->find("Index");
    if (it != p->end()) rawIndex = truncIndex(it->second);  // TiXL Index is an int slot → truncate toward zero
  }
  if (switchForceIgnoreIndexForTest()) return wired[0];  // -bug: ignore Index → always wire0

  const int sel = switchParticleForceSelectIndex(rawIndex, count);
  if (sel == kSwitchForceSelectNone) return kSwitchForceSelectNone;
  return wired[sel];
}

// RESIDENT: `srcPath` = the resident node wired into ParticleSystem.forces. Returns the effective force node
// path, or "" for NONE. Mirror of the flat resolver over the ResidentInput driver model (primary srcNodePath +
// extraConns = the MultiInput wire order).
std::string resolveSwitchedForceSourceResident(
    const ResidentEvalGraph& rg, const std::string& srcPath,
    const std::function<const std::map<std::string, float>*(const std::string&)>& nodeParams) {
  const ResidentNode* src = rg.node(srcPath);
  if (!src || src->opType != "SwitchParticleForce") return srcPath;  // not a switch → unchanged

  const ResidentInput* in = src->input("Input");
  std::vector<std::string> wired;
  if (in && in->driver == ResidentInput::Driver::Connection) {
    wired.push_back(in->srcNodePath);                       // primary wire
    for (const auto& ec : in->extraConns) wired.push_back(ec.first);  // MultiInput extras, wire order
  }
  const int count = (int)wired.size();
  if (count == 0) return std::string();  // no forces wired (cs:23-24) → NONE

  int rawIndex = 0;  // .t3 default
  if (const auto* p = nodeParams(srcPath)) {
    auto it = p->find("Index");
    if (it != p->end()) rawIndex = truncIndex(it->second);  // TiXL Index is an int slot → truncate toward zero
  }
  if (switchForceIgnoreIndexForTest()) return wired[0];  // -bug: ignore Index → always wire0

  const int sel = switchParticleForceSelectIndex(rawIndex, count);
  if (sel == kSwitchForceSelectNone) return std::string();
  return wired[sel];
}

// ───────────────────────────────────────── GOLDEN ─────────────────────────────────────────
// --selftest-switchparticleforce (FORCE-rail HARD GATE, BOTH legs). Two opposite DirectionalForces feed a
// SwitchParticleForce; the ParticleSystem runs whichever the Index selects. The observable is meanY of the
// live pool: DirectionalForce Direction=(0,-1,0) drags meanY << 0; (0,+1,0) lifts meanY >> 0; no force leaves
// meanY ~ 0 (turbulence/emit is symmetric about the ring). This is the SAME closed-form contract the
// forcekindoob golden uses (Direction sign → meanY sign is DirectionalForce.cs's push). The probe sits at
// non-identity (opposite forces, not a zero force), so a mis-select is OBSERVABLE.
//   Wire0 = DOWN (Direction.y=-1), Wire1 = UP (Direction.y=+1). Per SwitchParticleForce.cs (count==2):
//     Index =  0 → wire0 = DOWN  (meanY < 0)
//     Index =  1 → wire1 = UP    (meanY > 0)
//     Index =  2 → 2%2=0 = DOWN  (the WRAP tooth)
//     Index =  3 → 3%2=1 = UP    (the WRAP tooth)
//     Index = -1 →         NONE  (meanY ~ 0; no force selected, cs:19-20)
//   -bug (switchForceIgnoreIndexForTest): the gather ignores Index → always wire0 (DOWN). So every leg whose
//   correct force is NOT wire0 flips:
//     Index=1 (want UP,   meanY>0) → DOWN → meanY<0 → RED
//     Index=3 (want UP,   meanY>0) → DOWN → meanY<0 → RED
//     Index=-1(want NONE, meanY~0) → DOWN → meanY<0 → RED
//   Index=0/2 both correctly resolve to DOWN, so cook-wire0 matches by luck → SKIPPED under -bug (they can't
//   bite). The bug corrupts the REAL selection seam (the resolver), not the expected value → no want-flip.
namespace {
std::vector<SwPoint>* g_capSF = nullptr;
// Capture RenderTarget executor: read the live particle pool off the Command's DrawItem (the SAME point
// currency both cook legs put on the chain — DrawPoints is a CMD op now, so the FLAT-only registerDrawOp path
// is dead; a RenderTarget tex executor fires on BOTH legs). No GPU render — pure readback (mirror of the
// resident-cook-parity parTex capture). Replaces the real RenderTarget ONLY for the golden.
void captureRT(TexCookCtx& c) {
  if (!g_capSF || !c.command || c.command->items.empty()) return;
  const RenderDrawItem& it = c.command->items[0];
  if (!it.points || it.count == 0) return;
  g_capSF->assign(it.count, SwPoint{});
  std::memcpy(g_capSF->data(), const_cast<MTL::Buffer*>(it.points)->contents(), (size_t)it.count * sizeof(SwPoint));
}

// RadialPoints(1) → ParticleSystem(2); DirDown(4)+DirUp(5) → SwitchParticleForce(6, Index) → PS.forces;
// PS(2) → DrawPoints(3) → RenderTarget(7). Node ids fixed so the pins are stable.
Graph buildSwitchForceGraph(float index) {
  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 1024.0f; gen.params["Radius"] = 2.0f;
  Node sim; sim.id = 2; sim.type = "ParticleSystem";
  Node drw; drw.id = 3; drw.type = "DrawPoints";
  Node down; down.id = 4; down.type = "DirectionalForce";
  down.params["Amount"] = 60.0f;                                       // strong push clears drag
  down.params["Direction.x"] = 0.0f; down.params["Direction.y"] = -1.0f; down.params["Direction.z"] = 0.0f;
  Node up; up.id = 5; up.type = "DirectionalForce";
  up.params["Amount"] = 60.0f;
  up.params["Direction.x"] = 0.0f; up.params["Direction.y"] = 1.0f; up.params["Direction.z"] = 0.0f;
  Node sw; sw.id = 6; sw.type = "SwitchParticleForce"; sw.params["Index"] = index;
  Node rt; rt.id = 7; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = 32.0f; rt.params["CustomH"] = 32.0f;
  g.nodes = {gen, sim, drw, down, up, sw, rt};

  const NodeSpec* psSpec = findSpec("ParticleSystem");
  const NodeSpec* swSpec = findSpec("SwitchParticleForce");
  auto inIdx = [](const NodeSpec* s, const char* dt, bool input) {
    for (size_t i = 0; i < s->ports.size(); ++i)
      if (s->ports[i].isInput == input && s->ports[i].dataType == dt) return (int)i;
    return -1;
  };
  const int psEmit = inIdx(psSpec, "Points", true);
  const int psForce = inIdx(psSpec, "ParticleForce", true);
  const int psOut = inIdx(psSpec, "Points", false);
  const int swIn = inIdx(swSpec, "ParticleForce", true);
  const int swOut = inIdx(swSpec, "ParticleForce", false);
  const int forceOut = 0;  // DirectionalForce's "force" output is port 0

  g.connections.push_back({101, pinId(1, 0), pinId(2, psEmit)});      // RadialPoints → PS.emit
  g.connections.push_back({102, pinId(4, forceOut), pinId(6, swIn)}); // DirDown → Switch.Input wire0
  g.connections.push_back({103, pinId(5, forceOut), pinId(6, swIn)}); // DirUp   → Switch.Input wire1
  g.connections.push_back({104, pinId(6, swOut), pinId(2, psForce)}); // Switch → PS.forces
  g.connections.push_back({105, pinId(2, psOut), pinId(3, 0)});       // PS → DrawPoints
  g.connections.push_back({106, pinId(3, 1), pinId(7, 0)});           // DrawPoints.out → RenderTarget.command
  return g;
}

// Cook the graph over `frames` on whichPath (0=flat, 1=resident) and return meanY of the live pool (captured
// off the RenderTarget's Command DrawItem — identical mechanism on both legs).
float cookMeanY(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, float index, int whichPath) {
  std::vector<SwPoint> captured;
  g_capSF = &captured;
  Graph g = buildSwitchForceGraph(index);
  PointGraph pg(dev, lib, q, 64, 64);
  const int frames = 30;
  if (whichPath == 0) {
    for (int i = 0; i < frames; ++i) {
      EvaluationContext ctx{};
      ctx.frameIndex = (uint32_t)i; ctx.time = 0.05f * (float)i; ctx.deltaTime = 1.0f / 60.0f;
      pg.cook(g, ctx, nullptr, /*RenderTarget id=*/7);
    }
  } else {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    for (int i = 0; i < frames; ++i) {
      EvaluationContext ctx{};
      ctx.frameIndex = (uint32_t)i; ctx.time = 0.05f * (float)i; ctx.deltaTime = 1.0f / 60.0f;
      pg.cookResident(rg, ctx, nullptr, /*RenderTarget path=*/"7");
    }
  }
  double sy = 0; size_t n = 0;
  for (const SwPoint& p : captured)
    if (!std::isnan(p.Position.y)) { sy += p.Position.y; ++n; }
  g_capSF = nullptr;
  return n ? (float)(sy / n) : 0.0f;
}
}  // namespace

int runSwitchParticleForceSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-switchparticleforce] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();
  registerTexOp("RenderTarget", captureRT);  // capture-only terminal (reads the pool off the DrawItem, both legs)

  switchForceIgnoreIndexForTest() = injectBug;  // ★the sub-select -bug (always wire0)

  enum Want { WDOWN, WUP, WNONE };
  struct Leg { float index; Want want; const char* name; bool runUnderBug; };
  const Leg legs[] = {
      {0.0f,  WDOWN, "DOWN(wire0)",  false},  // wire0; -bug also wire0 → matches by luck, skip under bug
      {1.0f,  WUP,   "UP(wire1)",    true},   // wire1; -bug → wire0 DOWN → RED
      {2.0f,  WDOWN, "DOWN(wrap2)",  false},  // 2%2=0 = wire0; -bug also wire0 → skip
      {3.0f,  WUP,   "UP(wrap3)",    true},   // 3%2=1 = wire1; -bug → wire0 DOWN → RED (WRAP tooth)
      {-1.0f, WNONE, "NONE",         true},   // no force; -bug → wire0 DOWN → RED (NONE tooth)
  };

  const char* pathName[2] = {"flat", "resident"};
  bool allFaithful = true;
  for (int path = 0; path < 2; ++path) {
    for (const Leg& L : legs) {
      if (injectBug && !L.runUnderBug) continue;  // skip legs that cook-wire0 matches by luck
      float y = cookMeanY(dev, lib, q, L.index, path);
      bool match = (L.want == WDOWN) ? (y < -0.05f)
                 : (L.want == WUP)   ? (y > 0.05f)
                                     : (std::fabs(y) < 0.05f);  // WNONE
      allFaithful = allFaithful && match;
      std::printf("[selftest-switchparticleforce] %s Index=%g: meanY=%.3f want %s -> %s\n", pathName[path],
                  (double)L.index, y, L.name, match ? "faithful-ok" : "tripped");
    }
  }

  switchForceIgnoreIndexForTest() = false;  // reset the global (process hygiene)
  lib->release(); q->release(); dev->release(); pool->release();

  if (injectBug) {
    if (allFaithful) {
      std::printf("[selftest-switchparticleforce] FAIL: injectBug selected nothing wrong (the gather still "
                  "sub-selected the index → forcing wire0 changed no meanY)\n");
      return 0;  // did-not-trip → NO-BITE latch catches the dead tooth (GOLDEN_STANDARD polarity)
    }
    std::printf("[selftest-switchparticleforce] injectBug correctly RED (gather ignored Index → always wire0 "
                "DOWN → Index 1/3 read DOWN not UP, Index -1 read DOWN not NONE, on BOTH legs)\n");
    return 1;
  }
  std::printf("[selftest-switchparticleforce] %s\n", allFaithful ? "PASS" : "FAIL");
  return allFaithful ? 0 : 1;
}

REGISTER_SELFTESTS(/*orderBase=*/720, {"switchparticleforce", runSwitchParticleForceSelfTest});

}  // namespace sw
