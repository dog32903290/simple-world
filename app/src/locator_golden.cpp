// locator_golden — --selftest-gizmo-locator. C3 Tranche-1: Locator (3-axis cross marker, geometry only —
// label/screen-constant DROPPED, see gizmo_geometry.h emitAxisCross fork) via the pointlist seam.
//   LEG 1 — TRANSPORT (flat): assert the 3 axis segments vs the TiXL CommonPointSets.CrossPoints set
//           (Y, X, Z order; 3 edges × (2 pts + sep) = 9 points; F1/F2/Color pinned).
//   LEG 2 — ★PRODUCTION PIXEL (resident): Locator → ListToBuffer → DrawLines → RenderTarget, readback: a
//           point on the +X arm (between origin and +x tip) is lit; an off-axis diagonal point is dark.
//
// ORACLE PROVENANCE (P5 fix — all expectations from TiXL source, SHA 395c4c55):
//   • Cross point set: Operators/Lib/point/generate/CommonPointSets.cs:106 (S=0.5) + :108-120
//     (CrossPoints — Y axis pair, X axis pair, Z axis pair, each followed by Point.Separator(), every
//     point F1=1, F2=1) and :49-51 (Init(): Orientation=Identity, Color=Vector4.One → WHITE points; the
//     teal Locator Color param tints at the DrawLines stage, NOT in the point buffer — Locator.t3
//     connection Color→DrawLines slot 75419a73).
//   • Arm-length semantics: Locator.t3 wires the op's Size input (43f63f2d) into a Transform child's
//     UniformScale slot (a7b1e667; Transform.cs:60-61) and Transform.cs:28 applies s = Scale *
//     UniformScale — so in TiXL the drawn cross is the unit ±0.5 CrossPoints scaled by Size:
//     ARM HALF-LENGTH = 0.5 * Size (default Size=0.5 → arms reach ±0.25).
//   ★NAMED DIVERGENCE (reported prey — do NOT silently rebase): the sw cook
//     (runtime/pointlist_ops_locator.cpp:37) scales by k = 2*Size → arms reach ±Size, i.e. 2× the TiXL
//     arm. This golden pins the CURRENT sw behavior (armHalf = Size) so the suite stays green while the
//     fork is decided; when the leaf is re-anchored to TiXL, flip kArmHalfIsSize below to the 0.5*Size
//     convention in the same commit.
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

// TiXL arm-scale convention vs the sw fork (see header ★NAMED DIVERGENCE). TiXL: armHalf = 0.5*Size
// (CommonPointSets.cs:106 unit ±0.5 × Transform.cs:28 UniformScale=Size). sw cook: armHalf = Size
// (pointlist_ops_locator.cpp:37 k=2*Size). The golden pins the CURRENT sw behavior; flip to false when
// the leaf is re-anchored to the TiXL convention.
constexpr bool kArmHalfIsSize = false;  // leaf re-anchored to TiXL 0.5*Size (2026-07-03)

// Locator cross reference — CommonPointSets.CrossPoints TRANSCRIBED from CommonPointSets.cs:108-120
// (Y, X, Z axis order, Point.Separator() after each pair, F1=1 F2=1 per point) + :49-51 (Init():
// Color=Vector4.One, Orientation=Identity). 3 edges × (2 pts + sep) = 9 points. Only the arm HALF-LENGTH
// uses the sw forked convention (see kArmHalfIsSize above).
std::vector<SwPoint> crossRef(float size) {
  const float k = kArmHalfIsSize ? size : 0.5f * size;  // arm half-length
  struct E { float a[3], b[3]; };
  const E edges[3] = {
      {{0,-k,0},{0, k,0}},  // Y axis  (CrossPoints[0..1], cs:110-111)
      {{-k,0,0},{ k,0,0}},  // X axis  (CrossPoints[3..4], cs:113-114)
      {{0,0,-k},{0,0, k}},  // Z axis  (CrossPoints[6..7], cs:117-118)
  };
  std::vector<SwPoint> out;
  auto pt = [&](const float v[3]) {
    SwPoint p = swPointDefault();  // TiXL `new Point()` seed
    p.Position = {v[0], v[1], v[2]};
    p.FX1 = 1.0f;                  // F1 = 1 (cs:110)
    p.FX2 = 1.0f;                  // F2 = 1 (cs:110)
    p.Color = {1, 1, 1, 1};        // Init() Color = Vector4.One (cs:50) — WHITE, not the teal param
    return p;
  };
  auto sep = []() { SwPoint s = swPointDefault(); s.Scale = {std::nanf(""), std::nanf(""), std::nanf("")}; return s; };
  for (const E& e : edges) { out.push_back(pt(e.a)); out.push_back(pt(e.b)); out.push_back(sep()); }
  return out;
}

bool ptEq(const SwPoint& g, const SwPoint& w) {
  bool gs = isNanf(g.Scale.x), ws = isNanf(w.Scale.x);
  if (gs != ws) return false;
  if (!nearf(g.Position.x, w.Position.x) || !nearf(g.Position.y, w.Position.y) || !nearf(g.Position.z, w.Position.z)) return false;
  if (gs) return true;  // separator: only the NaN-Scale marker + position matter
  // Non-separator points carry the full TiXL CrossPoints attribute set (cs:108-120 + Init cs:49-51).
  return nearf(g.FX1, w.FX1) && nearf(g.FX2, w.FX2) &&
         nearf(g.Color.x, w.Color.x) && nearf(g.Color.y, w.Color.y) &&
         nearf(g.Color.z, w.Color.z) && nearf(g.Color.w, w.Color.w);
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
  const float kSize = 2.0f;  // Size param. Under the sw forked convention (kArmHalfIsSize, see header
                             // ★NAMED DIVERGENCE) arms reach ±2; TiXL semantics would be ±1 (0.5*Size,
                             // Transform.cs:28 + CommonPointSets.cs:106).

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
    // The X arm runs along world y=0 from x=-1 to x=+1 (TiXL ±0.5*Size arm, leaf re-anchored 2026-07-03)
    // → NDC line y=0, x∈[-0.286,0.286]. The Y arm runs along world x=0 → NDC line x=0. Probe the +X arm
    // at world (0.6,0)→NDC(0.171,0) — lit (mid-arm). Probe an OFF-AXIS diagonal (NDC 0.3, 0.3) — dark
    // (the cross has no diagonal arm).
    const float armNdcX = 0.6f / 3.5f;  // ≈ 0.171
    bool armLit = false, diagDark = true;
    if (sized) {
      std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
      tex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
      armLit = litAt(px, RW, armNdcX, 0.0f);
      diagDark = !litAt(px, RW, 0.3f, 0.3f);
    }
    // SAME assert in both legs (no want-flip): the arm pixel must be lit and the diagonal dark. The
    // injectBug leg corrupts the REAL cook (pointListInjectBug → pointlist_ops_locator.cpp:43 clears the
    // cooked list) so this identical assert goes RED through the production pixel path.
    bool pass = sized && armLit && diagDark;
    ok = ok && pass;
    std::printf("[selftest-gizmo-locator] LEG2 ★PRODUCTION armLit@(%.2f,0)=%d diagDark=%d -> %s\n",
                armNdcX, armLit ? 1 : 0, diagDark ? 1 : 0, pass ? "PASS" : "FAIL");
  }

  lib->release(); q->release(); dev->release(); pool->release();
  if (injectBug) {
    if (ok) {
      // Injection reached nothing — dead tooth. exit 0 so --bite's NO-BITE list catches it (a dead
      // tooth returning 1 would look like a healthy bite forever).
      std::printf("[selftest-gizmo-locator] FAIL: injectBug did not trip any leg (tooth has no bite)\n");
      return 0;
    }
    std::printf("[selftest-gizmo-locator] injectBug correctly RED\n");
    return 1;
  }
  std::printf("[selftest-gizmo-locator] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
