// runtime/point_ops_spreadintogrid_golden — SpreadIntoGrid per-wire grid-translation golden.
//
// EXPECTED VALUES (independent-of-impl, hand-derived from TiXL SpreadIntoGrid.cs — line-cited):
//   probe: 5 child wires, GridSize=(3,2,1), Spread=(2,4,6), SpreadScale=0.5
//   spread = Spread·SpreadScale = (1,2,3)                                     (cs:18)
//   xIdx=i%3, yIdx=i/3, zIdx=i/6                                              (cs:37-39)
//   fX = xIdx/(3-1)−0.5 → {−0.5, 0, +0.5};  fY = 0.5−yIdx/(2-1) → {+0.5,−0.5}; fZ = 0 (gz=1)  (cs:47-49)
//   tSpreaded_i = spread·(fX,fY,fZ)                                           (cs:51)
//     i0 (−0.5, 1, 0)   i1 (0, 1, 0)   i2 (0.5, 1, 0)   i3 (−0.5, −1, 0)   i4 (0, −1, 0)
//   transform = PURE TRANSLATE (scale=1, rotation=Identity, cs:54-60) ⇒ stamped groupObjectToWorld =
//   identity upper-left + translation row (row-vector S·R·T ⇒ m[12..14] = t).
//   single-wire probe: commands.Count==1 ⇒ spread forced ZERO (cs:20-21) ⇒ t = (0,0,0).
// The probe sits in the DIVERGING MIDDLE: spread≠0, SpreadScale≠1, grid>1, 5 wires ≠ grid capacity
// (exercises the yIdx integer-division row step) — swap the fX/fY sign or drop the (gx−1) divisor and
// every expected translation goes red.
//
// COOK-THROUGH BOTH LEGS (the blood-lesson: resident is production): 5 real Draw nodes (distinct
// VertexCount 10+i so wire↔item alignment is itself asserted) → SpreadIntoGrid → RenderTarget, cooked
// through cookFlatCommand AND cookResidentCommand; the renderStateCaptureForTest hook reads back the
// stamped items. Legs must be byte-identical on the group matrices.
//
// injectBug (real cook seam, polarity per GOLDEN_STANDARD): spreadCollapseIndexForTest() forces every
// child's spreadIndex to 0 INSIDE the real cook — the per-wire boundary mechanism (CmdCookCtx::
// inputCmdWireItemCounts) is corrupted while the cook still runs; items 1..4 keep child 0's
// translation → the per-index expected values diverge → RED. did-not-trip → return 0 (NO-BITE list).
#include "runtime/point_ops_spread.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"                // Graph/Node/pinId
#include "runtime/graph_bridge.h"         // libFromGraph
#include "runtime/point_graph.h"          // PointGraph::cook/cookResident, registerBuiltinPointOps
#include "runtime/render_command.h"       // RenderCommand / renderStateCaptureForTest
#include "runtime/resident_eval_graph.h"  // buildEvalGraph
#include "runtime/tixl_point.h"           // EvaluationContext

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// nDraws Draw sources (VertexCount=10+i tags wire order) → SpreadIntoGrid(100) → RenderTarget(101).
Graph buildSpreadGridGraph(int nDraws) {
  Graph g;
  for (int i = 0; i < nDraws; ++i) {
    Node dr; dr.id = 1 + i; dr.type = "Draw";
    dr.params["VertexCount"] = 10.0f + (float)i;
    g.nodes.push_back(dr);
  }
  Node sp; sp.id = 100; sp.type = "SpreadIntoGrid";
  sp.params["Spread.x"] = 2.0f; sp.params["Spread.y"] = 4.0f; sp.params["Spread.z"] = 6.0f;
  sp.params["SpreadScale"] = 0.5f;
  sp.params["GridSize.x"] = 3.0f; sp.params["GridSize.y"] = 2.0f; sp.params["GridSize.z"] = 1.0f;
  g.nodes.push_back(sp);
  Node rt; rt.id = 101; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = 64.0f; rt.params["CustomH"] = 64.0f;
  g.nodes.push_back(rt);
  for (int i = 0; i < nDraws; ++i)
    g.connections.push_back({200 + i, pinId(1 + i, 0), pinId(100, 0)});  // Draw.out → Spread.Commands
  g.connections.push_back({300, pinId(100, 1), pinId(101, 0)});          // Spread.out → RT.command
  return g;
}

// Cook one leg, capture the stamped chain (the draw-explicit golden harness).
RenderCommand cookLeg(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, const Graph& g,
                      bool resident) {
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  RenderCommand cap;
  renderStateCaptureForTest() = &cap;
  if (resident) {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    PointGraph pg(dev, lib, q, 64, 64);
    pg.cookResident(rg, ctx, nullptr, "101");
  } else {
    PointGraph pg(dev, lib, q, 64, 64);
    pg.cook(g, ctx, nullptr, pg.defaultDrawTarget(g));
  }
  renderStateCaptureForTest() = nullptr;
  return cap;
}

bool near1(float a, float b) { return std::fabs(a - b) < 1e-5f; }

// One captured chain vs the hand-derived cs:47-51 translations. Checks per-item: wire tag
// (explicitVertexCount=10+i), hasGroup, identity upper-left, translation row.
bool checkGrid(const RenderCommand& rc, const char* leg) {
  // cs:47-51 expected tSpreaded per child (see file header derivation).
  static const float kT[5][3] = {
      {-0.5f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 1.0f, 0.0f},
      {-0.5f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}};
  if (rc.items.size() != 5) {
    std::printf("[selftest-spreadintogrid] %s: got %zu items (want 5)\n", leg, rc.items.size());
    return false;
  }
  bool ok = true;
  for (int i = 0; i < 5; ++i) {
    const RenderDrawItem& it = rc.items[(size_t)i];
    const float* m = it.groupObjectToWorld;
    bool rowOk = it.explicitVertexCount == (uint32_t)(10 + i) && it.hasGroup &&
                 near1(m[0], 1.0f) && near1(m[5], 1.0f) && near1(m[10], 1.0f) &&  // pure translate (cs:54-59)
                 near1(m[1], 0.0f) && near1(m[4], 0.0f) &&
                 near1(m[12], kT[i][0]) && near1(m[13], kT[i][1]) && near1(m[14], kT[i][2]);
    if (!rowOk) {
      std::printf("[selftest-spreadintogrid] %s item %d: vc=%u grp=%d t=(%.4f,%.4f,%.4f) want (%.4f,%.4f,%.4f)\n",
                  leg, i, it.explicitVertexCount, it.hasGroup, m[12], m[13], m[14], kT[i][0], kT[i][1], kT[i][2]);
      ok = false;
    }
  }
  return ok;
}

}  // namespace

// --selftest-spreadintogrid: 5 Draw wires → SpreadIntoGrid cook-through BOTH legs; stamped per-wire
// grid translations == the cs:47-51 closed form + legs byte-identical + single-wire spread-zeroed probe.
int runSpreadIntoGridSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-spreadintogrid] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();

  spreadCollapseIndexForTest() = injectBug;  // -bug: every child cooks as spreadIndex 0 (real seam)
  Graph g = buildSpreadGridGraph(5);
  RenderCommand flat = cookLeg(dev, lib, q, g, /*resident=*/false);
  RenderCommand res = cookLeg(dev, lib, q, g, /*resident=*/true);
  Graph g1 = buildSpreadGridGraph(1);  // single wire: spread forced ZERO (cs:20-21)
  RenderCommand one = cookLeg(dev, lib, q, g1, /*resident=*/true);
  spreadCollapseIndexForTest() = false;

  bool ok = checkGrid(flat, "flat") && checkGrid(res, "resident");
  // flat/resident mirror gate: the stamped matrices must be byte-identical across legs.
  if (ok)
    for (size_t i = 0; i < flat.items.size() && ok; ++i)
      for (int j = 0; j < 16; ++j)
        if (flat.items[i].groupObjectToWorld[j] != res.items[i].groupObjectToWorld[j]) {
          std::printf("[selftest-spreadintogrid] leg divergence item %zu m[%d]\n", i, j);
          ok = false;
          break;
        }
  // single-wire probe: 1 item, hasGroup, translation (0,0,0) — the cs:20-21 spread-zero branch.
  bool oneOk = one.items.size() == 1 && one.items[0].hasGroup &&
               near1(one.items[0].groupObjectToWorld[12], 0.0f) &&
               near1(one.items[0].groupObjectToWorld[13], 0.0f) &&
               near1(one.items[0].groupObjectToWorld[14], 0.0f);
  if (!oneOk) std::printf("[selftest-spreadintogrid] single-wire probe failed\n");
  ok = ok && oneOk;

  std::printf("[selftest-spreadintogrid] items flat=%zu res=%zu single=%zu -> %s\n", flat.items.size(),
              res.items.size(), one.items.size(), ok ? "PASS" : "tripped");

  lib->release(); q->release(); dev->release(); pool->release();
  // Polarity (GOLDEN_STANDARD): green leg wants ok → 0. -bug leg: the collapse bit → ok=false → 1;
  // did-not-trip (bug failed to bite, ok stayed true) → 0 so --bite's NO-BITE list catches the dead tooth.
  return ok ? 0 : 1;
}

}  // namespace sw
