// colorlist_fanout_golden — goldens for the colorlist FAN-OUT leaves on the vec4-list currency:
//   --selftest-colorlist           (ColorList: identity passthrough, flat + R-2 resident)
//   --selftest-combinecolorlists   (CombineColorLists: MultiInput concat, flat + R-2 resident)
//   --selftest-readpointcolors     (ReadPointColors: read a Points bag's .Color -> ColorList, flat)
//
// Each asserts EXACT colorlist contents from known inputs. ColorList/CombineColorLists are HOST ops
// (ColorList input → ColorList output), so they get BOTH a flat leg (PointGraph::cook → cookColorListNode
// → debugCookedColorList) AND the ★R-2 PRODUCTION resident leg (libFromGraph → buildEvalGraph →
// cookColorListNodes → ResidentNode::extColorOut) — proving the currency is live on the per-frame
// production pass, not flat-only. ReadPointColors reads a GPU bag (Points input) → its production readback
// path is the flat PointGraph::cook, which actually dispatches the point pipeline and reads the bag back;
// the resident host-list pass has no resident Points gather (the GPU point bag is not reachable from the
// pure-CPU resident colorlist cook), so ReadPointColors is flat-only here (a hand-built bag direct-cook
// tooth for precise color math + a graph smoke for end-to-end wiring through the cook driver).
//
// injectBug routes through colorListInjectBug() so the RED case corrupts the REAL cook output (drops the
// last color) on every leg — teeth on the actual op path, not the expected value (mirror colorlist_golden).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <simd/simd.h>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/colorlist_op_registry.h"  // ColorListCookCtx / colorListParam / colorListInjectBug / cook fns via findColorListOp
#include "runtime/compound_graph.h"          // SymbolLibrary
#include "runtime/eval_context.h"            // EvaluationContext
#include "runtime/graph.h"                   // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"            // libFromGraph
#include "runtime/point_graph.h"             // PointGraph::cook + debugCookedColorList
#include "runtime/resident_eval_graph.h"     // buildEvalGraph / cookColorListNodes / ResidentEvalGraph
#include "runtime/tixl_point.h"              // SwPoint (Color @ byte 32)

namespace sw {

const ColorListCookFn* findColorListOp(const std::string&);  // dispatch a cook fn directly (ReadPointColors)

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

// ───────────────────── shared graph builders (Const → component zip via ColorsToList) ─────────────────────
// Build a ColorsToList(id) feeding `colors` via the 4 parallel Float MultiInput component ports, plus the
// Const sources. `id` = the ColorsToList node id; Consts start at `nextNode`. Returns the ColorsToList id.
// Mirrors colorlist_golden.cpp makeColorsToList but lets the caller place it in a larger graph.
void addColorsToList(Graph& g, int id, const std::vector<simd::float4>& colors, int& nextNode,
                     int& connId) {
  Node ctl; ctl.id = id; ctl.type = "ColorsToList"; g.nodes.push_back(ctl);
  const int chanPin[4] = {pinId(id, 0), pinId(id, 1), pinId(id, 2), pinId(id, 3)};  // Colors.x/.y/.z/.w
  for (size_t i = 0; i < colors.size(); ++i) {
    const float comp[4] = {colors[i].x, colors[i].y, colors[i].z, colors[i].w};
    for (int k = 0; k < 4; ++k) {
      Node c; c.id = nextNode++; c.type = "Const"; c.params["value"] = comp[k];
      g.nodes.push_back(c);
      g.connections.push_back({connId++, pinId(c.id, /*out*/ 1), chanPin[k]});  // Const.out → channel
    }
  }
}

// Read a flat-cooked ColorList back off node `id` via debugCookedColorList.
std::vector<simd::float4> readFlat(PointGraph& pg) {
  // caller cooks; this just reads the terminal (set by the per-test cook below)
  const std::vector<simd::float4>* out = pg.debugCookedColorList(1);  // terminal is always node id 1
  return out ? *out : std::vector<simd::float4>{};
}

// Read the resident extColorOut of node "1" (the terminal) after cookColorListNodes.
std::vector<simd::float4> readResident(const Graph& g) {
  SymbolLibrary lib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  ResidentEvalCtx rc;
  rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
  std::map<std::string, std::vector<simd::float4>> clState;  // single-frame: fresh accumulator store
  cookColorListNodes(rg, rc, clState);
  const ResidentNode* n = rg.node("1");
  if (!n) return {};
  // terminal's ColorList output port idx: ColorList op is "out" — find its port index dynamically.
  const NodeSpec* s = findSpec(n->opType);
  int outIdx = -1;
  if (s) for (size_t i = 0; i < s->ports.size(); ++i)
    if (!s->ports[i].isInput && s->ports[i].dataType == "ColorList") { outIdx = (int)i; break; }
  if (outIdx < 0) return {};
  auto it = n->extColorOut.find(outIdx);
  return it != n->extColorOut.end() ? it->second : std::vector<simd::float4>{};
}

}  // namespace

// ═══════════════════════════════ ColorList (identity passthrough) ═══════════════════════════════
// Graph: ColorsToList(id 2, colors C0,C1,C2) → ColorList(id 1).List → out. ColorList copies its single
// input list. Assert out == [C0,C1,C2], flat + R-2 resident. injectBug drops the last → RED.
int runColorListSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, nullptr, q, 64, 64);
  bool ok = true;

  const std::vector<simd::float4> colors = {
      simd::make_float4(1.0f, 0.0f, 0.0f, 1.0f),
      simd::make_float4(0.0f, 1.0f, 0.0f, 0.5f),
      simd::make_float4(0.25f, 0.5f, 0.75f, 1.0f)};

  // Build: ColorList(id 1) ← ColorsToList(id 2)
  auto build = [&]() {
    Graph g;
    Node cl; cl.id = 1; cl.type = "ColorList"; g.nodes.push_back(cl);
    int nextNode = 3, connId = 100;
    addColorsToList(g, 2, colors, nextNode, connId);
    g.connections.push_back({connId++, pinId(2, /*ColorsToList out*/ 4), pinId(1, /*List*/ 0)});
    return g;
  };

  { // FLAT
    Graph g = build();
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    colorListInjectBug() = injectBug;
    pg.cook(g, ctx, nullptr, /*terminal*/ 1);
    colorListInjectBug() = false;
    std::vector<simd::float4> got = readFlat(pg);
    bool pass = listEq(got, colors);
    ok = ok && pass;
    std::printf("[selftest-colorlist] FLAT ColorList(copy)="); printList("", got);
    std::printf(" -> %s\n", pass ? "PASS" : "FAIL");
  }
  { // R-2 RESIDENT
    Graph g = build();
    colorListInjectBug() = injectBug;
    std::vector<simd::float4> got = readResident(g);
    colorListInjectBug() = false;
    bool pass = listEq(got, colors);
    ok = ok && pass;
    std::printf("[selftest-colorlist] RESIDENT(production) ColorList(copy)="); printList("", got);
    std::printf(" -> %s\n", pass ? "PASS" : "FAIL");
  }

  q->release(); dev->release(); pool->release();
  std::printf("[selftest-colorlist] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ═══════════════════════════════ CombineColorLists (MultiInput concat) ═══════════════════════════════
// Graph: ColorsToList(id 2 = [A0,A1]) and ColorsToList(id 3 = [B0]) → CombineColorLists(id 1).InputLists
// (wires in that order). Assert out == [A0,A1,B0] (concatenation, wire order). flat + R-2 resident.
int runCombineColorListsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, nullptr, q, 64, 64);
  bool ok = true;

  const std::vector<simd::float4> listA = {
      simd::make_float4(1.0f, 0.0f, 0.0f, 1.0f),
      simd::make_float4(0.0f, 1.0f, 0.0f, 1.0f)};
  const std::vector<simd::float4> listB = {
      simd::make_float4(0.0f, 0.0f, 1.0f, 0.25f)};
  std::vector<simd::float4> want = listA;
  want.insert(want.end(), listB.begin(), listB.end());  // [A0,A1,B0]

  auto build = [&]() {
    Graph g;
    Node comb; comb.id = 1; comb.type = "CombineColorLists"; g.nodes.push_back(comb);
    int nextNode = 4, connId = 100;
    addColorsToList(g, 2, listA, nextNode, connId);
    addColorsToList(g, 3, listB, nextNode, connId);
    // Wire A then B into the SAME MultiInput port (InputLists, port idx 0) — wire-declaration order.
    g.connections.push_back({connId++, pinId(2, /*out*/ 4), pinId(1, /*InputLists*/ 0)});
    g.connections.push_back({connId++, pinId(3, /*out*/ 4), pinId(1, /*InputLists*/ 0)});
    return g;
  };

  { // FLAT
    Graph g = build();
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    colorListInjectBug() = injectBug;
    pg.cook(g, ctx, nullptr, /*terminal*/ 1);
    colorListInjectBug() = false;
    std::vector<simd::float4> got = readFlat(pg);
    bool pass = listEq(got, want);
    ok = ok && pass;
    std::printf("[selftest-combinecolorlists] FLAT Combine([A0,A1],[B0])="); printList("", got);
    std::printf(" want=[A0,A1,B0] -> %s\n", pass ? "PASS" : "FAIL");
  }
  { // R-2 RESIDENT
    Graph g = build();
    colorListInjectBug() = injectBug;
    std::vector<simd::float4> got = readResident(g);
    colorListInjectBug() = false;
    bool pass = listEq(got, want);
    ok = ok && pass;
    std::printf("[selftest-combinecolorlists] RESIDENT(production) Combine="); printList("", got);
    std::printf(" want=[A0,A1,B0] -> %s\n", pass ? "PASS" : "FAIL");
  }

  q->release(); dev->release(); pool->release();
  std::printf("[selftest-combinecolorlists] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ═══════════════════════════════ ReadPointColors (Points bag .Color readback) ═══════════════════════════════
// TOOTH 1 (precise color math, direct-cook): hand-build a shared SwPoint bag with 4 distinct .Color values,
//   call cookReadPointColors via a hand-built ColorListCookCtx (inputPointsBag/Count set, StartIndex/
//   MaxCount params), assert the read-back list. Covers: default (all), StartIndex skip, MaxCount clamp,
//   StartIndex>=count → empty.
// TOOTH 2 (graph smoke, end-to-end wiring): LinePoints(16) → ReadPointColors → terminal; cook through the
//   real driver, read back via debugCookedColorList. Proves the Points-gather is wired into cookColorListNode
//   (count = the line's 16 points; LinePoints colors default to white). Resident: flat-only (no resident
//   Points gather), so no resident leg for ReadPointColors.
namespace {
// Direct-cook one ReadPointColors over a hand-built bag. Returns the read-back colorlist.
std::vector<simd::float4> runReadDirect(MTL::Device* dev, const std::vector<SwPoint>& rows,
                                        int startIndex, int maxCount) {
  const size_t inBytes = rows.size() * sizeof(SwPoint);
  MTL::Buffer* bag = dev->newBuffer(rows.empty() ? sizeof(SwPoint) : inBytes,
                                    MTL::ResourceStorageModeShared);
  if (!rows.empty()) std::memcpy(bag->contents(), rows.data(), inBytes);

  std::map<std::string, float> params;
  params["StartIndex"] = (float)startIndex;
  params["MaxCount"]   = (float)maxCount;
  std::vector<simd::float4> out;

  ColorListCookCtx cc;
  cc.inputPointsBag = bag;
  cc.inputPointsCount = (uint32_t)rows.size();
  cc.output = &out;
  cc.params = &params;
  const ColorListCookFn* fn = findColorListOp("ReadPointColors");
  if (fn && *fn) (*fn)(cc);
  bag->release();
  return out;
}
SwPoint mkPtColor(simd::float4 color) {
  SwPoint p{};
  p.Position = {0.0f, 0.0f, 0.0f};
  p.Rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  p.Color    = color;
  p.Scale    = {1.0f, 1.0f, 1.0f};
  return p;
}
}  // namespace

int runReadPointColorsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(
      NS::String::string("shaders.metallib", NS::UTF8StringEncoding), &err);
  bool ok = true;

  // 4 distinct colors so order + indexing is unambiguous.
  const std::vector<simd::float4> C = {
      simd::make_float4(1.0f, 0.0f, 0.0f, 1.0f),    // P0
      simd::make_float4(0.0f, 1.0f, 0.0f, 0.5f),    // P1
      simd::make_float4(0.0f, 0.0f, 1.0f, 0.25f),   // P2
      simd::make_float4(0.5f, 0.5f, 0.5f, 1.0f)};   // P3
  std::vector<SwPoint> rows;
  for (auto& c : C) rows.push_back(mkPtColor(c));

  colorListInjectBug() = injectBug;

  // CASE A — default StartIndex=0, MaxCount=50 → all 4 colors in order. The EXPECTED value is the FULL
  // list (NOT bug-adjusted): injectBug drops the last color in the REAL cook → got ≠ want → RED (teeth on
  // the actual op path, not the expected value — mirror colorlist_golden / ColorsToList).
  {
    std::vector<simd::float4> got = runReadDirect(dev, rows, 0, 50);
    bool pass = listEq(got, C);
    ok = ok && pass;
    std::printf("[selftest-readpointcolors] A(all) ="); printList("", got);
    std::printf(" -> %s\n", pass ? "PASS" : "FAIL");
  }
  // CASE B — StartIndex=1 → [P1,P2,P3].
  {
    std::vector<simd::float4> got = runReadDirect(dev, rows, 1, 50);
    std::vector<simd::float4> want = {C[1], C[2], C[3]};
    bool pass = listEq(got, want);
    ok = ok && pass;
    std::printf("[selftest-readpointcolors] B(start=1) ="); printList("", got);
    std::printf(" -> %s\n", pass ? "PASS" : "FAIL");
  }
  // CASE C — MaxCount=2 → [P0,P1] (clamp).
  {
    std::vector<simd::float4> got = runReadDirect(dev, rows, 0, 2);
    std::vector<simd::float4> want = {C[0], C[1]};
    bool pass = listEq(got, want);
    ok = ok && pass;
    std::printf("[selftest-readpointcolors] C(max=2) ="); printList("", got);
    std::printf(" -> %s\n", pass ? "PASS" : "FAIL");
  }
  // CASE D — StartIndex >= count → empty (cs:39-43). injectBug cannot drop from empty; stays [].
  {
    std::vector<simd::float4> got = runReadDirect(dev, rows, 4, 50);
    bool pass = got.empty();
    ok = ok && pass;
    std::printf("[selftest-readpointcolors] D(start>=count) size=%zu -> %s\n",
                got.size(), pass ? "PASS" : "FAIL");
  }
  colorListInjectBug() = false;

  // TOOTH 2 — graph smoke: LinePoints(16) → ReadPointColors → terminal, cook through the real driver.
  // Proves the Points-input gather is wired into cookColorListNode (count == the line's points). The
  // bug-mode drop is exercised on the direct teeth above; here we only assert non-bug wiring (so the
  // graph leg does not flip pass on bug-mode — the direct teeth own the RED). LinePoints' default Color
  // is white (1,1,1,1); we assert count == 16 and the first color is finite/white-ish.
  if (lib) {
    Graph g;
    Node gen; gen.id = 2; gen.type = "LinePoints";
    gen.params["Count"] = 16.0f; gen.params["Length"] = 4.0f;
    g.nodes.push_back(gen);
    Node rd; rd.id = 1; rd.type = "ReadPointColors"; g.nodes.push_back(rd);
    g.connections.push_back({101, pinId(2, 0), pinId(1, /*PointBuffer*/ 0)});

    PointGraph pg(dev, lib, q, 256, 256);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, /*terminal*/ 1);
    const std::vector<simd::float4>* got = pg.debugCookedColorList(1);
    size_t n = got ? got->size() : 0;
    bool pass = (n == 16) && got && std::isfinite((*got)[0].x);
    ok = ok && pass;
    std::printf("[selftest-readpointcolors] graph LinePoints(16)->Read count=%zu -> %s\n",
                n, pass ? "PASS" : "FAIL");
  } else {
    std::printf("[selftest-readpointcolors] graph smoke SKIPPED (no metallib)\n");
  }

  q->release(); if (lib) lib->release(); dev->release(); pool->release();
  std::printf("[selftest-readpointcolors] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
