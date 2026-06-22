// PointsOnMesh golden — sibling leaf to point_ops_pointsonmesh.cpp (split to keep each file ≤400 lines,
// the <400 ratchet). R-2 discipline: flat-only is self-deception, so FOUR legs + two RED teeth:
//
//  (1) FLAT direct-cook (exact-position probe): a hand-built UNIT-QUAD mesh (4 verts / 2 faces) + a
//      PointCookCtx with meshVtx+meshIdx, cook directly, byte-read the SwPoints. The 2-tri fixture is
//      the degenerate width=faceCount-2=0 case (PARITY: TiXL always scatters onto face 1). With the
//      .t3 default Seed=10 the C++ side recomputes the wang_hash chain for i=0 → faceIndex=1 (verts
//      2,3,1) → u=0.1538,v=0.1488,w=0.6974 → Position=(0.302637,0.846183,0). Assert every point is
//      in-plane (z≈0) and inside the unit quad, plus the exact i=0 position to 1e-4.
//  (2) FLAT-DRIVER gather: a real flat Graph QuadMesh→PointsOnMesh cooked through PointGraph::cook,
//      read debugCookedBuffer — exercises the meshIdx gather + Count Float port through the driver.
//      Assert count==Count and in-quad.
//  (3) RESIDENT production: QuadMesh→PointsOnMesh→DrawPoints2→RenderTarget via cookResident, read the
//      rendered pixels — asserts lit sprites inside the quad's projected region (the resident mesh
//      gather LIVES).
//  (4) AREA-CDF leg (the seam's reason to exist): a hand-built ≥4-tri UNEQUAL-AREA fixture (two big
//      area-1 faces at y∈[0,1] + two small area-0.25 faces at y∈[2,2.5]; big area fraction = 0.80).
//      Scatter N points → assert the fraction landing on the BIG faces ≈ their area fraction (loose,
//      Monte-Carlo). This is the leg the 2-tri fixture CANNOT do (degeneracy).
//
//  RED teeth (both bite the REAL cook path):
//   • DROP-MESHIDX: c.meshIdx=null → the cook's no-faces guard → empty bag → in-quad assertion fails
//     (bites the new meshIdx bind specifically; the flat-driver tooth OMITS the QuadMesh→op wire).
//   • UNIFORM-AREA: force every normalizedFaceArea=1 (ForceUniformArea) on the unequal-area fixture →
//     points spread evenly (big fraction ≈ 0.5) instead of area-proportionally → the area-fraction
//     assertion fails (bites CalcCdf2).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"
#include "runtime/eval_context.h"
#include "runtime/graph.h"                     // Graph/Node/pinId/findSpec
#include "runtime/graph_bridge.h"              // libFromGraph (resident production path)
#include "runtime/point_graph.h"               // PointCookCtx, PointGraph, registerBuiltinPointOps
#include "runtime/resident_eval_graph.h"       // buildEvalGraph
#include "runtime/sw_mesh.h"                   // SwVertex / SwTriIndex
#include "runtime/tixl_point.h"                // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Defined in point_ops_pointsonmesh.cpp; forceUniformArea is the TEST-ONLY area-weighting RED knob.
void cookPointsOnMeshImpl(PointCookCtx& c, float forceUniformArea);
void registerPointsOnMeshOp();

namespace {

bool nearf(float a, float b, float t = 1e-4f) { return std::fabs(a - b) < t; }

// wang_hash — host mirror of pointsonmesh.metal's bit-exact port (for the exact-position probe).
uint32_t wangHashHost(uint32_t& seed) {
  seed = (seed ^ 61u) ^ (seed >> 16);
  seed *= 9u;
  seed = seed ^ (seed >> 4);
  seed *= 0x27d4eb2du;
  seed = seed ^ (seed >> 15);
  return seed;
}

// ── UNIT-QUAD fixture (mirrors QuadMesh Segments=(1,1)): verts index = row + col*rows, rows=2 ───────
//   v0(0,0,0) v1(0,1,0) v2(1,0,0) v3(1,1,0); faces (QuadMesh.cs winding):
//   face0 = Int3(0,2,1)  face1 = Int3(2,3,1).  TBN identity, Selection=1, ColorRgb white.
constexpr float kQuadVerts[4][3] = {{0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {1, 1, 0}};

MTL::Buffer* makeQuadVtx(MTL::Device* dev) {
  SwVertex v[4];
  for (int i = 0; i < 4; ++i) {
    v[i] = SwVertex{};
    v[i].Position = {kQuadVerts[i][0], kQuadVerts[i][1], kQuadVerts[i][2]};
    v[i].Normal = {0, 0, 1}; v[i].Tangent = {1, 0, 0}; v[i].Bitangent = {0, 1, 0};
    v[i].Texcoord = {0, 0}; v[i].Texcoord2 = {0, 0};
    v[i].Selection = 1.0f; v[i].ColorRgb = {1, 1, 1};
  }
  return dev->newBuffer(v, 4 * sizeof(SwVertex), MTL::ResourceStorageModeShared);
}
MTL::Buffer* makeQuadIdx(MTL::Device* dev) {
  SwTriIndex idx[2] = {{0, 2, 1}, {2, 3, 1}};  // QuadMesh.cs:106-107 winding for the unit cell
  return dev->newBuffer(idx, 2 * sizeof(SwTriIndex), MTL::ResourceStorageModeShared);
}

// (1) FLAT direct-cook leg. wireMesh=false (RED tooth DROP-MESHIDX) → null index buffer → empty bag.
bool flatDirectLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireMesh,
                   std::vector<SwPoint>& out, uint32_t N) {
  MTL::Buffer* vtx = makeQuadVtx(dev);
  MTL::Buffer* idx = makeQuadIdx(dev);
  MTL::Buffer* outBag = dev->newBuffer((size_t)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  std::memset(outBag->contents(), 0, (size_t)N * sizeof(SwPoint));

  std::map<std::string, float> params;
  params["Seed"] = 10.0f;  // .t3 default → matches the host probe below
  params["UseVertexSelection"] = 1.0f;

  PointCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.count = N; c.output = outBag; c.params = &params;
  c.meshVtx = vtx; c.meshVtxCount = 4;
  if (wireMesh) { c.meshIdx = idx; c.meshFaceCount = 2; }  // else: null index / 0 faces (RED tooth)
  cookPointsOnMeshImpl(c, /*forceUniformArea=*/0.0f);

  out.assign(N, SwPoint{});
  std::memcpy(out.data(), outBag->contents(), (size_t)N * sizeof(SwPoint));
  vtx->release(); idx->release(); outBag->release();
  return true;
}

// (2) FLAT-DRIVER leg: a real flat Graph cooked through PointGraph::cook. wireMesh=false omits the
// QuadMesh→op wire (RED tooth) → the gather loses its mesh → empty bag.
bool flatDriverLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireMesh,
                   std::vector<SwPoint>& out, uint32_t& cookedCount, uint32_t N) {
  registerBuiltinPointOps();
  registerPointsOnMeshOp();

  Graph g;
  Node quad; quad.id = 1; quad.type = "QuadMesh"; g.nodes.push_back(quad);  // defaults → 4 verts / 2 tris
  Node pom; pom.id = 2; pom.type = "PointsOnMesh"; pom.params["Count"] = (float)N; pom.params["Seed"] = 10.0f;
  g.nodes.push_back(pom);

  int quadOut = -1, pomMeshIn = 0;
  { const NodeSpec* qs = findSpec("QuadMesh");
    for (size_t i = 0; i < qs->ports.size(); ++i) if (!qs->ports[i].isInput) { quadOut = (int)i; break; }
    const NodeSpec* ms = findSpec("PointsOnMesh");
    for (size_t i = 0; i < ms->ports.size(); ++i)
      if (ms->ports[i].isInput && ms->ports[i].dataType == "Mesh") { pomMeshIn = (int)i; break; } }
  if (wireMesh) g.connections.push_back({100, pinId(1, quadOut), pinId(2, pomMeshIn)});

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 256, 256);
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/2);

  cookedCount = pg.debugCookedCount(2);
  out.assign(N, SwPoint{});
  const MTL::Buffer* buf = pg.debugCookedBuffer(2);
  if (buf && cookedCount >= N)
    std::memcpy(out.data(), const_cast<MTL::Buffer*>(buf)->contents(), (size_t)N * sizeof(SwPoint));
  return buf != nullptr;
}

// (3) RESIDENT production leg: QuadMesh→PointsOnMesh→DrawPoints2→RenderTarget via cookResident.
// wireMesh=false (RED tooth): omit the QuadMesh→op wire → 0 points → no sprites.
bool residentLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireMesh,
                 std::vector<uint8_t>& px, uint32_t& ow, uint32_t& oh) {
  registerBuiltinPointOps();
  registerPointsOnMeshOp();
  const uint32_t W = 256, H = 256;

  Graph g;
  Node quad; quad.id = 1; quad.type = "QuadMesh"; g.nodes.push_back(quad);
  Node pom; pom.id = 2; pom.type = "PointsOnMesh"; pom.params["Count"] = 256.0f; pom.params["Seed"] = 10.0f;
  g.nodes.push_back(pom);
  Node draw; draw.id = 3; draw.type = "DrawPoints2";
  draw.params["Color.x"] = 1.0f; draw.params["Color.y"] = 0.0f;
  draw.params["Color.z"] = 0.0f; draw.params["Color.w"] = 1.0f;  // RED sprites
  draw.params["Radius"] = 0.05f; draw.params["UseWForSize"] = 0.0f;
  g.nodes.push_back(draw);
  Node rt; rt.id = 4; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);

  int quadOut = -1, pomMeshIn = 0, pomOut = -1, drawPtsIn = 0, drawOut = -1, rtCmdIn = 0;
  { const NodeSpec* qs = findSpec("QuadMesh");
    for (size_t i = 0; i < qs->ports.size(); ++i) if (!qs->ports[i].isInput) { quadOut = (int)i; break; }
    const NodeSpec* ms = findSpec("PointsOnMesh");
    for (size_t i = 0; i < ms->ports.size(); ++i) {
      if (ms->ports[i].isInput && ms->ports[i].dataType == "Mesh") pomMeshIn = (int)i;
      if (!ms->ports[i].isInput) pomOut = (int)i; }
    const NodeSpec* ds = findSpec("DrawPoints2");
    for (size_t i = 0; i < ds->ports.size(); ++i) {
      if (ds->ports[i].isInput && ds->ports[i].dataType == "Points") drawPtsIn = (int)i;
      if (!ds->ports[i].isInput) drawOut = (int)i; }
    const NodeSpec* rs = findSpec("RenderTarget");
    for (size_t i = 0; i < rs->ports.size(); ++i)
      if (rs->ports[i].isInput && rs->ports[i].dataType == "Command") { rtCmdIn = (int)i; break; } }
  if (wireMesh)
    g.connections.push_back({100, pinId(1, quadOut), pinId(2, pomMeshIn)});
  g.connections.push_back({101, pinId(2, pomOut), pinId(3, drawPtsIn)});
  g.connections.push_back({102, pinId(3, drawOut), pinId(4, rtCmdIn)});

  SymbolLibrary slib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, W, H);
  pg.cookResident(rg, ctx, nullptr, /*RenderTarget path*/ "4");

  MTL::Texture* tex = pg.target();
  ow = tex ? (uint32_t)tex->width() : 0;
  oh = tex ? (uint32_t)tex->height() : 0;
  if (!tex || ow == 0 || oh == 0) return false;
  px.assign((size_t)ow * oh * 4, 0);
  tex->getBytes(px.data(), ow * 4, MTL::Region::Make2D(0, 0, ow, oh), 0);
  return true;
}

// ── UNEQUAL-AREA fixture for the AREA-CDF leg: 8 verts / 4 faces. Big faces 0,1 (area 1 each) at
//    y∈[0,1]; small faces 2,3 (area 0.25 each) at y∈[2,2.5]. big area fraction = 2.0/2.5 = 0.80. ────
MTL::Buffer* makeUnequalVtx(MTL::Device* dev) {
  const float P[8][3] = {
      {0, 0, 0}, {2, 0, 0}, {0, 1, 0}, {2, 1, 0},        // big rect (y 0..1)
      {0, 2, 0}, {1, 2, 0}, {0, 2.5f, 0}, {1, 2.5f, 0},  // small rect (y 2..2.5)
  };
  SwVertex v[8];
  for (int i = 0; i < 8; ++i) {
    v[i] = SwVertex{};
    v[i].Position = {P[i][0], P[i][1], P[i][2]};
    v[i].Normal = {0, 0, 1}; v[i].Tangent = {1, 0, 0}; v[i].Bitangent = {0, 1, 0};
    v[i].Texcoord = {0, 0}; v[i].Texcoord2 = {0, 0};
    v[i].Selection = 1.0f; v[i].ColorRgb = {1, 1, 1};
  }
  return dev->newBuffer(v, 8 * sizeof(SwVertex), MTL::ResourceStorageModeShared);
}
MTL::Buffer* makeUnequalIdx(MTL::Device* dev) {
  SwTriIndex idx[4] = {{0, 1, 2}, {1, 3, 2}, {4, 5, 6}, {5, 7, 6}};  // faces 0,1 big ; 2,3 small
  return dev->newBuffer(idx, 4 * sizeof(SwTriIndex), MTL::ResourceStorageModeShared);
}

// AREA-CDF leg: scatter N points over the unequal-area fixture, classify by y (big y<1.5 / small y>1.5).
// forceUniformArea breaks the weighting (the UNIFORM-AREA RED tooth). Returns the big-face fraction.
float areaCdfLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, float forceUniformArea,
                 uint32_t N) {
  MTL::Buffer* vtx = makeUnequalVtx(dev);
  MTL::Buffer* idx = makeUnequalIdx(dev);
  MTL::Buffer* outBag = dev->newBuffer((size_t)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  std::memset(outBag->contents(), 0, (size_t)N * sizeof(SwPoint));

  std::map<std::string, float> params;
  params["Seed"] = 10.0f; params["UseVertexSelection"] = 0.0f;  // pure area weighting (no selection)

  PointCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.count = N; c.output = outBag; c.params = &params;
  c.meshVtx = vtx; c.meshVtxCount = 8; c.meshIdx = idx; c.meshFaceCount = 4;
  cookPointsOnMeshImpl(c, forceUniformArea);

  std::vector<SwPoint> out(N);
  std::memcpy(out.data(), outBag->contents(), (size_t)N * sizeof(SwPoint));
  int big = 0, valid = 0;
  for (uint32_t i = 0; i < N; ++i) {
    // a point that was actually written has Position somewhere in the fixture (y in [0,2.5]).
    if (out[i].Position.y < -0.01f || out[i].Position.y > 2.51f) continue;  // skip unwritten/garbage
    ++valid;
    if (out[i].Position.y < 1.5f) ++big;
  }
  vtx->release(); idx->release(); outBag->release();
  return valid > 0 ? (float)big / (float)valid : -1.0f;
}

}  // namespace

int runPointsOnMeshSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-pointsonmesh] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // ── (1) FLAT direct-cook: in-quad + exact i=0 position probe. ──────────────────────────────────
  const uint32_t N = 64;
  std::vector<SwPoint> d;
  // RED tooth DROP-MESHIDX (injectBug): null index buffer → empty bag → in-quad assertion fails.
  flatDirectLeg(dev, q, lib, /*wireMesh=*/!injectBug, d, N);

  bool flatInQuad = true;
  for (uint32_t i = 0; i < N; ++i) {
    const SwPoint& p = d[i];
    if (!(p.Position.x >= -1e-3f && p.Position.x <= 1.0f + 1e-3f &&
          p.Position.y >= -1e-3f && p.Position.y <= 1.0f + 1e-3f &&
          std::fabs(p.Position.z) < 1e-3f)) { flatInQuad = false; break; }
  }
  // Exact i=0 probe: recompute the wang_hash chain (Seed=10 → mul=103170; i=0 → rng=0). 2-tri mesh →
  // PARITY face 1 = verts (2,3,1). Predicted Position from the barycentric (see file header).
  uint32_t mul = (uint32_t)(10.0f * 10317.0f);  // (uint)(Seed*10317)
  uint32_t rng = 0u * mul;                       // i.x=0
  float xi  = (float)wangHashHost(rng) * (1.0f / 4294967296.0f);  (void)xi;  // CDF draw (face=1 on 2-tri)
  float xi1 = (float)wangHashHost(rng) * (1.0f / 4294967296.0f);
  float xi2 = (float)wangHashHost(rng) * (1.0f / 4294967296.0f);
  float xi1s = std::sqrt(xi1);
  float u = 1.0f - xi1s, v = xi2 * xi1s, w = 1.0f - u - v;
  // face 1 verts = (2,3,1): V2(1,0,0) V3(1,1,0) V1(0,1,0).
  float ex = 1.0f * u + 1.0f * v + 0.0f * w;
  float ey = 0.0f * u + 1.0f * v + 1.0f * w;
  float ez = 0.0f;
  bool probePass = nearf(d[0].Position.x, ex) && nearf(d[0].Position.y, ey) && nearf(d[0].Position.z, ez);
  bool flatPass = flatInQuad && probePass;

  // ── (2) FLAT-DRIVER: count + in-quad + SCATTER VARIETY via PointGraph::cook + debugCookedBuffer. ─
  // The variety check (some point not at the origin) is the RED-tooth bite: omitting the QuadMesh→op
  // wire (injectBug) leaves the bag sized to Count but zero-filled (no mesh → the cook's no-faces
  // guard) — every Position is (0,0,0), which is trivially "in-quad" but has NO scatter → fails here.
  std::vector<SwPoint> dr; uint32_t drCount = 0;
  flatDriverLeg(dev, q, lib, /*wireMesh=*/!injectBug, dr, drCount, N);
  bool drvInQuad = (drCount == N);
  int drScattered = 0;
  for (uint32_t i = 0; i < N && drvInQuad; ++i) {
    drvInQuad = drvInQuad && dr[i].Position.x >= -1e-3f && dr[i].Position.x <= 1.0f + 1e-3f &&
                dr[i].Position.y >= -1e-3f && dr[i].Position.y <= 1.0f + 1e-3f &&
                std::fabs(dr[i].Position.z) < 1e-3f;
    if (dr[i].Position.x > 0.05f || dr[i].Position.y > 0.05f) ++drScattered;
  }
  bool drvPass = drvInQuad && drScattered > 4;  // a real surface scatter, not an all-origin empty bag

  // ── (3) RESIDENT production: RED sprites somewhere inside the projected quad. ────────────────────
  std::vector<uint8_t> px; uint32_t ow = 0, oh = 0;
  bool gotRes = residentLeg(dev, q, lib, /*wireMesh=*/!injectBug, px, ow, oh);
  int redLit = 0;
  if (gotRes)
    for (size_t i = 0; i < (size_t)ow * oh; ++i)
      if (px[i * 4 + 0] > 120 && px[i * 4 + 1] < 80 && px[i * 4 + 2] < 80) ++redLit;
  bool resPass = gotRes && redLit > 20;  // a real cloud of red sprites

  // ── (4) AREA-CDF: big-face fraction ≈ area fraction 0.80 (Monte-Carlo, loose). ──────────────────
  const uint32_t AN = 4000;
  // RED tooth UNIFORM-AREA (injectBug): ForceUniformArea=1 → big fraction collapses toward 0.5.
  float bigFrac = areaCdfLeg(dev, q, lib, /*forceUniformArea=*/injectBug ? 1.0f : 0.0f, AN);
  bool areaPass = bigFrac > 0.70f && bigFrac < 0.88f;  // area-weighted ≈0.789; uniform ≈0.47 (RED)

  bool pass = flatPass && drvPass && resPass && areaPass;
  std::printf("[selftest-pointsonmesh] FLAT-DIRECT: inQuad=%d pos0=(%.4f,%.4f,%.4f) want(%.4f,%.4f,%.4f) "
              "probe=%d pass=%d | FLAT-DRIVER: count=%u(need %u) pass=%d | RESIDENT: %ux%u redLit=%d "
              "pass=%d | AREA-CDF: bigFrac=%.3f(want 0.70..0.88) pass=%d | injectBug=%d -> %s\n",
              flatInQuad ? 1 : 0, d[0].Position.x, d[0].Position.y, d[0].Position.z, ex, ey, ez,
              probePass ? 1 : 0, flatPass ? 1 : 0, drCount, N, drvPass ? 1 : 0, ow, oh, redLit,
              resPass ? 1 : 0, bigFrac, areaPass ? 1 : 0, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
