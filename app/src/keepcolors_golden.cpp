// keepcolors_golden — --selftest-keepcolors. MULTI-FRAME golden for KeepColors, the FIRST cross-frame
// state consumer on the COLORLIST rail. The state IS the whole point, so this drives the SAME node across
// 3+ frames carrying the per-node accumulator, on BOTH the flat cook (PointGraph reused across cook()
// calls → Impl::colorListState persists) AND the PRODUCTION resident path (rebuild the resident graph per
// frame with the new Color override, but carry ONE s_colorListState map — the EXACT pattern of the AnimInt
// production golden, frame_cook_animint_selftest.cpp:94-105, mirroring frame_cook.cpp's s_svState).
//
// TiXL authority (KeepColors.cs:18-37): each frame Insert(0,newColor) prepends, RemoveRange caps to
// MaxLength (drops the OLDEST/tail), Reset clears BEFORE the insert. Hand-computed trajectories:
//   ACCUMULATE (MaxLength=10, AddColorToList=true): feed C0 then C1 then C2 over 3 frames →
//       f0 [C0]; f1 [C1,C0]; f2 [C2,C1,C0]   (newest-first per Insert(0,…)).
//   CAP (MaxLength=2): same 3 colors → f0 [C0]; f1 [C1,C0]; f2 [C2,C1]  (C0 dropped off the tail at cap).
//   RESET: accumulate to [C2,C1,C0], then a frame with Reset=true (+ AddColorToList=false) → []  (cleared).
//
// ★R-2: the RESIDENT leg proves the per-node state PERSISTS on the PRODUCTION path (cookColorListNodes +
// the s_colorListState the running app threads), not flat-only. injectBug routes through
// colorListInjectBug() — KeepColors then SKIPS the Insert on the REAL accumulator → the accumulated list
// is wrong (too short) → every accumulate/cap frame FAILS. Teeth on the actual cook path.
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <simd/simd.h>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/colorlist_op_registry.h"  // colorListInjectBug
#include "runtime/compound_graph.h"          // SymbolLibrary
#include "runtime/eval_context.h"            // EvaluationContext
#include "runtime/graph.h"                   // Graph/Node
#include "runtime/graph_bridge.h"            // libFromGraph (flat Graph -> SymbolLibrary, paths == ids)
#include "runtime/point_graph.h"             // PointGraph::cook + debugCookedColorList
#include "runtime/resident_eval_graph.h"     // buildEvalGraph / cookColorListNodes / ResidentEvalGraph

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-5f; }
bool near4(simd::float4 a, simd::float4 b) {
  return nearf(a.x, b.x) && nearf(a.y, b.y) && nearf(a.z, b.z) && nearf(a.w, b.w);
}
bool listEq(const std::vector<simd::float4>& a, const std::vector<simd::float4>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i)
    if (!near4(a[i], b[i])) return false;
  return true;
}
void printList(const char* tag, const std::vector<simd::float4>& v) {
  std::printf("%s [", tag);
  for (size_t i = 0; i < v.size(); ++i)
    std::printf("%s(%.2f,%.2f,%.2f,%.2f)", i ? "," : "", v[i].x, v[i].y, v[i].z, v[i].w);
  std::printf("]");
}

// The 3 distinct colors fed over frames (distinct channels prove no x/y/z/w mix-up + newest-first order).
const simd::float4 C0 = simd::make_float4(1.0f, 0.0f, 0.0f, 1.0f);    // red
const simd::float4 C1 = simd::make_float4(0.0f, 1.0f, 0.0f, 0.5f);    // green, half alpha
const simd::float4 C2 = simd::make_float4(0.25f, 0.5f, 0.75f, 1.0f);  // mixed

// One per-frame configuration of the KeepColors node (the .cs inputs).
struct Frame {
  simd::float4 color;
  bool addColor;
  int maxLength;
  bool reset;
};

// Build a single KeepColors node (id 1) with the given per-frame inputs as stored params (the float rail:
// Color.x..w + AddColorToList/MaxLength/Reset). Same node id every frame → flatKey("#1")/resident path "1"
// stays stable → the accumulator persists across cooks.
Graph makeKeepColors(const Frame& f) {
  Graph g;
  Node n; n.id = 1; n.type = "KeepColors";
  n.params["Color.x"] = f.color.x; n.params["Color.y"] = f.color.y;
  n.params["Color.z"] = f.color.z; n.params["Color.w"] = f.color.w;
  n.params["AddColorToList"] = f.addColor ? 1.0f : 0.0f;
  n.params["MaxLength"] = (float)f.maxLength;
  n.params["Reset"] = f.reset ? 1.0f : 0.0f;
  g.nodes.push_back(n);
  return g;
}

// FLAT leg: one PointGraph reused across all frames (so Impl::colorListState["#1"] persists). Cook each
// frame's graph with KeepColors as the terminal; return the accumulated list after the LAST frame.
std::vector<simd::float4> cookFlatTrajectory(PointGraph& pg, const std::vector<Frame>& frames) {
  std::vector<simd::float4> last;
  for (size_t fi = 0; fi < frames.size(); ++fi) {
    Graph g = makeKeepColors(frames[fi]);
    EvaluationContext ctx{};
    ctx.frameIndex = (uint32_t)fi; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
    const std::vector<simd::float4>* out = pg.debugCookedColorList(1);
    last = out ? *out : std::vector<simd::float4>{};
  }
  return last;
}

// ★R-2 RESIDENT leg (production): rebuild the resident graph each frame with the new Color override, but
// carry ONE s_colorListState map across frames (mirror of AnimInt's StatefulValueState carry). Returns
// the accumulated list off node "1"'s extColorOut after the LAST frame.
std::vector<simd::float4> cookResidentTrajectory(const std::vector<Frame>& frames) {
  std::map<std::string, std::vector<simd::float4>> s_colorListState;  // the cross-frame accumulator store
  std::vector<simd::float4> last;
  for (size_t fi = 0; fi < frames.size(); ++fi) {
    Graph g = makeKeepColors(frames[fi]);
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    ResidentEvalCtx rc;
    rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = (uint32_t)fi; rc.lib = &lib;
    cookColorListNodes(rg, rc, s_colorListState);  // PRODUCTION pass, state carried across frames
    const ResidentNode* n = rg.node("1");
    last.clear();
    if (n) {
      // KeepColors's "out" ColorList port index = 7 (Color.x/.y/.z/.w, AddColorToList, MaxLength, Reset, out).
      auto it = n->extColorOut.find(/*out port idx*/ 7);
      if (it != n->extColorOut.end()) last = it->second;
    }
  }
  return last;
}

// DIAMOND (flat fan-out): ONE KeepColors node (id 1) feeding a CombineColorLists (id 2) MultiInput on
// TWO wires, cooked for ONE frame. The bug being pinned: the flat cookColorListNode used to re-cook each
// upstream colorlist node ONCE PER CONSUMING WIRE (no per-frame memo) — so KeepColors's cross-frame Insert
// ran TWICE this frame → its accumulator grew 2/frame instead of 1. The fix is the per-frame memo
// (point_graph.cpp colorListCooked, mirroring the Points-path `cooked` memo + the resident path's state=
// nullptr-on-recursion split). After 1 frame the accumulator MUST be size 1, NOT 2.
//   Wiring: KeepColors out = pinId(1,7) (Color.x/.y/.z/.w=0..3, AddColorToList=4, MaxLength=5, Reset=6,
//   out=7); CombineColorLists InputLists = pinId(2,0) (MultiInput ColorList). Two Connections (same src→
//   same dst input pin) = the two diamond wires the flat gather expands in wire order.
// Returns the KeepColors (id 1) accumulator SIZE after the single frame (read via debugCookedColorList —
// KeepColors writes its accumulator to its output each cook).
size_t cookFlatDiamondKeepColorsSize() {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  Graph g = makeKeepColors({C0, /*add=*/true, /*maxLength=*/10, /*reset=*/false});  // node id 1
  Node combine; combine.id = 2; combine.type = "CombineColorLists";
  g.nodes.push_back(combine);
  // TWO wires from KeepColors.out (pin 1*100+7+1=708) to CombineColorLists.InputLists (pin 2*100+0+1=201).
  Connection w1; w1.fromPin = pinId(1, 7); w1.toPin = pinId(2, 0); g.connections.push_back(w1);
  Connection w2; w2.fromPin = pinId(1, 7); w2.toPin = pinId(2, 0); g.connections.push_back(w2);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/2);  // CombineColorLists is the terminal (fan-in of 2 wires)

  const std::vector<simd::float4>* acc = pg.debugCookedColorList(1);  // KeepColors's accumulator output
  size_t sz = acc ? acc->size() : 0;

  q->release(); dev->release(); pool->release();
  return sz;
}

// Run ONE trajectory on BOTH legs, assert both equal `want`. Returns true on PASS.
bool runCase(const char* label, const std::vector<Frame>& frames, const std::vector<simd::float4>& want,
             bool injectBug) {
  bool ok = true;
  {
    NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
    MTL::Device* dev = MTL::CreateSystemDefaultDevice();
    MTL::CommandQueue* q = dev->newCommandQueue();
    PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
    colorListInjectBug() = injectBug;
    std::vector<simd::float4> got = cookFlatTrajectory(pg, frames);
    colorListInjectBug() = false;
    bool pass = listEq(got, want);
    ok = ok && pass;
    std::printf("[selftest-keepcolors] %s FLAT=", label); printList("", got);
    std::printf(" want="); printList("", want); std::printf(" -> %s\n", pass ? "PASS" : "FAIL");
    q->release(); dev->release(); pool->release();
  }
  {
    colorListInjectBug() = injectBug;
    std::vector<simd::float4> got = cookResidentTrajectory(frames);
    colorListInjectBug() = false;
    bool pass = listEq(got, want);
    ok = ok && pass;
    std::printf("[selftest-keepcolors] %s RESIDENT(production)=", label); printList("", got);
    std::printf(" want="); printList("", want); std::printf(" -> %s\n", pass ? "PASS" : "FAIL");
  }
  return ok;
}

}  // namespace

int runKeepColorsSelfTest(bool injectBug) {
  bool ok = true;

  // CASE ACCUMULATE: MaxLength=10, AddColorToList=true. Feed C0,C1,C2 over 3 frames → [C2,C1,C0]
  // (newest-first per Insert(0,…)). The list is right ONLY if the accumulator persisted across all 3
  // frames (no state → just [C2], the last frame). injectBug skips Insert → fewer/empty → RED.
  {
    std::vector<Frame> frames = {
        {C0, true, 10, false}, {C1, true, 10, false}, {C2, true, 10, false}};
    std::vector<simd::float4> want = {C2, C1, C0};
    ok = runCase("ACCUM", frames, want, injectBug) && ok;
  }

  // CASE CAP: MaxLength=2. Same 3 colors → f0 [C0]; f1 [C1,C0]; f2 [C2,C1] (C0 falls off the tail at the
  // cap). Proves RemoveRange drops the OLDEST and that the cap rides the persistent list. injectBug → RED.
  {
    std::vector<Frame> frames = {
        {C0, true, 2, false}, {C1, true, 2, false}, {C2, true, 2, false}};
    std::vector<simd::float4> want = {C2, C1};
    ok = runCase("CAP2", frames, want, injectBug) && ok;
  }

  // CASE RESET: accumulate [C2,C1,C0] over 3 frames, then a 4th frame with Reset=true (AddColorToList=
  // false) → _list.Clear() → []. Proves Reset clears the PERSISTENT accumulator (cs:24-25). NOTE: this
  // case has an EMPTY expected list, so injectBug (skips Insert) does NOT change the cleared result — the
  // accumulate+cap cases above carry the teeth (their lists shrink under the bug). Run clean only-meaningful
  // but harmless under bug (stays PASS), so it never masks the failing cases.
  {
    std::vector<Frame> frames = {
        {C0, true, 10, false}, {C1, true, 10, false}, {C2, true, 10, false},
        {simd::make_float4(0, 0, 0, 0), /*add=*/false, 10, /*reset=*/true}};
    std::vector<simd::float4> want = {};  // cleared
    ok = runCase("RESET", frames, want, injectBug) && ok;
  }

  // CASE DIAMOND (flat fan-out double-cook): one KeepColors feeding a CombineColorLists MultiInput on TWO
  // wires, cooked ONE frame. The KeepColors accumulator MUST be size 1 (one Insert/frame), NOT 2 (one
  // Insert PER CONSUMING WIRE = the no-memo bug the per-frame colorListCooked memo fixes). want is FIXED at
  // 1. (This case does NOT route through injectBug — it pins the cook-once structural invariant directly;
  // the bug here is in the GATHER, not in KeepColors's body, so the want stays 1 in both modes.)
  {
    size_t sz = cookFlatDiamondKeepColorsSize();
    bool pass = (sz == 1);
    ok = pass && ok;
    std::printf("[selftest-keepcolors] DIAMOND FLAT KeepColors-accum-size=%zu want=1 -> %s\n",
                sz, pass ? "PASS" : "FAIL");
  }

  std::printf("[selftest-keepcolors] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
