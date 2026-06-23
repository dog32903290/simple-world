// gridplane_golden — --selftest-gizmo-gridplane. C3 Tranche-1: GridPlane (wireframe grid plane) via the
// pointlist seam. ★See gizmo_geometry.h / pointlist_ops_gridplane.cpp [fork-gizmo-gridplane-shader]:
// TiXL GridPlane is a procedural-fragment-shader quad; SW v1 renders it as a DrawLines wireframe grid
// (emitGridLines). This golden gates the wireframe port, not TiXL's fragment grid.
//   LEG 1 — TRANSPORT (flat): default Orientation XZ (the .t3 90°-X-rotation flat ground plane) → every
//           point in the y=0 plane; assert the line count + plane.
//   LEG 2 — ★PRODUCTION PIXEL (resident): with Orientation=XY (so the camera-less orthographic DrawLines
//           projection — the same no-camera fork as DrawLines — actually SHOWS the grid; the XZ ground
//           plane collapses to a line under XY projection, needing a 3D camera out of C3 scope): two
//           ADJACENT grid lines lit, the cell between dark. injectBug → black → RED.
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

int runGizmoGridPlaneSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) { std::printf("[selftest-gizmo-gridplane] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }
  registerBuiltinPointOps();

  bool ok = true;
  const int kSeg = 4;  // 4×4 → 5+5 = 10 lines

  // ===== LEG 1 — TRANSPORT (flat), default Orientation XZ → y=0 plane. =====
  {
    PointGraph pg(dev, lib, q, 64, 64);
    Graph g;
    Node r; r.id = 1; r.type = "GridPlane"; r.params["Segments"] = (float)kSeg; r.params["Orientation"] = 1.0f;  // XZ
    g.nodes.push_back(r);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.deltaTime = 1.0f / 60.0f;
    pointListInjectBug() = injectBug;
    pg.cook(g, ctx, nullptr, /*terminal=*/1);
    pointListInjectBug() = false;

    const std::vector<SwPoint>* got = pg.debugCookedPointList(1);
    size_t wantN = (size_t)((kSeg + 1) + (kSeg + 1)) * 3;  // 30
    bool pass = got && got->size() == wantN;
    bool sepOk = pass, planeOk = pass;
    if (pass) for (int i = 0; i < (int)got->size(); ++i) {
      const SwPoint& p = (*got)[i];
      bool isSep = (i % 3 == 2);
      if (isSep != isNanf(p.Scale.x)) sepOk = false;
      if (!isSep && std::fabs(p.Position.y) > 1e-5f) planeOk = false;  // XZ plane → y=0
    }
    pass = pass && sepOk && planeOk;
    ok = ok && pass;
    std::printf("[selftest-gizmo-gridplane] LEG1 transport n=%zu want=%zu(XZ,%dx%d) sep=%d plane(y=0)=%d -> %s\n",
                got ? got->size() : 0, wantN, kSeg, kSeg, sepOk ? 1 : 0, planeOk ? 1 : 0, pass ? "PASS" : "FAIL");
  }

  // ===== LEG 2 — ★PRODUCTION PIXEL (resident), Orientation XY so the orthographic projection shows it. =====
  {
    const uint32_t RW = 256, RH = 256;
    PointGraph pg(dev, lib, q, RW, RH);
    Graph g;
    Node r; r.id = 1; r.type = "GridPlane"; r.params["Segments"] = (float)kSeg; r.params["Orientation"] = 0.0f;  // XY
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
    // Probe two adjacent VERTICAL lines at world y=-0.125 (NDC -0.0357), BETWEEN the horizontal grid
    // lines, so only vertical lines light up there (the cell midpoint stays dark).
    const float toNdc = 1.0f / 3.5f;
    const float probeY = -0.125f * toNdc;
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
    std::printf("[selftest-gizmo-gridplane] LEG2 ★PRODUCTION adjacentLines a=%d b=%d cellMidDark=%d -> %s\n",
                aLit ? 1 : 0, bLit ? 1 : 0, midDark ? 1 : 0, pass ? "PASS" : "FAIL");
  }

  lib->release(); q->release(); dev->release(); pool->release();
  std::printf("[selftest-gizmo-gridplane] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
