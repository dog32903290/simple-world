// runtime/point_ops_spreadlayout_golden — SpreadLayout per-wire line-spread SRT golden.
//
// EXPECTED VALUES (independent-of-impl, hand-derived from TiXL SpreadLayout.cs — line-cited):
//   probe A (3 wires): Spread=(4,2,0), Translation=(10,20,30), Scale=(2,1,1), UniformScale=3,
//   Pivot=0.25, Rotation=0.
//     s = Scale·UniformScale = (6,3,3)                                          (cs:34)
//     f_i = (0.5 − (i/(count−1) − 0.5)) − Pivot → {0.75, 0.25, −0.25}           (cs:59)
//     t_i = Translation − Spread·f_i                                            (cs:60)
//       i0 (10−3, 20−1.5, 30) = (7, 18.5, 30)
//       i1 (10−1, 20−0.5, 30) = (9, 19.5, 30)
//       i2 (10+1, 20+0.5, 30) = (11, 20.5, 30)
//     M_i = S·R·T (GraphicsMath CreateTransformationMatrix, centers=0 ⇒ S·R·T; R=Identity) ⇒
//       m[0]=6, m[5]=3, m[10]=3, m[12..14]=t_i (row-vector convention).
//   probe B (rotation mapping, 1 wire): Rotation=(0,90,0), UniformScale=2, Translation=(1,2,3).
//     count==1 ⇒ spread ZERO (cs:43-44), f=0 (cs:59 ternary) ⇒ t = Translation.
//     yaw=Rotation.Y=90° (cs:36: yaw=r.Y — the axis-mapping under test), R = System.Numerics
//     CreateRotationY(90°) row-major = [0 0 −1; 0 1 0; 1 0 0]; M = diag(2)·R·T ⇒
//       m[0]≈0, m[2]=−2, m[5]=2, m[8]=2, m[10]≈0, m[12..14]=(1,2,3).
//   probe C: IsEnabled=false ⇒ children not executed ⇒ EMPTY chain (cs:41).
// Probe A sits in the diverging middle (Scale≠1, UniformScale≠1, Pivot≠default, Spread≠0): flip the
// f_i sign, drop the pivot subtraction, or forget the UniformScale product and it goes red. Probe B
// bites the yaw=r.Y/pitch=r.X axis mapping (a swap puts −2 in the wrong cell).
//
// COOK-THROUGH BOTH LEGS + injectBug: same harness + spreadCollapseIndexForTest() seam as
// point_ops_spreadintogrid_golden.cpp (see that header); polarity per GOLDEN_STANDARD.
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

enum class LayoutProbe { kSpread3, kRotate1, kDisabled };

// nDraws Draw sources (VertexCount=10+i wire tags) → SpreadLayout(100) → RenderTarget(101).
Graph buildSpreadLayoutGraph(LayoutProbe probe) {
  Graph g;
  const int nDraws = probe == LayoutProbe::kSpread3 ? 3 : 1;
  for (int i = 0; i < nDraws; ++i) {
    Node dr; dr.id = 1 + i; dr.type = "Draw";
    dr.params["VertexCount"] = 10.0f + (float)i;
    g.nodes.push_back(dr);
  }
  Node sp; sp.id = 100; sp.type = "SpreadLayout";
  if (probe == LayoutProbe::kSpread3) {
    sp.params["Spread.x"] = 4.0f; sp.params["Spread.y"] = 2.0f; sp.params["Spread.z"] = 0.0f;
    sp.params["Translation.x"] = 10.0f; sp.params["Translation.y"] = 20.0f; sp.params["Translation.z"] = 30.0f;
    sp.params["Scale.x"] = 2.0f; sp.params["Scale.y"] = 1.0f; sp.params["Scale.z"] = 1.0f;
    sp.params["UniformScale"] = 3.0f;
    sp.params["Pivot"] = 0.25f;
  } else if (probe == LayoutProbe::kRotate1) {
    sp.params["Rotation.y"] = 90.0f;  // yaw = Rotation.Y (cs:36) — the axis-mapping tooth
    sp.params["UniformScale"] = 2.0f;
    sp.params["Translation.x"] = 1.0f; sp.params["Translation.y"] = 2.0f; sp.params["Translation.z"] = 3.0f;
  } else {
    sp.params["IsEnabled"] = 0.0f;  // cs:41 gate
  }
  g.nodes.push_back(sp);
  Node rt; rt.id = 101; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = 64.0f; rt.params["CustomH"] = 64.0f;
  g.nodes.push_back(rt);
  for (int i = 0; i < nDraws; ++i)
    g.connections.push_back({200 + i, pinId(1 + i, 0), pinId(100, 0)});  // Draw.out → Layout.Commands
  g.connections.push_back({300, pinId(100, 1), pinId(101, 0)});          // Layout.out → RT.command
  return g;
}

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

bool near1(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// Probe A: 3 items, per-item S diag (6,3,3) + translation t_i (see header derivation).
bool checkSpread3(const RenderCommand& rc, const char* leg) {
  static const float kT[3][3] = {{7.0f, 18.5f, 30.0f}, {9.0f, 19.5f, 30.0f}, {11.0f, 20.5f, 30.0f}};
  if (rc.items.size() != 3) {
    std::printf("[selftest-spreadlayout] %s: got %zu items (want 3)\n", leg, rc.items.size());
    return false;
  }
  bool ok = true;
  for (int i = 0; i < 3; ++i) {
    const RenderDrawItem& it = rc.items[(size_t)i];
    const float* m = it.groupObjectToWorld;
    bool rowOk = it.explicitVertexCount == (uint32_t)(10 + i) && it.hasGroup &&
                 near1(m[0], 6.0f) && near1(m[5], 3.0f) && near1(m[10], 3.0f) &&  // s = Scale·Uniform (cs:34)
                 near1(m[1], 0.0f) && near1(m[4], 0.0f) &&
                 near1(m[12], kT[i][0]) && near1(m[13], kT[i][1]) && near1(m[14], kT[i][2]);
    if (!rowOk) {
      std::printf("[selftest-spreadlayout] %s item %d: vc=%u grp=%d s=(%.3f,%.3f,%.3f) t=(%.3f,%.3f,%.3f) "
                  "want t=(%.3f,%.3f,%.3f)\n", leg, i, it.explicitVertexCount, it.hasGroup, m[0], m[5],
                  m[10], m[12], m[13], m[14], kT[i][0], kT[i][1], kT[i][2]);
      ok = false;
    }
  }
  return ok;
}

}  // namespace

// --selftest-spreadlayout: 3-wire spread SRT + yaw-mapping + IsEnabled gate, cook-through both legs.
int runSpreadLayoutSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-spreadlayout] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();

  spreadCollapseIndexForTest() = injectBug;  // -bug: every child cooks as spreadIndex 0 (real seam)
  Graph gA = buildSpreadLayoutGraph(LayoutProbe::kSpread3);
  RenderCommand flat = cookLeg(dev, lib, q, gA, /*resident=*/false);
  RenderCommand res = cookLeg(dev, lib, q, gA, /*resident=*/true);
  Graph gB = buildSpreadLayoutGraph(LayoutProbe::kRotate1);
  RenderCommand rot = cookLeg(dev, lib, q, gB, /*resident=*/true);
  Graph gC = buildSpreadLayoutGraph(LayoutProbe::kDisabled);
  RenderCommand off = cookLeg(dev, lib, q, gC, /*resident=*/true);
  spreadCollapseIndexForTest() = false;

  bool ok = checkSpread3(flat, "flat") && checkSpread3(res, "resident");
  // flat/resident mirror gate.
  if (ok)
    for (size_t i = 0; i < flat.items.size() && ok; ++i)
      for (int j = 0; j < 16; ++j)
        if (flat.items[i].groupObjectToWorld[j] != res.items[i].groupObjectToWorld[j]) {
          std::printf("[selftest-spreadlayout] leg divergence item %zu m[%d]\n", i, j);
          ok = false;
          break;
        }

  // Probe B: yaw=90° ⇒ M = diag(2)·RotY(90°)·T (header derivation; System.Numerics CreateRotationY).
  bool rotOk = rot.items.size() == 1 && rot.items[0].hasGroup;
  if (rotOk) {
    const float* m = rot.items[0].groupObjectToWorld;
    rotOk = near1(m[0], 0.0f) && near1(m[2], -2.0f) && near1(m[5], 2.0f) && near1(m[8], 2.0f) &&
            near1(m[10], 0.0f) && near1(m[12], 1.0f) && near1(m[13], 2.0f) && near1(m[14], 3.0f);
  }
  if (!rotOk) std::printf("[selftest-spreadlayout] rotation (yaw=r.Y) probe failed\n");

  // Probe C: IsEnabled=false ⇒ empty chain (cs:41).
  bool offOk = off.items.empty();
  if (!offOk) std::printf("[selftest-spreadlayout] IsEnabled=false probe: %zu items (want 0)\n", off.items.size());

  ok = ok && rotOk && offOk;
  std::printf("[selftest-spreadlayout] items flat=%zu res=%zu rot=%zu off=%zu -> %s\n", flat.items.size(),
              res.items.size(), rot.items.size(), off.items.size(), ok ? "PASS" : "tripped");

  lib->release(); q->release(); dev->release(); pool->release();
  // Polarity (GOLDEN_STANDARD): -bug bit → ok=false → 1; did-not-trip → 0 (NO-BITE catches it).
  return ok ? 0 : 1;
}

}  // namespace sw
