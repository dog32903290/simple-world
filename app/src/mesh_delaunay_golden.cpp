// mesh_delaunay_golden — --selftest-mesh-delaunay. CPU-READBACK golden for DelaunayMesh (pointlist →
// mesh; the pointlist-into-mesh seam's first consumer) + a production-pixel resident leg.
//
// ORACLE (independent-of-impl): the expected triangle SETS below were derived with a BRUTE-FORCE
// empty-circumcircle oracle (a triangle is Delaunay iff its circumcircle strictly contains no other
// point — the canonical DEFINITION, not Bowyer-Watson), then centroid-filtered + winding-reversed per
// DelaunayMesh.cs:343-377. Every case was checked degeneracy-free (no co-circular 4-subsets), so the
// Delaunay triangulation is UNIQUE and implementation-independent (fork-delaunay-bowyer-watson:
// triangle SET is the parity surface; order/rotation is not).
//
// CASE 1: quad (0,0),(1,0),(1.2,1),(0,1) + NaN-Scale 5th point (dropped, .cs:72-92) + Extra (0.5,0.5)
//   kept / (5,5) filtered (.cs:259-266) → 5 verts, faces {(0,3,4),(0,4,1),(1,4,2),(2,4,3)} all CW
//   (.cs:375 reversal); UV bbox / Selection=F1 / ColorRgb / TBN pinned.
// CASE 2: subdivision (.cs:117-160): tri (0,0),(1,0),(0.4,0.9) @0.5 → 7 pts (+0.5,0 / +0.8,0.3 /
//   +0.6,0.6 / +0.2,0.45 lerped), faces {(0,6,1),(1,3,2),(1,4,3),(1,6,4),(4,6,5)}.
// CASE 3: centroid filter (.cs:359-371): L-hex → 5 Delaunay tris, CCW(2,4,3) centroid (1.33,1.33)
//   outside → dropped → faces {(0,3,1),(0,5,3),(1,3,2),(3,5,4)}.
// CASE 4: Poisson SMOKE (honest label — emergent System.Random sampling, no closed-form value
//   oracle): fill exists, deterministic across cooks, every fill point inside + ≥ minDist (.cs:202).
// CASE 5: PRODUCTION PIXEL resident leg: NDC quad → DelaunayMesh → DrawMeshUnlit(RED) → Camera(eye
//   -Z) → RenderTarget via cookResident; interior probe RED + corner clear (the RESIDENT
//   pointlist-into-mesh gather). The Camera views from BEHIND (-Z): the op's TiXL-verbatim :375
//   winding reversal emits CW-in-world faces, which sw's mesh executor (front=CCW + cull Back,
//   point_ops_rendertarget.cpp:537-538) culls from the default +Z eye — visible from -Z.
// injectBug → meshInjectBug() → REAL cook corrupts verts[0] → case 1 diverges → RED.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/delaunay_core.h"        // pointInPolygon / tooCloseToBoundary (case-4 predicates)
#include "runtime/eval_context.h"
#include "runtime/field_camera.h"         // defaultLayerCameraForward/objectToClipSpace/mat4TransformPointDivW
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"         // libFromGraph
#include "runtime/mesh_op_registry.h"
#include "runtime/point_graph.h"
#include "runtime/pointlist_op_registry.h"  // pointListCookFns (stub PointList sources)
#include "runtime/resident_eval_graph.h"
#include "runtime/sw_mesh.h"
#include "runtime/tixl_point.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

bool nearDy(float a, float b) { return std::fabs(a - b) < 1e-5f; }
SwPoint mkPt(float x, float y, float f1, float cr, float cg, float cb) {
  SwPoint p{};
  p.Position = {x, y, 0.0f}; p.FX1 = f1; p.Rotation = {0, 0, 0, 1};
  p.Color = {cr, cg, cb, 1.0f}; p.Scale = {1, 1, 1}; p.FX2 = 1.0f;
  return p;
}

// Stub boundary source: Shape 0 = case-1 quad (+ NaN-Scale 5th point), 1 = case-2 triangle,
// 2 = case-3 L-hexagon, 3 = case-5 NDC quad.
void cookStubDelaunayBoundary(PointListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();
  const int shape = (int)(pointListParam(c.params, "Shape", 0.0f) + 0.5f);
  if (shape == 0) {
    c.output->push_back(mkPt(0.0f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f));
    c.output->push_back(mkPt(1.0f, 0.0f, 0.6f, 0.0f, 1.0f, 0.0f));
    c.output->push_back(mkPt(1.2f, 1.0f, 0.7f, 0.0f, 0.0f, 1.0f));
    c.output->push_back(mkPt(0.0f, 1.0f, 0.8f, 0.3f, 0.3f, 0.3f));
    SwPoint nan = mkPt(9.0f, 9.0f, 1.0f, 1, 1, 1);
    nan.Scale = {std::nanf(""), 1.0f, 1.0f};  // the .cs:72-92 NaN-Scale drop witness
    c.output->push_back(nan);
  } else if (shape == 1) {
    const float T[3][2] = {{0, 0}, {1, 0}, {0.4f, 0.9f}};
    for (auto& p : T) c.output->push_back(mkPt(p[0], p[1], 1, 1, 1, 1));
  } else if (shape == 2) {
    const float L[6][2] = {{0, 0}, {2, 0}, {2, 1}, {1, 1}, {1, 2}, {0, 2}};
    for (auto& p : L) c.output->push_back(mkPt(p[0], p[1], 1, 1, 1, 1));
  } else {
    const float N[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}};
    for (auto& p : N) c.output->push_back(mkPt(p[0], p[1], 1, 1, 1, 1));
  }
}

// Stub extra source: (0.5,0.5) inside the case-1 quad + (5,5) outside (must be filtered).
void cookStubDelaunayExtra(PointListCookCtx& c) {
  if (!c.output) return;
  c.output->clear();
  c.output->push_back(mkPt(0.5f, 0.5f, 0.9f, 1.0f, 1.0f, 0.0f));
  c.output->push_back(mkPt(5.0f, 5.0f, 1.0f, 1, 1, 1));
}

NodeSpec stubListSpecDy(const char* type, bool withShape) {
  NodeSpec s;
  s.type = type; s.title = type;
  if (withShape) {
    PortSpec sh; sh.id = "Shape"; sh.name = "Shape"; sh.dataType = "Float"; sh.isInput = true;
    sh.def = 0.0f; sh.minV = 0.0f; sh.maxV = 3.0f; s.ports.push_back(sh);
  }
  PortSpec out; out.id = "Points"; out.name = "Points"; out.dataType = "PointList"; out.isInput = false;
  s.ports.push_back(out);
  return s;
}

void installDelaunayStubs() {
  pointListCookFns()["StubDelaunayBoundary"] = cookStubDelaunayBoundary;
  pointListCookFns()["StubDelaunayExtra"] = cookStubDelaunayExtra;
  std::map<std::string, NodeSpec> dyn;
  dyn["StubDelaunayBoundary"] = stubListSpecDy("StubDelaunayBoundary", true);
  dyn["StubDelaunayExtra"] = stubListSpecDy("StubDelaunayExtra", false);
  setDynamicSpecs(std::move(dyn));
}
int portIdxDy(const char* type, const char* id, bool input) {
  const NodeSpec* s = findSpec(type);
  if (!s) return -1;
  for (size_t i = 0; i < s->ports.size(); ++i)
    if (s->ports[i].isInput == input && s->ports[i].id == id) return (int)i;
  return -1;
}

// Canonicalize a face list: rotate each triple min-first (cyclic → winding preserved), sort the list.
struct Face3 { int a, b, c; };
std::vector<Face3> canonFaces(const SwTriIndex* f, uint32_t n) {
  std::vector<Face3> out;
  out.reserve(n);
  for (uint32_t i = 0; i < n; ++i) {
    int t[3] = {f[i].X, f[i].Y, f[i].Z};
    int r = (t[1] < t[0]) ? 1 : 0;
    if (t[2] < t[r]) r = 2;
    out.push_back({t[r], t[(r + 1) % 3], t[(r + 2) % 3]});
  }
  std::sort(out.begin(), out.end(), [](const Face3& x, const Face3& y) {
    return x.a != y.a ? x.a < y.a : (x.b != y.b ? x.b < y.b : x.c < y.c);
  });
  return out;
}
bool facesEq(const std::vector<Face3>& g2, const std::vector<Face3>& w) {
  if (g2.size() != w.size()) return false;
  for (size_t i = 0; i < g2.size(); ++i)
    if (g2[i].a != w[i].a || g2[i].b != w[i].b || g2[i].c != w[i].c) return false;
  return true;
}

// Cook a DelaunayMesh graph (flat) and read the pair back.
bool cookDelaunayFlat(MTL::Device* dev, MTL::Library* lib, MTL::CommandQueue* q, float shape,
                      bool wireExtra, float subdiv, float fillDensity, float seed, float tweak,
                      std::vector<SwVertex>& verts, std::vector<SwTriIndex>& faces) {
  PointGraph pg(dev, lib, q, 64, 64);
  Graph g;
  Node b; b.id = 1; b.type = "StubDelaunayBoundary"; b.params["Shape"] = shape; g.nodes.push_back(b);
  Node e; e.id = 2; e.type = "StubDelaunayExtra"; g.nodes.push_back(e);
  Node d; d.id = 3; d.type = "DelaunayMesh";
  d.params["SubdivideLongEdges"] = subdiv; d.params["FillDensity"] = fillDensity;
  d.params["Seed"] = seed; d.params["Tweak"] = tweak;
  g.nodes.push_back(d);
  const int bOut = portIdxDy("StubDelaunayBoundary", "Points", false);
  const int eOut = portIdxDy("StubDelaunayExtra", "Points", false);
  const int dBIn = portIdxDy("DelaunayMesh", "BoundaryPoints", true);
  const int dEIn = portIdxDy("DelaunayMesh", "ExtraPoints", true);
  g.connections.push_back({101, pinId(1, bOut), pinId(3, dBIn)});
  if (wireExtra) g.connections.push_back({102, pinId(2, eOut), pinId(3, dEIn)});

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, 3);
  const MTL::Buffer* vb = nullptr; const MTL::Buffer* ib = nullptr;
  uint32_t vc = 0, fc = 0;
  if (!pg.debugCookedMesh(3, vb, vc, ib, fc)) return false;
  verts.assign((const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents(),
               (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents() + vc);
  faces.assign((const SwTriIndex*)const_cast<MTL::Buffer*>(ib)->contents(),
               (const SwTriIndex*)const_cast<MTL::Buffer*>(ib)->contents() + fc);
  return true;
}

float signedArea2Dy(const SwVertex* v, const SwTriIndex& t) {
  const float ax = v[t.X].Position.x, ay = v[t.X].Position.y;
  return (v[t.Y].Position.x - ax) * (v[t.Z].Position.y - ay) -
         (v[t.Y].Position.y - ay) * (v[t.Z].Position.x - ax);
}

}  // namespace

int runMeshDelaunayGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  meshInjectBug() = injectBug;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  registerBuiltinPointOps();
  installDelaunayStubs();
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  bool ok = true;

  // ── CASE 1: quad + NaN drop + ExtraPoints inside/outside + full vertex spine. ──
  {
    std::vector<SwVertex> v; std::vector<SwTriIndex> f;
    bool got = cookDelaunayFlat(dev, lib, q, /*shape*/ 0, /*extra*/ true, 0, 0, 0, 0, v, f);
    bool pass = got && v.size() == 5 && f.size() == 4;
    if (pass) {
      bool posOk = nearDy(v[0].Position.x, 0) && nearDy(v[1].Position.x, 1) &&
                   nearDy(v[2].Position.x, 1.2f) && nearDy(v[3].Position.y, 1) &&
                   nearDy(v[4].Position.x, 0.5f) && nearDy(v[4].Position.y, 0.5f);
      // UVs: x∈[0,1.2] y∈[0,1] (.cs:291-329): v2 uv=(1,1); v4 uv=(0.5/1.2, 0.5).
      bool uvOk = nearDy(v[2].Texcoord.x, 1.0f) && nearDy(v[2].Texcoord.y, 1.0f) &&
                  nearDy(v[4].Texcoord.x, 0.5f / 1.2f) && nearDy(v[4].Texcoord.y, 0.5f);
      bool selOk = nearDy(v[0].Selection, 0.5f) && nearDy(v[2].Selection, 0.7f) &&
                   nearDy(v[4].Selection, 0.9f);  // Selection = F1 (.cs:338)
      bool colOk = nearDy(v[1].ColorRgb.y, 1.0f) && nearDy(v[4].ColorRgb.x, 1.0f) &&
                   nearDy(v[4].ColorRgb.y, 1.0f) && nearDy(v[4].ColorRgb.z, 0.0f);
      bool nrmOk = nearDy(v[0].Normal.z, 1.0f) && nearDy(v[0].Tangent.x, 1.0f) &&
                   nearDy(v[0].Bitangent.y, 1.0f);
      const std::vector<Face3> want = {{0, 3, 4}, {0, 4, 1}, {1, 4, 2}, {2, 4, 3}};
      bool fOk = facesEq(canonFaces(f.data(), (uint32_t)f.size()), want);
      bool windOk = true;  // every face CW (.cs:375 reversal) → signed area < 0
      for (const SwTriIndex& t : f) windOk = windOk && (signedArea2Dy(v.data(), t) < 0.0f);
      pass = posOk && uvOk && selOk && colOk && nrmOk && fOk && windOk;
      std::printf("[selftest-mesh-delaunay] case1 quad+extra: vc=%zu fc=%zu posOk=%d uvOk=%d selOk=%d "
                  "colOk=%d fOk=%d windOk=%d\n", v.size(), f.size(), posOk, uvOk, selOk, colOk, fOk,
                  windOk);
    } else {
      std::printf("[selftest-mesh-delaunay] case1 FAIL: got=%d vc=%zu fc=%zu (want 5/4)\n", got,
                  v.size(), f.size());
    }
    ok = ok && pass;
  }

  // ── CASE 2: edge subdivision (7 points, 5 faces). ──
  if (ok && !injectBug) {
    std::vector<SwVertex> v; std::vector<SwTriIndex> f;
    bool got = cookDelaunayFlat(dev, lib, q, 1, false, /*subdiv*/ 0.5f, 0, 0, 0, v, f);
    bool pass = got && v.size() == 7 && f.size() == 5;
    if (pass) {
      bool posOk = nearDy(v[1].Position.x, 0.5f) && nearDy(v[1].Position.y, 0.0f) &&
                   nearDy(v[3].Position.x, 0.8f) && nearDy(v[3].Position.y, 0.3f) &&
                   nearDy(v[4].Position.x, 0.6f) && nearDy(v[4].Position.y, 0.6f) &&
                   nearDy(v[6].Position.x, 0.2f) && nearDy(v[6].Position.y, 0.45f);
      bool attrOk = nearDy(v[1].Selection, 1.0f) && nearDy(v[1].ColorRgb.x, 1.0f);  // lerp F1=1/color
      const std::vector<Face3> want = {{0, 6, 1}, {1, 3, 2}, {1, 4, 3}, {1, 6, 4}, {4, 6, 5}};
      bool fOk = facesEq(canonFaces(f.data(), (uint32_t)f.size()), want);
      pass = posOk && attrOk && fOk;
      std::printf("[selftest-mesh-delaunay] case2 subdiv: vc=%zu fc=%zu posOk=%d attrOk=%d fOk=%d\n",
                  v.size(), f.size(), posOk, attrOk, fOk);
    } else {
      std::printf("[selftest-mesh-delaunay] case2 FAIL: got=%d vc=%zu fc=%zu (want 7/5)\n", got,
                  v.size(), f.size());
    }
    ok = ok && pass;
  }

  // ── CASE 3: centroid filter on the L-hexagon (5 Delaunay tris → 4 kept). ──
  if (ok && !injectBug) {
    std::vector<SwVertex> v; std::vector<SwTriIndex> f;
    bool got = cookDelaunayFlat(dev, lib, q, 2, false, 0, 0, 0, 0, v, f);
    bool pass = got && v.size() == 6 && f.size() == 4;
    if (pass) {
      const std::vector<Face3> want = {{0, 3, 1}, {0, 5, 3}, {1, 3, 2}, {3, 5, 4}};
      pass = facesEq(canonFaces(f.data(), (uint32_t)f.size()), want);
      std::printf("[selftest-mesh-delaunay] case3 L-filter: vc=%zu fc=%zu (the (2,3,4) centroid-outside "
                  "tri dropped) fOk=%d\n", v.size(), f.size(), pass);
    } else {
      std::printf("[selftest-mesh-delaunay] case3 FAIL: got=%d vc=%zu fc=%zu (want 6/4)\n", got,
                  v.size(), f.size());
    }
    ok = ok && pass;
  }

  // ── CASE 4: Poisson fill SMOKE (deterministic + .cs predicates; no value oracle — header). ──
  if (ok && !injectBug) {
    std::vector<SwVertex> v1, v2; std::vector<SwTriIndex> f1, f2;
    bool g1 = cookDelaunayFlat(dev, lib, q, 0, false, 0, /*FillDensity*/ 0.5f, 42, 0.2f, v1, f1);
    bool g2 = cookDelaunayFlat(dev, lib, q, 0, false, 0, 0.5f, 42, 0.2f, v2, f2);
    bool pass = g1 && g2 && v1.size() > 4 && v1.size() == v2.size() && f1.size() == f2.size();
    if (pass) {
      // Determinism: byte-identical positions across cooks.
      for (size_t i = 0; i < v1.size() && pass; ++i)
        pass = nearDy(v1[i].Position.x, v2[i].Position.x) && nearDy(v1[i].Position.y, v2[i].Position.y);
      // Predicates: every FILL vertex (index ≥ 4) inside the polygon, ≥ minDist from boundary.
      std::vector<DelaunayV2> poly = {{0, 0}, {1, 0}, {1.2f, 1}, {0, 1}};
      const float minDist = 0.5f * 0.2f;  // fillDensity(=1-0.5) · Tweak — .cs:202
      for (size_t i = 4; i < v1.size() && pass; ++i) {
        const DelaunayV2 p{v1[i].Position.x, v1[i].Position.y};
        pass = pointInPolygon(p, poly) && !tooCloseToBoundary(p, minDist, poly);
      }
      std::printf("[selftest-mesh-delaunay] case4 poisson-smoke: fillVerts=%zu deterministic+inside+"
                  "minDist -> %s\n", v1.size() - 4, pass ? "ok" : "FAIL");
    } else {
      std::printf("[selftest-mesh-delaunay] case4 FAIL: g1=%d g2=%d vc=%zu/%zu\n", g1, g2, v1.size(),
                  v2.size());
    }
    ok = ok && pass;
  }

  // ── CASE 5: PRODUCTION PIXEL (resident leg) — NDC quad → DrawMeshUnlit → RenderTarget. ──
  if (ok && lib) {
    const uint32_t W = 256, H = 256;
    PointGraph pg(dev, lib, q, W, H);
    Graph g;
    Node b; b.id = 1; b.type = "StubDelaunayBoundary"; b.params["Shape"] = 3.0f; g.nodes.push_back(b);
    Node d; d.id = 2; d.type = "DelaunayMesh"; g.nodes.push_back(d);
    Node draw; draw.id = 3; draw.type = "DrawMeshUnlit";
    draw.params["Color.x"] = 1.0f; draw.params["Color.y"] = 0.0f;
    draw.params["Color.z"] = 0.0f; draw.params["Color.w"] = 1.0f;
    g.nodes.push_back(draw);
    Node cam; cam.id = 5; cam.type = "Camera";  // eye -Z: the CW faces are front from behind (header)
    cam.params["Position.x"] = 0.0f; cam.params["Position.y"] = 0.0f;
    cam.params["Position.z"] = -2.4142135f;
    g.nodes.push_back(cam);
    Node rt; rt.id = 4; rt.type = "RenderTarget";
    rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
    g.nodes.push_back(rt);
    // Ports by spec (dataType-robust): DrawMeshUnlit Mesh-in / out; RenderTarget command-in.
    const NodeSpec* ds = findSpec("DrawMeshUnlit");
    const NodeSpec* rs = findSpec("RenderTarget");
    int meshIn = 0, drawOut = 1, rtCmdIn = 0;
    for (size_t i = 0; i < ds->ports.size(); ++i) {
      if (ds->ports[i].isInput && ds->ports[i].dataType == "Mesh") meshIn = (int)i;
      if (!ds->ports[i].isInput) drawOut = (int)i;
    }
    for (size_t i = 0; i < rs->ports.size(); ++i)
      if (rs->ports[i].isInput && rs->ports[i].dataType == "Command") { rtCmdIn = (int)i; break; }
    const NodeSpec* cs = findSpec("Camera");
    int camCmdIn = 0, camOut = 1;
    for (size_t i = 0; i < cs->ports.size(); ++i) {
      if (cs->ports[i].isInput && cs->ports[i].dataType == "Command") camCmdIn = (int)i;
      if (!cs->ports[i].isInput) camOut = (int)i;
    }
    g.connections.push_back({101, pinId(1, portIdxDy("StubDelaunayBoundary", "Points", false)),
                             pinId(2, portIdxDy("DelaunayMesh", "BoundaryPoints", true))});
    g.connections.push_back({102, pinId(2, portIdxDy("DelaunayMesh", "Data", false)), pinId(3, meshIn)});
    g.connections.push_back({103, pinId(3, drawOut), pinId(5, camCmdIn)});
    g.connections.push_back({104, pinId(5, camOut), pinId(4, rtCmdIn)});

    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    pg.cookResident(rg, ctx, nullptr, "4");

    MTL::Texture* tex = pg.target();
    bool sized = tex && (uint32_t)tex->width() == W && (uint32_t)tex->height() == H;
    bool interiorRed = false, cornerClear = false;
    int ir = 0, ig = 0, ib2 = 0;
    if (sized) {
      std::vector<uint8_t> px((size_t)W * H * 4, 0);
      tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
      auto project = [&](float wx, float wy, float out[3]) {
        LayerCameraForward cam = defaultLayerCameraForward(1.0f);
        Mat4 o2c = objectToClipSpace(mat4Identity(), cam.worldToCamera, cam.cameraToClipSpace);
        mat4TransformPointDivW(o2c, wx, wy, 0.0f, out);
      };
      auto rd = [&](float ndcX, float ndcY, int& r, int& gg2, int& b2) {
        int x = (int)((ndcX * 0.5f + 0.5f) * (float)(W - 1) + 0.5f);
        int y = (int)((1.0f - (ndcY * 0.5f + 0.5f)) * (float)(H - 1) + 0.5f);
        size_t i = ((size_t)y * W + x) * 4;
        r = px[i]; gg2 = px[i + 1]; b2 = px[i + 2];
      };
      float ie[3]; project(0.2f, -0.2f, ie);
      int cr, cg, cb;
      float fc[3]; project(0.9f, 0.9f, fc);
      rd(ie[0], ie[1], ir, ig, ib2);
      rd(fc[0], fc[1], cr, cg, cb);
      interiorRed = ir > 250 && ig < 5 && ib2 < 5;
      cornerClear = cr < 30 && cg < 30 && cb < 30;
    }
    bool pass = sized && interiorRed && cornerClear;
    std::printf("[selftest-mesh-delaunay] case5 resident-pixel: interior=(%d,%d,%d) interiorRed=%d "
                "cornerClear=%d -> %s\n", ir, ig, ib2, interiorRed, cornerClear, pass ? "ok" : "FAIL");
    ok = ok && pass;
  } else if (ok && !lib) {
    std::printf("[selftest-mesh-delaunay] case5 SKIP: no metallib\n");
    ok = false;
  }

  meshInjectBug() = false;
  setDynamicSpecs({});
  pointListCookFns().erase("StubDelaunayBoundary");
  pointListCookFns().erase("StubDelaunayExtra");
  if (lib) lib->release();
  q->release(); dev->release(); pool->release();

  if (injectBug) {
    // did-not-trip → return 0 so --bite's NO-BITE list catches it (GOLDEN_STANDARD P1).
    std::printf("[selftest-mesh-delaunay] injectBug %s\n",
                ok ? "did not trip (v0 corruption changed nothing)"
                   : "correctly RED (REAL cook corrupted verts[0] → case1 diverged)");
    return ok ? 0 : 1;
  }
  std::printf("[selftest-mesh-delaunay] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
