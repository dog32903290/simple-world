// drawboxgizmo_golden — --selftest-gizmo-box. C3 Tranche-1: DrawBoxGizmo (12-edge wireframe cube) via the
// pointlist seam. Two legs (the pointlist_golden discipline):
//   LEG 1 — TRANSPORT (flat): cook DrawBoxGizmo as terminal → debugCookedPointList → assert the host point
//           list element-for-element vs a CLOSED-FORM reference re-derived from CommonPointSets.CubePoints
//           (NOT a copy of emitBoxEdges) — 12 edges × (2 pts + separator) = 36 points, every edge on the
//           [-0.5,0.5]^3 cube, every 3rd point a Scale=NaN separator.
//   LEG 2 — ★PRODUCTION PIXEL (resident): DrawBoxGizmo → ListToBuffer → DrawLines → RenderTarget through
//           the canonical production path (libFromGraph → buildEvalGraph → cookResident), readback pixels:
//           a point ON a projected edge (between its two endpoints, NOT at a corner) is lit; a point in a
//           known-empty interior cell is dark. Proves the resident DrawLines dispatch draws the wireframe.
//
// injectBug routes through pointListInjectBug(): DrawBoxGizmo CLEARS its real output → empty list →
// transport size mismatch + black production screen → RED. Teeth on the real cook path.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"            // libFromGraph
#include "runtime/point_graph.h"             // PointGraph::cook/cookResident + debugCookedPointList + registerBuiltinPointOps
#include "runtime/pointlist_op_registry.h"   // pointListInjectBug / swPointDefault
#include "runtime/resident_eval_graph.h"     // buildEvalGraph
#include "runtime/tixl_point.h"              // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

bool nearf(float a, float b, float t = 1e-4f) { return std::fabs(a - b) < t; }
bool isNanf(float a) { return a != a; }

// CLOSED-FORM CommonPointSets.CubePoints reference (CommonPointSets.cs:104-167, re-derived — NOT a copy of
// emitBoxEdges). 12 edges in TiXL order, each 2 pts + separator; S=0.5, then a uniform scale applied.
std::vector<SwPoint> boxRef(float scale) {
  const float S = 0.5f * scale;
  struct E { float a[3], b[3]; };
  const E edges[12] = {
      {{-S,-S, S},{ S,-S, S}}, {{-S, S, S},{ S, S, S}}, {{-S,-S,-S},{ S,-S,-S}}, {{-S, S,-S},{ S, S,-S}},
      {{-S,-S, S},{-S, S, S}}, {{ S,-S, S},{ S, S, S}}, {{-S,-S,-S},{-S, S,-S}}, {{ S,-S,-S},{ S, S,-S}},
      {{-S,-S,-S},{-S,-S, S}}, {{ S,-S,-S},{ S,-S, S}}, {{-S, S,-S},{-S, S, S}}, {{ S, S,-S},{ S, S, S}},
  };
  std::vector<SwPoint> out;
  auto pt = [&](const float v[3]) {
    SwPoint p = swPointDefault(); p.Position = {v[0], v[1], v[2]}; p.FX1 = 1.0f;
    p.Color = {1.0f, 1.0f, 1.0f, 1.0f}; return p;
  };
  auto sep = []() { SwPoint s = swPointDefault(); s.Scale = {std::nanf(""), std::nanf(""), std::nanf("")}; return s; };
  for (const E& e : edges) { out.push_back(pt(e.a)); out.push_back(pt(e.b)); out.push_back(sep()); }
  return out;
}

bool ptEq(const SwPoint& g, const SwPoint& w) {
  bool gs = isNanf(g.Scale.x), ws = isNanf(w.Scale.x);
  if (gs != ws) return false;
  if (!nearf(g.Position.x, w.Position.x) || !nearf(g.Position.y, w.Position.y) || !nearf(g.Position.z, w.Position.z))
    return false;
  if (gs) return true;
  return nearf(g.FX1, w.FX1);
}

bool litAt(const std::vector<uint8_t>& px, uint32_t W, float ndcX, float ndcY) {
  int x = (int)((ndcX * 0.5f + 0.5f) * (float)(W - 1) + 0.5f);
  int y = (int)((1.0f - (ndcY * 0.5f + 0.5f)) * (float)(W - 1) + 0.5f);
  if (x < 0 || y < 0 || x >= (int)W || y >= (int)W) return false;
  size_t i = ((size_t)y * W + x) * 4;
  return px[i] > 40 || px[i + 1] > 40 || px[i + 2] > 40;
}

}  // namespace

int runGizmoBoxSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) { std::printf("[selftest-gizmo-box] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }
  registerBuiltinPointOps();  // pulls the pointlist cook driver + DrawLines(cmd) + RenderTarget(tex)

  bool ok = true;
  const float kScale = 4.0f;  // big enough that edges land well inside the NDC frame (viewExtent 3.5)

  // ===== LEG 1 — TRANSPORT (flat). =====
  {
    PointGraph pg(dev, lib, q, 64, 64);
    Graph g;
    Node r; r.id = 1; r.type = "DrawBoxGizmo"; r.params["Scale"] = kScale; g.nodes.push_back(r);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.deltaTime = 1.0f / 60.0f;
    pointListInjectBug() = injectBug;
    pg.cook(g, ctx, nullptr, /*terminal=*/1);
    pointListInjectBug() = false;

    const std::vector<SwPoint>* got = pg.debugCookedPointList(1);
    std::vector<SwPoint> want = boxRef(kScale);  // 36 points
    bool pass = got && got->size() == want.size();  // 36; injectBug → empty → FAIL
    if (pass) for (size_t i = 0; i < want.size(); ++i) if (!ptEq((*got)[i], want[i])) { pass = false; break; }
    // structural probe: every 3rd element is a separator; the 24 edge points all lie at |coord|==2S radius box.
    bool sepOk = pass;
    if (pass) for (int i = 0; i < (int)got->size(); ++i) if ((i % 3 == 2) != isNanf((*got)[i].Scale.x)) { sepOk = false; break; }
    pass = pass && sepOk;
    ok = ok && pass;
    std::printf("[selftest-gizmo-box] LEG1 transport n=%zu want=%zu(12 edges) sep=%d -> %s\n",
                got ? got->size() : 0, want.size(), sepOk ? 1 : 0, pass ? "PASS" : "FAIL");
  }

  // ===== LEG 2 — ★PRODUCTION PIXEL (resident): DrawBoxGizmo→ListToBuffer→DrawLines→RenderTarget. =====
  {
    const uint32_t RW = 256, RH = 256;
    PointGraph pg(dev, lib, q, RW, RH);
    Graph g;
    Node r; r.id = 1; r.type = "DrawBoxGizmo"; r.params["Scale"] = kScale; g.nodes.push_back(r);
    Node ltb; ltb.id = 2; ltb.type = "ListToBuffer"; g.nodes.push_back(ltb);
    Node drw; drw.id = 3; drw.type = "DrawLines"; drw.params["LineWidth"] = 0.1f; g.nodes.push_back(drw);
    Node rt; rt.id = 4; rt.type = "RenderTarget";
    rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)RW; rt.params["CustomH"] = (float)RH; g.nodes.push_back(rt);
    g.connections.push_back({100, pinId(1, 0), pinId(2, 1)});  // DrawBoxGizmo.Points → ListToBuffer.Lists
    g.connections.push_back({101, pinId(2, 0), pinId(3, 0)});  // ListToBuffer.OutBuffer → DrawLines.points
    g.connections.push_back({102, pinId(3, 1), pinId(4, 0)});  // DrawLines.out → RenderTarget.command

    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.deltaTime = 1.0f / 60.0f;
    pointListInjectBug() = injectBug;
    pg.cookResident(rg, ctx, nullptr, /*RenderTarget path*/ "4");
    pointListInjectBug() = false;

    MTL::Texture* tex = pg.target();
    bool sized = tex && (uint32_t)tex->width() == RW && (uint32_t)tex->height() == RH;
    // The front (z=+S) and back (z=-S) faces project (orthographic XY/viewExtent) to the SAME square: the
    // top edge at world y=+2 (S=2), the bottom at y=-2, left x=-2, right x=+2. viewExtent=3.5 → NDC = coord/3.5.
    // Probe the MIDDLE of the top edge (world (0, +2) → NDC (0, 0.571)) — lit (edge body, not a corner).
    // Probe the box CENTER (world (0,0) → NDC (0,0)) — dark (no edge through the middle).
    const float Sx = 0.5f * kScale;            // = 2
    const float topNdcY = Sx / 3.5f;           // ≈ 0.571
    bool edgeLit = false, centerDark = true;
    if (sized) {
      std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
      tex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
      edgeLit = litAt(px, RW, 0.0f, topNdcY);     // middle of the top edge
      centerDark = !litAt(px, RW, 0.0f, 0.0f);    // box center (hollow)
    }
    bool pass = sized && (injectBug ? (!edgeLit) : (edgeLit && centerDark));
    ok = ok && pass;
    std::printf("[selftest-gizmo-box] LEG2 ★PRODUCTION edgeLit@(0,%.2f)=%d centerDark=%d -> %s\n",
                topNdcY, edgeLit ? 1 : 0, centerDark ? 1 : 0, pass ? "PASS" : "FAIL");
  }

  lib->release(); q->release(); dev->release(); pool->release();
  std::printf("[selftest-gizmo-box] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
