// colorlist_golden — --selftest-colorstolist. R-2 TRANSPORT golden for the COLORLIST cook flow (host
// List<Vector4>): build Const scalar sources wired into ColorsToList's 4 PARALLEL Float MultiInput
// component ports (Colors.x/.y/.z/.w), drive it through BOTH the flat cook (PointGraph::cook →
// cookColorListNode) AND the PRODUCTION resident path (libFromGraph → buildEvalGraph →
// cookColorListNodes → ResidentNode::extColorOut), and assert the cooked list equals the input colors
// IN WIRE ORDER, EXACT. NO GPU / texture — the ColorList currency is a CPU std::vector<simd::float4> the
// whole way.
//
// What this proves (the architecture-defining part of the seam):
//   (1) the ColorList host list flows node→node: ColorsToList's output is readable downstream via the
//       driver-owned colorListBuf (flat) / extColorOut (resident) — the transport mechanism;
//   (2) the 4-parallel-MultiInput vec4 gather (fork-colorstolist-vec4-as-4-parallel-multiinputs) ZIPS
//       each component channel per index in WIRE-DECLARATION order — a graph wiring colors [C0,C1,C2]
//       (per-channel wires in that order) reads back [C0,C1,C2], not reordered, not the first wire only;
//   (3) ★R-2: the PRODUCTION resident pass (cookColorListNodes, the leg frame_cook.cpp drives per frame)
//       writes the REAL list onto extColorOut — NOT just the flat rail (which has zero production callers).
//
// injectBug routes through colorListInjectBug() so the RED case corrupts the REAL cook output (drops the
// last color) on BOTH legs — teeth on the actual op path, not the expected value (mirror floatlist_golden).
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <simd/simd.h>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/colorlist_op_registry.h"  // colorListInjectBug
#include "runtime/compound_graph.h"          // SymbolLibrary
#include "runtime/eval_context.h"            // EvaluationContext
#include "runtime/graph.h"                   // Graph/Node/Connection/pinId
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

// Build { ColorsToList(id 1) with one Const per (color,channel), wired into the matching component port }.
// ColorsToList ports: [0]=Colors.x, [1]=Colors.y, [2]=Colors.z, [3]=Colors.w (each a Float MultiInput),
// [4]=out (ColorList). For color i, four Consts feed the four component ports (so the i-th wire on each
// channel = color i's x/y/z/w). Wires on each channel are pushed in `colors` order → wire-declaration
// order → the zip reads back `colors` exactly. Const port 1 = "out" (Float output).
Graph makeColorsToList(const std::vector<simd::float4>& colors) {
  Graph g;
  Node ctl; ctl.id = 1; ctl.type = "ColorsToList"; g.nodes.push_back(ctl);
  const int pinX = pinId(1, /*Colors.x*/ 0);
  const int pinY = pinId(1, /*Colors.y*/ 1);
  const int pinZ = pinId(1, /*Colors.z*/ 2);
  const int pinW = pinId(1, /*Colors.w*/ 3);
  const int chanPin[4] = {pinX, pinY, pinZ, pinW};

  int nextNode = 2;
  int connId = 100;
  for (size_t i = 0; i < colors.size(); ++i) {
    const float comp[4] = {colors[i].x, colors[i].y, colors[i].z, colors[i].w};
    for (int k = 0; k < 4; ++k) {
      Node c; c.id = nextNode++; c.type = "Const"; c.params["value"] = comp[k];
      g.nodes.push_back(c);
      g.connections.push_back({connId++, pinId(c.id, /*out*/ 1), chanPin[k]});
    }
  }
  return g;
}

// FLAT leg: cook ColorsToList as the terminal, read back via debugCookedColorList.
std::vector<simd::float4> cookFlat(PointGraph& pg, const std::vector<simd::float4>& colors) {
  Graph g = makeColorsToList(colors);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/1);
  const std::vector<simd::float4>* out = pg.debugCookedColorList(1);
  return out ? *out : std::vector<simd::float4>{};
}

// ★R-2 RESIDENT leg (production): mirror the SAME flat Graph into a SymbolLibrary (libFromGraph →
// resident paths == flat node ids as strings), build the resident graph, run cookColorListNodes (the
// per-frame production pass frame_cook.cpp drives), then read the host color list back off the resident
// node's extColorOut[out port idx 4] — the EXACT production channel a downstream consumer reads.
std::vector<simd::float4> cookResident(const std::vector<simd::float4>& colors) {
  Graph g = makeColorsToList(colors);
  SymbolLibrary lib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
  ResidentEvalCtx rc;
  rc.localTime = 0.0f; rc.localFxTime = 0.0f; rc.frameIndex = 0; rc.lib = &lib;
  std::map<std::string, std::vector<simd::float4>> clState;  // single-frame: fresh accumulator store
  cookColorListNodes(rg, rc, clState);  // PRODUCTION pass: walks the resident graph, writes extColorOut
  const ResidentNode* n = rg.node("1");  // ColorsToList resident path == flat node id "1"
  if (!n) return {};
  auto it = n->extColorOut.find(/*out port idx*/ 4);
  return it != n->extColorOut.end() ? it->second : std::vector<simd::float4>{};
}

}  // namespace

int runColorsToListSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  bool ok = true;

  // The shared scenario: three colors in wire-declaration order. Distinct channels prove the per-channel
  // zip (no x/y/z/w mix-up) AND the wire order (C0 then C1 then C2, not reordered).
  const std::vector<simd::float4> colors = {
      simd::make_float4(1.0f, 0.0f, 0.0f, 1.0f),  // C0 red
      simd::make_float4(0.0f, 1.0f, 0.0f, 0.5f),  // C1 green, half alpha
      simd::make_float4(0.25f, 0.5f, 0.75f, 1.0f) // C2 a mixed color
  };

  // LEG 1 — FLAT transport + gather order: ColorsToList(C0,C1,C2) → host list [C0,C1,C2]. Proves the
  // producer output is readable via colorListBuf AND the 4-channel zip honors wire order. injectBug drops
  // the last color in the REAL cook → [C0,C1] ≠ [C0,C1,C2] → RED.
  {
    colorListInjectBug() = injectBug;
    std::vector<simd::float4> got = cookFlat(pg, colors);
    colorListInjectBug() = false;
    bool pass = listEq(got, colors);
    ok = ok && pass;
    std::printf("[selftest-colorstolist] FLAT ColorsToList(C0,C1,C2)=");
    printList("", got);
    std::printf(" want=[(1,0,0,1),(0,1,0,.5),(.25,.5,.75,1)] -> %s\n", pass ? "PASS" : "FAIL");
  }

  // LEG 2 — ★R-2 PRODUCTION RESIDENT path: the SAME graph through libFromGraph → buildEvalGraph →
  // cookColorListNodes (the per-frame pass the running app drives) → extColorOut. Proves the colorlist
  // currency is LIVE on the production resident path, not flat-only (the FloatList R-2 trap). injectBug
  // drops the last color in the REAL resident cook → RED.
  {
    colorListInjectBug() = injectBug;
    std::vector<simd::float4> got = cookResident(colors);
    colorListInjectBug() = false;
    bool pass = listEq(got, colors);
    ok = ok && pass;
    std::printf("[selftest-colorstolist] RESIDENT(production) ColorsToList(C0,C1,C2)=");
    printList("", got);
    std::printf(" want=[(1,0,0,1),(0,1,0,.5),(.25,.5,.75,1)] -> %s\n", pass ? "PASS" : "FAIL");
  }

  q->release();
  dev->release();
  pool->release();

  // Harness convention (run_all_selftests.sh --bite): the -bug variant must exit NON-zero. injectBug
  // drops the last color in the REAL cook on both legs -> ok is false -> return 1 (the tooth bites).
  std::printf("[selftest-colorstolist] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
