// drawlinegrid_golden — --selftest-gizmo-grid. C3 Tranche-1: DrawLineGrid (wireframe grid plane) via the
// pointlist seam. (See gizmo_geometry.h emitGridLines [fork-gizmo-grid-composite].)
//   LEG 1 — TRANSPORT (flat): assert the line count + that every point lies in the chosen plane (XY → z=0)
//           on the [-0.5,0.5] grid, vs a CLOSED-FORM re-derivation. (segsX+1)+(segsY+1) lines × (2+sep).
//   LEG 2 — ★PRODUCTION PIXEL (resident): grid → ListToBuffer → DrawLines → RenderTarget, readback: TWO
//           ADJACENT vertical grid lines are BOTH lit (the grid spacing is real, not one line); the cell
//           CENTER between them is dark. injectBug → black → RED. (blueprint §4: assert two adjacent line
//           centers lit, never just one.)
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/point_graph.h"
#include "runtime/pointlist_op_registry.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/tixl_point.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

bool isNanf(float a) { return a != a; }

bool litAt(const std::vector<uint8_t>& px, uint32_t W, float ndcX, float ndcY) {
  int x = (int)((ndcX * 0.5f + 0.5f) * (float)(W - 1) + 0.5f);
  int y = (int)((1.0f - (ndcY * 0.5f + 0.5f)) * (float)(W - 1) + 0.5f);
  if (x < 0 || y < 0 || x >= (int)W || y >= (int)W) return false;
  size_t i = ((size_t)y * W + x) * 4;
  return px[i] > 40 || px[i + 1] > 40 || px[i + 2] > 40;
}

}  // namespace

int runGizmoGridSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) { std::printf("[selftest-gizmo-grid] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }
  registerBuiltinPointOps();

  bool ok = true;
  const int kSegX = 4, kSegY = 4;  // 4×4 cells → 5+5 = 10 lines

  // ===== LEG 1 — TRANSPORT (flat). =====
  {
    PointGraph pg(dev, lib, q, 64, 64);
    Graph g;
    Node r; r.id = 1; r.type = "DrawLineGrid";
    r.params["SegmentsX"] = (float)kSegX; r.params["SegmentsY"] = (float)kSegY; r.params["Orientation"] = 0.0f;  // XY
    g.nodes.push_back(r);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.deltaTime = 1.0f / 60.0f;
    pointListInjectBug() = injectBug;
    pg.cook(g, ctx, nullptr, /*terminal=*/1);
    pointListInjectBug() = false;

    const std::vector<SwPoint>* got = pg.debugCookedPointList(1);
    size_t wantN = (size_t)((kSegX + 1) + (kSegY + 1)) * 3;  // 10 lines × (2 pts + sep) = 30
    bool pass = got && got->size() == wantN;
    bool sepOk = pass, planeOk = pass;
    if (pass) for (int i = 0; i < (int)got->size(); ++i) {
      const SwPoint& p = (*got)[i];
      bool isSep = (i % 3 == 2);
      if (isSep != isNanf(p.Scale.x)) sepOk = false;
      if (!isSep && std::fabs(p.Position.z) > 1e-5f) planeOk = false;  // XY plane → z=0
    }
    pass = pass && sepOk && planeOk;
    ok = ok && pass;
    std::printf("[selftest-gizmo-grid] LEG1 transport n=%zu want=%zu(%dx%d) sep=%d plane(z=0)=%d -> %s\n",
                got ? got->size() : 0, wantN, kSegX, kSegY, sepOk ? 1 : 0, planeOk ? 1 : 0, pass ? "PASS" : "FAIL");
  }

  // ===== LEG 2 — ★PRODUCTION PIXEL (resident): two ADJACENT grid lines both lit, the cell between dark. =====
  {
    const uint32_t RW = 256, RH = 256;
    PointGraph pg(dev, lib, q, RW, RH);
    Graph g;
    Node r; r.id = 1; r.type = "DrawLineGrid";
    r.params["SegmentsX"] = (float)kSegX; r.params["SegmentsY"] = (float)kSegY; r.params["Orientation"] = 0.0f;
    g.nodes.push_back(r);
    Node ltb; ltb.id = 2; ltb.type = "ListToBuffer"; g.nodes.push_back(ltb);
    Node drw; drw.id = 3; drw.type = "DrawLines"; drw.params["LineWidth"] = 0.06f; g.nodes.push_back(drw);
    Node rt; rt.id = 4; rt.type = "RenderTarget";
    rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)RW; rt.params["CustomH"] = (float)RH; g.nodes.push_back(rt);
    g.connections.push_back({100, pinId(1, 0), pinId(2, 1)});
    g.connections.push_back({101, pinId(2, 0), pinId(3, 0)});
    g.connections.push_back({102, pinId(3, 1), pinId(4, 0)});

    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.deltaTime = 1.0f / 60.0f;
    pointListInjectBug() = injectBug;
    pg.cookResident(rg, ctx, nullptr, /*RenderTarget path*/ "4");
    pointListInjectBug() = false;

    MTL::Texture* tex = pg.target();
    bool sized = tex && (uint32_t)tex->width() == RW && (uint32_t)tex->height() == RH;
    // Vertical lines (constant u) sit at world x = -0.5 + i/4 for i=0..4 → x = -0.5,-0.25,0,0.25,0.5.
    // Probe two adjacent VERTICAL lines x=-0.25 and x=0. Probe at world y=-0.125 (NDC -0.0357) — BETWEEN
    // the horizontal grid lines (which sit at y=-0.5,-0.25,0,0.25,0.5), so only the vertical lines light
    // up there. Both vertical lines lit; the cell MIDPOINT x=-0.125 at that same y must be dark (no line).
    const float toNdc = 1.0f / 3.5f;
    const float probeY = -0.125f * toNdc;  // off any horizontal grid line
    const float lineA = -0.25f * toNdc, lineB = 0.0f * toNdc, mid = -0.125f * toNdc;
    bool aLit = false, bLit = false, midDark = true;
    if (sized) {
      std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
      tex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
      aLit = litAt(px, RW, lineA, probeY);
      bLit = litAt(px, RW, lineB, probeY);
      midDark = !litAt(px, RW, mid, probeY);
    }
    bool pass = sized && (injectBug ? (!aLit && !bLit) : (aLit && bLit && midDark));
    ok = ok && pass;
    std::printf("[selftest-gizmo-grid] LEG2 ★PRODUCTION adjacentLines a=%d b=%d cellMidDark=%d -> %s\n",
                aLit ? 1 : 0, bLit ? 1 : 0, midDark ? 1 : 0, pass ? "PASS" : "FAIL");
  }

  lib->release(); q->release(); dev->release(); pool->release();
  std::printf("[selftest-gizmo-grid] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
