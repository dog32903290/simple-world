// drawspheregizmo_golden — --selftest-gizmo-sphere. C3 Tranche-1: DrawSphereGizmo (lat/long wireframe
// sphere) via the pointlist seam. (See gizmo_geometry.h emitSphereRings [fork-gizmo-sphere-composite].)
//   LEG 1 — TRANSPORT (flat): assert the point count + ring structure vs a CLOSED-FORM re-derivation
//           (rings lat + rings long circles, each segments segments × (2 pts + sep)), every edge point on
//           the unit sphere (|p|==radius), every 3rd point a separator.
//   LEG 2 — ★PRODUCTION PIXEL (resident): sphere → ListToBuffer → DrawLines → RenderTarget, readback: the
//           sphere's silhouette rim (a point on the outer circle at radius·NDC) is lit; a point WELL
//           OUTSIDE the sphere is dark. injectBug → black → RED.
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

constexpr float kPi = 3.14159265358979323846f;
bool isNanf(float a) { return a != a; }

bool litAt(const std::vector<uint8_t>& px, uint32_t W, float ndcX, float ndcY) {
  int x = (int)((ndcX * 0.5f + 0.5f) * (float)(W - 1) + 0.5f);
  int y = (int)((1.0f - (ndcY * 0.5f + 0.5f)) * (float)(W - 1) + 0.5f);
  if (x < 0 || y < 0 || x >= (int)W || y >= (int)W) return false;
  size_t i = ((size_t)y * W + x) * 4;
  return px[i] > 40 || px[i + 1] > 40 || px[i + 2] > 40;
}

}  // namespace

int runGizmoSphereSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) { std::printf("[selftest-gizmo-sphere] FAIL: no metallib\n"); q->release(); dev->release(); pool->release(); return 1; }
  registerBuiltinPointOps();

  bool ok = true;
  const float kRadius = 2.5f;
  const int kRings = 3, kSeg = 16;

  // ===== LEG 1 — TRANSPORT (flat). =====
  {
    PointGraph pg(dev, lib, q, 64, 64);
    Graph g;
    Node r; r.id = 1; r.type = "DrawSphereGizmo";
    r.params["Radius"] = kRadius; r.params["Rings"] = (float)kRings; r.params["Segments"] = (float)kSeg;
    g.nodes.push_back(r);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.deltaTime = 1.0f / 60.0f;
    pointListInjectBug() = injectBug;
    pg.cook(g, ctx, nullptr, /*terminal=*/1);
    pointListInjectBug() = false;

    const std::vector<SwPoint>* got = pg.debugCookedPointList(1);
    // Expected: (kRings lat + kRings long) circles, each kSeg segments × 3 elements.
    size_t wantN = (size_t)(2 * kRings) * (size_t)kSeg * 3;
    bool pass = got && got->size() == wantN;  // injectBug → empty → FAIL
    // structural probes: every 3rd element a separator; every non-separator point on |p|==kRadius.
    bool sepOk = pass, radiusOk = pass;
    if (pass) for (int i = 0; i < (int)got->size(); ++i) {
      const SwPoint& p = (*got)[i];
      bool isSep = (i % 3 == 2);
      if (isSep != isNanf(p.Scale.x)) { sepOk = false; }
      if (!isSep) {
        float rr = std::sqrt(p.Position.x * p.Position.x + p.Position.y * p.Position.y + p.Position.z * p.Position.z);
        if (std::fabs(rr - kRadius) > 1e-3f) radiusOk = false;
      }
    }
    pass = pass && sepOk && radiusOk;
    ok = ok && pass;
    std::printf("[selftest-gizmo-sphere] LEG1 transport n=%zu want=%zu(%d rings×%d seg) sep=%d radius@%.1f=%d -> %s\n",
                got ? got->size() : 0, wantN, 2 * kRings, kSeg, sepOk ? 1 : 0, kRadius, radiusOk ? 1 : 0,
                pass ? "PASS" : "FAIL");
  }

  // ===== LEG 2 — ★PRODUCTION PIXEL (resident). =====
  {
    const uint32_t RW = 256, RH = 256;
    PointGraph pg(dev, lib, q, RW, RH);
    Graph g;
    Node r; r.id = 1; r.type = "DrawSphereGizmo";
    r.params["Radius"] = kRadius; r.params["Rings"] = (float)kRings; r.params["Segments"] = (float)kSeg;
    g.nodes.push_back(r);
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
    // The equator latitude ring (y=0) is the outer circle of radius kRadius in XZ → projects (XY) onto a
    // HORIZONTAL line y=0 spanning x∈[-kRadius,kRadius]. Probe the rim at world (kRadius·0.92, 0) → NDC
    // (kRadius·0.92/3.5, 0) — on the equator line, lit. Probe FAR outside (NDC 0.98, 0.98) → dark.
    const float rimNdcX = kRadius * 0.92f / 3.5f;
    bool rimLit = false, outsideDark = true;
    if (sized) {
      std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
      tex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
      rimLit = litAt(px, RW, rimNdcX, 0.0f);
      outsideDark = !litAt(px, RW, 0.98f, 0.98f);
    }
    bool pass = sized && (injectBug ? (!rimLit) : (rimLit && outsideDark));
    ok = ok && pass;
    std::printf("[selftest-gizmo-sphere] LEG2 ★PRODUCTION rimLit@(%.2f,0)=%d outsideDark=%d -> %s\n",
                rimNdcX, rimLit ? 1 : 0, outsideDark ? 1 : 0, pass ? "PASS" : "FAIL");
  }

  lib->release(); q->release(); dev->release(); pool->release();
  std::printf("[selftest-gizmo-sphere] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
