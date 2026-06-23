// locator_golden — --selftest-gizmo-locator. C3 Tranche-1: Locator (3-axis cross marker, geometry only —
// label/screen-constant DROPPED, see gizmo_geometry.h emitAxisCross fork) via the pointlist seam.
//   LEG 1 — TRANSPORT (flat): assert the 3 axis segments vs a CLOSED-FORM CommonPointSets.CrossPoints
//           re-derivation (Y, X, Z order; ±k on each axis; 3 edges × (2 pts + sep) = 9 points).
//   LEG 2 — ★PRODUCTION PIXEL (resident): Locator → ListToBuffer → DrawLines → RenderTarget, readback: a
//           point on the +X arm (between origin and +x tip) is lit; an off-axis diagonal point is dark.
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

bool nearf(float a, float b, float t = 1e-4f) { return std::fabs(a - b) < t; }
bool isNanf(float a) { return a != a; }

// CLOSED-FORM Locator cross reference (CommonPointSets.CrossPoints, Y/X/Z order). The cook scales the unit
// ±0.5 cross by k = 2*Size, so the arms reach ±Size. 3 edges × (2 pts + sep) = 9 points.
std::vector<SwPoint> crossRef(float size) {
  const float k = size;  // arm half-length (Size)
  struct E { float a[3], b[3]; };
  const E edges[3] = {
      {{0,-k,0},{0, k,0}},  // Y axis
      {{-k,0,0},{ k,0,0}},  // X axis
      {{0,0,-k},{0,0, k}},  // Z axis
  };
  std::vector<SwPoint> out;
  auto pt = [&](const float v[3]) { SwPoint p = swPointDefault(); p.Position = {v[0], v[1], v[2]}; p.FX1 = 1.0f; p.Color = {1,1,1,1}; return p; };
  auto sep = []() { SwPoint s = swPointDefault(); s.Scale = {std::nanf(""), std::nanf(""), std::nanf("")}; return s; };
  for (const E& e : edges) { out.push_back(pt(e.a)); out.push_back(pt(e.b)); out.push_back(sep()); }
  return out;
}

bool ptEq(const SwPoint& g, const SwPoint& w) {
  bool gs = isNanf(g.Scale.x), ws = isNanf(w.Scale.x);
  if (gs != ws) return false;
  if (!nearf(g.Position.x, w.Position.x) || !nearf(g.Position.y, w.Position.y) || !nearf(g.Position.z, w.Position.z)) return false;
  return gs ? true : nearf(g.FX1, w.FX1);
}

bool litAt(const std::vector<uint8_t>& px, uint32_t W, float ndcX, float ndcY) {
  int x = (int)((ndcX * 0.5f + 0.5f) * (float)(W - 1) + 0.5f);
  int y = (int)((1.0f - (ndcY * 0.5f + 0.5f)) * (float)(W - 1) + 0.5f);
  if (x < 0 || y < 0 || x >= (int)W || y >= (int)W) return false;
  size_t i = ((size_t)y * W + x) * 4;
  return px[i] > 40 || px[i + 1] > 40 || px[i + 2] > 40;
}

}  // namespace

int runGizmoLocatorSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) { std::printf("[selftest-gizmo-locator] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }
  registerBuiltinPointOps();

  bool ok = true;
  const float kSize = 2.0f;  // arms reach ±2 (k = 2*Size in the cook; here Size param chosen so arm=±2)
  // The cook scales the unit cross by 2*Size, so arm half-length = Size? No: emit spans ±0.5, k=2*Size →
  // ±0.5*2*Size = ±Size. So Size param = arm half-length. Pass Size=kSize directly.

  // ===== LEG 1 — TRANSPORT (flat). =====
  {
    PointGraph pg(dev, lib, q, 64, 64);
    Graph g;
    Node r; r.id = 1; r.type = "Locator"; r.params["Size"] = kSize; g.nodes.push_back(r);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.deltaTime = 1.0f / 60.0f;
    pointListInjectBug() = injectBug;
    pg.cook(g, ctx, nullptr, /*terminal=*/1);
    pointListInjectBug() = false;

    const std::vector<SwPoint>* got = pg.debugCookedPointList(1);
    std::vector<SwPoint> want = crossRef(kSize);  // 9 points
    bool pass = got && got->size() == want.size();
    if (pass) for (size_t i = 0; i < want.size(); ++i) if (!ptEq((*got)[i], want[i])) { pass = false; break; }
    bool sepOk = pass;
    if (pass) for (int i = 0; i < (int)got->size(); ++i) if ((i % 3 == 2) != isNanf((*got)[i].Scale.x)) { sepOk = false; break; }
    pass = pass && sepOk;
    ok = ok && pass;
    std::printf("[selftest-gizmo-locator] LEG1 transport n=%zu want=%zu(3 axes) sep=%d -> %s\n",
                got ? got->size() : 0, want.size(), sepOk ? 1 : 0, pass ? "PASS" : "FAIL");
  }

  // ===== LEG 2 — ★PRODUCTION PIXEL (resident). =====
  {
    const uint32_t RW = 256, RH = 256;
    PointGraph pg(dev, lib, q, RW, RH);
    Graph g;
    Node r; r.id = 1; r.type = "Locator"; r.params["Size"] = kSize; g.nodes.push_back(r);
    Node ltb; ltb.id = 2; ltb.type = "ListToBuffer"; g.nodes.push_back(ltb);
    Node drw; drw.id = 3; drw.type = "DrawLines"; drw.params["LineWidth"] = 0.1f; g.nodes.push_back(drw);
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
    // The X arm runs along world y=0 from x=-2 to x=+2 → NDC line y=0, x∈[-0.571,0.571]. The Y arm runs
    // along world x=0 → NDC line x=0. Probe the +X arm at world (1.2,0)→NDC(0.343,0) — lit (on the arm).
    // Probe an OFF-AXIS diagonal (NDC 0.3, 0.3) — dark (the cross has no diagonal arm).
    const float armNdcX = 1.2f / 3.5f;  // ≈ 0.343
    bool armLit = false, diagDark = true;
    if (sized) {
      std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
      tex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
      armLit = litAt(px, RW, armNdcX, 0.0f);
      diagDark = !litAt(px, RW, 0.3f, 0.3f);
    }
    bool pass = sized && (injectBug ? (!armLit) : (armLit && diagDark));
    ok = ok && pass;
    std::printf("[selftest-gizmo-locator] LEG2 ★PRODUCTION armLit@(%.2f,0)=%d diagDark=%d -> %s\n",
                armNdcX, armLit ? 1 : 0, diagDark ? 1 : 0, pass ? "PASS" : "FAIL");
  }

  lib->release(); q->release(); dev->release(); pool->release();
  std::printf("[selftest-gizmo-locator] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
