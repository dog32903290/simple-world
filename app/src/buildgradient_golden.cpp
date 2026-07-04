// buildgradient_golden — --selftest-buildgradient (+ -bug). PARITY golden for BuildGradient (the
// list-currency-fed Gradient PRODUCER, BuildGradient.cs). Proves the LIST-CURRENCY BRIDGE: a ColorList
// producer (ColorsToList) + a FloatList producer (FloatsToList) feed BuildGradient's Colors/Positions
// inputs, and the cooked SwGradient's steps == the zipped (position, color) pairs, SORTED by position,
// on BOTH the flat cook AND the PRODUCTION resident cook (cook_ctx.h both-legs byte-identical rule).
//
// WHY THIS SHAPE (chain-through, not a hand-built ctx): the bridge is the SEAM under test — a golden that
// hand-fed GradientCookCtx::inputColorList/inputFloatList would prove the leaf's zip math but NOT the
// gather (cookFlatGradient's new ColorList/FloatList branches / cookResidentColorList+FloatList). So this
// builds a REAL graph (ColorsToList + FloatsToList → BuildGradient) and cooks it through the driver, so the
// tooth rides the actual gather seam. The readback is the BuildGradient node's own SwGradient (gradientBuf),
// read via debugCookedGradient (flat id) / residentGradientFor (resident path) — the EXACT step list, so a
// gather that drops/reorders/mismatches a color↔position is caught (not a sampled-texture proxy).
//
// RESIDENT DRIVER: cookResident has no standalone Gradient terminal (a Gradient-output node falls to the
// cookNode else → clearTarget). So — exactly like gradient_golden's resident leg — a downstream
// GradientsToTexture terminal gathers BuildGradient's Gradient output; cooking that terminal invokes
// cookResidentGradient on BuildGradient, populating gradientBuf[buildGradientPath], which residentGradientFor
// reads back. The texture output is ignored; we assert the BuildGradient STEPS directly.
//
// PROBE (non-trivial, per GOLDEN discipline): 3 DISTINCT colors + 3 UNSORTED positions so the zip order,
// the per-index color↔position pairing, AND SortHandles are all load-bearing:
//   colors    = [ (1,0,0,1)=red, (0,1,0,.5)=green½α, (.25,.5,.75,1)=mixed ]  (wire order C0,C1,C2)
//   positions = [ 0.8, 0.2, 0.5 ]  (UNSORTED → after SortHandles the step order is pos 0.2/0.5/0.8 =
//                                    color C1(green)/C2(mixed)/C0(red) — a REORDER a naive zip misses).
// Expected steps (TiXL BuildGradient.cs:51-65 zip-then-SortHandles), CITED from the .cs, not an sw snapshot:
//   step[0] = { pos 0.2, C1 (0,1,0,.5) }
//   step[1] = { pos 0.5, C2 (.25,.5,.75,1) }
//   step[2] = { pos 0.8, C0 (1,0,0,1) }
// Interpolation param = 2 (Smooth) → asserts the enum param rides through (BuildGradient.cs:66).
//
// injectBug routes gradientInjectBug(): BuildGradient's cook drops the LAST step on the REAL cook path (both
// legs) → the step count assert (want 3, got 2) FAILs. Teeth on the actual op path, not a flipped expected.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <simd/simd.h>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"            // EvaluationContext
#include "runtime/gradient_op_registry.h"    // gradientInjectBug
#include "runtime/graph.h"                    // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"             // libFromGraph (flat Graph -> SymbolLibrary, paths == ids)
#include "runtime/point_graph.h"              // PointGraph + debugCookedGradient / residentGradientFor
#include "runtime/resident_eval_graph.h"      // buildEvalGraph (production resident path)
#include "runtime/selftest_registry.h"        // REGISTER_SELFTESTS
#include "runtime/sw_gradient.h"              // SwGradient / SwGradientStep

namespace sw {
namespace {

bool nearf(float a, float b) { return std::fabs(a - b) < 1e-5f; }
bool near4(simd::float4 a, simd::float4 b) {
  return nearf(a.x, b.x) && nearf(a.y, b.y) && nearf(a.z, b.z) && nearf(a.w, b.w);
}

// The shared probe (see header). C0/C1/C2 colors + UNSORTED positions so SortHandles reorders.
const simd::float4 kC0 = simd::make_float4(1.0f, 0.0f, 0.0f, 1.0f);    // red
const simd::float4 kC1 = simd::make_float4(0.0f, 1.0f, 0.0f, 0.5f);    // green, half alpha
const simd::float4 kC2 = simd::make_float4(0.25f, 0.5f, 0.75f, 1.0f);  // mixed

// Build { ColorsToList(id 1) <- 3 colors } + { FloatsToList(id 20) <- 3 positions } + { BuildGradient(id 40)
// <- Colors, Positions, Interpolation=2 } + { GradientsToTexture(id 50) <- BuildGradient } (the resident tex
// terminal). Returns via `g`; BuildGradient node id = 40, tex terminal id = 50.
void buildGraph(Graph& g) {
  // ColorsToList (id 1): ports [0..3]=Colors.x/.y/.z/.w (Float MultiInput), [4]=out (ColorList).
  Node ctl; ctl.id = 1; ctl.type = "ColorsToList"; g.nodes.push_back(ctl);
  const int chanPin[4] = {pinId(1, 0), pinId(1, 1), pinId(1, 2), pinId(1, 3)};
  const simd::float4 colors[3] = {kC0, kC1, kC2};
  int nextNode = 2, connId = 100;
  for (int i = 0; i < 3; ++i) {
    const float comp[4] = {colors[i].x, colors[i].y, colors[i].z, colors[i].w};
    for (int k = 0; k < 4; ++k) {
      Node c; c.id = nextNode++; c.type = "Const"; c.params["value"] = comp[k];
      g.nodes.push_back(c);
      g.connections.push_back({connId++, pinId(c.id, /*out*/ 1), chanPin[k]});
    }
  }

  // FloatsToList (id 20): ports [0]=Input (Float MultiInput), [1]=out (FloatList).
  Node ftl; ftl.id = 20; ftl.type = "FloatsToList"; g.nodes.push_back(ftl);
  const float positions[3] = {0.8f, 0.2f, 0.5f};  // UNSORTED (SortHandles must reorder)
  const int posPin = pinId(20, /*Input*/ 0);
  for (int i = 0; i < 3; ++i) {
    Node c; c.id = nextNode++; c.type = "Const"; c.params["value"] = positions[i];
    g.nodes.push_back(c);
    g.connections.push_back({connId++, pinId(c.id, /*out*/ 1), posPin});  // wire order = positions order
  }

  // BuildGradient (id 40): ports [0]=OutGradient, [1]=Colors (ColorList), [2]=Positions (FloatList),
  // [3]=Interpolation (Float enum). Set Interpolation=2 (Smooth) via node param.
  Node bg; bg.id = 40; bg.type = "BuildGradient"; bg.params["Interpolation"] = 2.0f; g.nodes.push_back(bg);
  g.connections.push_back({connId++, pinId(1, /*ColorsToList out*/ 4), pinId(40, /*Colors*/ 1)});
  g.connections.push_back({connId++, pinId(20, /*FloatsToList out*/ 1), pinId(40, /*Positions*/ 2)});

  // GradientsToTexture (id 50): ports [0]=Gradients (Gradient MultiInput), [1]=out (Texture2D). The resident
  // tex terminal that pulls BuildGradient's Gradient output (so cookResidentGradient runs on node 40).
  Node g2t; g2t.id = 50; g2t.type = "GradientsToTexture"; g.nodes.push_back(g2t);
  g.connections.push_back({connId++, pinId(40, /*OutGradient*/ 0), pinId(50, /*Gradients*/ 0)});
}

// Assert a cooked SwGradient == the expected sorted zip (see header). `wantFull` = expect 3 steps (false in
// -bug once the last step is dropped — but we always want 3 so the count assert bites). Returns pass.
bool checkGradient(const SwGradient* g, const char* label) {
  if (!g) { std::printf("[selftest-buildgradient] %s NULL gradient -> FAIL\n", label); return false; }
  bool ok = (g->steps.size() == 3);  // HARD count tooth (-bug drops to 2 → FAIL)
  // Expected sorted-by-position order: (0.2,C1), (0.5,C2), (0.8,C0)  (BuildGradient.cs:51-65).
  const float wantPos[3] = {0.2f, 0.5f, 0.8f};
  const simd::float4 wantCol[3] = {kC1, kC2, kC0};
  for (size_t i = 0; i < g->steps.size() && i < 3; ++i)
    ok = ok && nearf(g->steps[i].pos, wantPos[i]) && near4(g->steps[i].color, wantCol[i]);
  ok = ok && (g->interpolation == 2);  // Smooth enum rode through (BuildGradient.cs:66)
  std::printf("[selftest-buildgradient] %s steps=%zu interp=%d", label, g->steps.size(), g->interpolation);
  for (const SwGradientStep& s : g->steps)
    std::printf(" (%.2f:%.2f,%.2f,%.2f,%.2f)", s.pos, s.color.x, s.color.y, s.color.z, s.color.w);
  std::printf(" -> %s\n", ok ? "PASS" : "FAIL");
  return ok;
}

}  // namespace

int runBuildGradientSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();

  bool ok = true;

  // LEG 1 — FLAT: cook the GradientsToTexture terminal (id 50); its Gradient gather cooks BuildGradient (40)
  // via cookFlatGradient → the new ColorList/FloatList branches. Read BuildGradient's SwGradient back.
  {
    PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
    Graph g; buildGraph(g);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    gradientInjectBug() = injectBug;
    pg.cook(g, ctx, nullptr, /*targetNodeId=*/50);
    gradientInjectBug() = false;
    ok = checkGradient(pg.debugCookedGradient(40), injectBug ? "FLAT(bug)" : "FLAT") && ok;
  }

  // LEG 2 — ★PRODUCTION RESIDENT: the SAME graph through libFromGraph → buildEvalGraph → cookResident with
  // the GradientsToTexture terminal (path "50"); its resident Gradient gather cooks BuildGradient (path "40")
  // via cookResidentGradient → cookResidentColorList + cookResidentFloatList. Read gradientBuf["40"] back.
  // Proves the bridge is LIVE on the production leg, byte-identical to flat.
  {
    PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);
    Graph g; buildGraph(g);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    gradientInjectBug() = injectBug;
    pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"50");
    gradientInjectBug() = false;
    ok = checkGradient(pg.residentGradientFor("40"), injectBug ? "RESIDENT(bug)" : "RESIDENT") && ok;
  }

  q->release(); dev->release(); pool->release();
  std::printf("[selftest-buildgradient] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// Self-register (orderBase 640, its own high block so it appends deterministically after the 72/300 blocks).
REGISTER_SELFTESTS(/*orderBase=*/640, {"buildgradient", runBuildGradientSelfTest});

}  // namespace sw
