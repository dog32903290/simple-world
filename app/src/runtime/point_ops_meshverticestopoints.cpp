// MeshVerticesToPoints — the FIRST Points op with a MESH INPUT, and the proving op for the
// mesh-into-points seam (PointCookCtx::meshVtx). Faithful port of external/tixl
// .../point/generate/MeshVerticesToPoints.cs + .../Assets/shaders/points/generate/MeshVerticesToPoints.hlsl.
// A GENERATOR (one Point per mesh vertex): per vertex →
//   Position = v.Position + OffsetByTBN·{Tangent,Bitangent,Normal}·OffsetScale  (default offset 0 → vtx pos)
//   Rotation = normalize(qFromMatrix3Precise(transpose(float3x3(Tangent,Bitangent,Normal))))
//   Color    = float4(v.ColorRgb, 1) ;  FX1 = FX2 = v.Selection ;  Scale = (1,1,1).
//
// SEAM (mesh-into-points): the cook driver gathers this op's Mesh input port (cookMeshInto on the flat
// path / cookResidentMesh on the resident path → PointCookCtx::meshVtx + meshVtxCount) and the op is
// registered countFromMeshVtx=true so its output bag is sized to the mesh's VERTEX count. An UNWIRED
// Mesh → meshVtx null / count 0 → nothing to cook (empty bag), exactly like an unwired Points input.
//
// .cs/.hlsl parity + named forks: OffsetByTBN (Vector3, .t3 default (0,0,0)) + W (.t3 default 1.0 = the
// shader's OffsetScale). At the defaults the offset terms vanish (Position = vertex pos). The TBN→quat
// uses the project's qFromMatrix3Precise (the addnoise.metal column-convention: HLSL transpose(float3x3
// (T,B,N)) ≡ MSL float3x3(T,B,N)). The .hlsl field `Selected` is our SwVertex `Selection` (lone @64).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dispatch.h"                  // calcDispatchCount
#include "runtime/eval_context.h"
#include "runtime/graph.h"                     // Graph/Node/pinId/findSpec
#include "runtime/graph_bridge.h"              // libFromGraph (flat Graph -> SymbolLibrary, production path)
#include "runtime/meshverticestopoints_params.h"  // MeshVtxToPointsParams + MVTP_* bindings
#include "runtime/point_graph.h"               // PointCookCtx, registerPointOp, PointGraph
#include "runtime/resident_eval_graph.h"       // buildEvalGraph (production path)
#include "runtime/sw_mesh.h"                   // SwVertex (80B)
#include "runtime/tixl_point.h"                // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// MeshVerticesToPoints cook: dispatch the per-vertex kernel over the gathered mesh, one Point per vertex.
// No Mesh input (or 0 vertices) → empty bag (the count fork already sized output to 0 → nothing to do).
void cookMeshVerticesToPoints(PointCookCtx& c) {
  if (!c.output || !c.lib || c.count == 0) return;
  const MTL::Buffer* verts = c.meshVtx;
  if (!verts || c.meshVtxCount == 0) return;  // unwired Mesh input -> empty bag (like an unwired Points input)

  MTL::Function* fn =
      c.lib->newFunction(NS::String::string("meshverticestopoints", NS::UTF8StringEncoding));
  if (!fn) return;
  NS::Error* err = nullptr;
  MTL::ComputePipelineState* pso = c.dev->newComputePipelineState(fn, &err);
  fn->release();
  if (!pso) return;

  MeshVtxToPointsParams P{};
  P.Count = c.meshVtxCount;
  float tbn[3] = {0, 0, 0};
  cookVecN(c, "OffsetByTBN", tbn, 3, tbn);   // Vector3 -> OffsetByTBN.x/.y/.z (default 0)
  P.OffsetByTbnX = tbn[0]; P.OffsetByTbnY = tbn[1]; P.OffsetByTbnZ = tbn[2];
  P.OffsetScale = cookParam(c, "W", 1.0f);   // .t3 W input = OffsetScale (default 1.0)

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(verts), 0, MVTP_Vertices);
  enc->setBuffer(c.output, 0, MVTP_ResultPoints);
  enc->setBytes(&P, sizeof(P), MVTP_Params);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.meshVtxCount, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  pso->release();
}

}  // namespace

void registerMeshVerticesToPointsOp() {
  // countFromMeshVtx=true: the output bag is sized to the gathered Mesh's vertex count (one Point/vertex).
  registerPointOp("MeshVerticesToPoints", cookMeshVerticesToPoints, /*stNew=*/nullptr, /*stFree=*/nullptr,
                  /*countTransform=*/nullptr, /*countFromFirstPointsInput=*/false, /*countFromMeshVtx=*/true);
}

// =============================================================================================
// Golden — THREE legs (R-2: flat-only is self-deception; flat-direct + flat-DRIVER + RESIDENT production).
// Fixture: QuadMesh Segments=(1,1) → 4 verts at v0(0,0,0) v1(0,1,0) v2(1,0,0) v3(1,1,0); ColorRgb=(1,1,1),
// Selection=1; default params (OffsetByTBN=0, W=1) → Position == vertex pos.
//
//  (1) FLAT direct-cook: hand-build a 4-vert SwVertex buffer + a PointCookCtx with meshVtx + meshVtxCount,
//      dispatch cookMeshVerticesToPoints, CPU-readback the 4 SwPoints → assert Position == the 4 vertex
//      positions, Color.rgb == ColorRgb (1,1,1), count == 4. RED tooth: bind-drop (meshVtx=null) → empty.
//  (2) FLAT-DRIVER gather: a real flat Graph QuadMesh→MeshVerticesToPoints, PointGraph::cook(target=op),
//      read the cooked Points buffer back (debugCookedBuffer) → 4 points at the quad verts (exercises the
//      flat cookNode Mesh gather + the countFromMeshVtx fork). RED tooth: seam bind-drop (omit the wire).
//  (3) RESIDENT (production): QuadMesh→MeshVerticesToPoints→DrawPoints2→RenderTarget through the canonical
//      production path (libFromGraph → buildEvalGraph → cookResident), read the rendered pixels → 4 sprites
//      at the projected vertex positions (proves the resident point Mesh gather LIVES). RED tooth: OMIT the
//      QuadMesh→op wire → the resident Mesh gather (cookNode's new Mesh branch → cookResidentMesh) loses its
//      mesh → 0 points → no sprites (QuadMesh's index-corrupting meshInjectBug would NOT move a per-vertex
//      point; the bind-drop is the production tooth that bites this op). All teeth bite the REAL cook path.
// =============================================================================================

namespace {

constexpr float kQuadVerts[4][3] = {{0, 0, 0}, {0, 1, 0}, {1, 0, 0}, {1, 1, 0}};

// Build a 4-vertex QuadMesh-equivalent SwVertex buffer (positions kQuadVerts; TBN identity basis;
// Selection=1; ColorRgb=(1,1,1)) — matches QuadMesh defaults (mesh_ops_quadmesh.cpp).
MTL::Buffer* makeQuadVtxBuffer(MTL::Device* dev) {
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

bool nearf(float a, float b, float t = 1e-4f) { return std::fabs(a - b) < t; }

// (4) ROT+OFFSET direct-cook leg — one vertex with a NON-IDENTITY TBN and NON-ZERO offset.
// Fixture: Position=(2,3,0); T=(0,1,0), B=(-1,0,0), N=(0,0,1) (90° rotation about Z).
// OffsetByTBN=(1,2,0.5), OffsetScale=2.
//
// Expected Rotation (qFromMatrix3Precise on col-major float3x3(T,B,N)):
//   m = [col0=T=(0,1,0), col1=B=(-1,0,0), col2=N=(0,0,1)]
//   tr = m[0][0]+m[1][1]+m[2][2] = 0+0+1 = 1 > 0 → branch 1
//   S = sqrt(1+1)*2 = 2√2
//   q = (0/S, 0/S, (m[0][1]-m[1][0])/S, 0.25S)
//     = (0, 0, (1-(-1))/(2√2), √2/2)
//     = (0, 0, 1/√2, 1/√2) ≈ (0, 0, 0.7071, 0.7071)
// Rotation verified via probe: qRotateVec3((1,0,0), q) → (0,1,0) (90° about Z). ✓
// Sign ambiguity: assert |dot(q_gpu, q_expected)| ≈ 1.0 (q and -q represent same rotation).
//
// Expected Position: (2,3,0) + 1*(0,1,0)*2 + 2*(-1,0,0)*2 + 0.5*(0,0,1)*2
//                  = (2,3,0) + (0,2,0) + (-4,0,0) + (0,0,1) = (-2, 5, 1).
//
// RED tooth: corrupts TBN (flip tangent to (-1,0,0)) → output Rotation ≠ expected,
// Position offset formula changes → either assertion fails → exit 1 bites.
bool rotOffsetLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool injectBug,
                  SwPoint& outPt) {
  SwVertex v{};
  v.Position = {2.0f, 3.0f, 0.0f};
  if (injectBug) {
    v.Tangent = {-1.0f, 0.0f, 0.0f};  // corrupted T → Rotation and Position both wrong
    v.Bitangent = {-1.0f, 0.0f, 0.0f};
  } else {
    v.Tangent = {0.0f, 1.0f, 0.0f};   // 90° Z-rotation basis
    v.Bitangent = {-1.0f, 0.0f, 0.0f};
  }
  v.Normal = {0.0f, 0.0f, 1.0f};
  v.Selection = 1.0f; v.ColorRgb = {1.0f, 1.0f, 1.0f};

  MTL::Buffer* vtx = dev->newBuffer(&v, sizeof(SwVertex), MTL::ResourceStorageModeShared);
  MTL::Buffer* outBag = dev->newBuffer(sizeof(SwPoint), MTL::ResourceStorageModeShared);
  std::memset(outBag->contents(), 0, sizeof(SwPoint));

  PointCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 99; c.count = 1;
  c.output = outBag; c.meshVtx = vtx; c.meshVtxCount = 1;
  // Override params: OffsetByTBN=(1,2,0.5), OffsetScale=2.
  // cookMeshVerticesToPoints reads c's param map; set them via a one-shot Graph node below.
  // Simpler: call the kernel with a custom P struct directly (same path as flatDirectLeg).
  MeshVtxToPointsParams P{};
  P.Count = 1;
  P.OffsetByTbnX = 1.0f; P.OffsetByTbnY = 2.0f; P.OffsetByTbnZ = 0.5f;
  P.OffsetScale = 2.0f;

  MTL::Function* fn =
      lib->newFunction(NS::String::string("meshverticestopoints", NS::UTF8StringEncoding));
  bool ok = false;
  if (fn) {
    NS::Error* err = nullptr;
    MTL::ComputePipelineState* pso = dev->newComputePipelineState(fn, &err);
    fn->release();
    if (pso) {
      MTL::CommandBuffer* cmd = q->commandBuffer();
      MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
      enc->setComputePipelineState(pso);
      enc->setBuffer(vtx, 0, MVTP_Vertices);
      enc->setBuffer(outBag, 0, MVTP_ResultPoints);
      enc->setBytes(&P, sizeof(P), MVTP_Params);
      enc->dispatchThreadgroups(MTL::Size::Make(1, 1, 1), MTL::Size::Make(64, 1, 1));
      enc->endEncoding();
      cmd->commit();
      cmd->waitUntilCompleted();
      pso->release();
      ok = true;
    }
  }
  std::memcpy(&outPt, outBag->contents(), sizeof(SwPoint));
  vtx->release(); outBag->release();
  return ok;
}

// (1) FLAT direct-cook leg. wireMesh=false (RED tooth) drops the mesh bind → empty bag.
bool flatDirectLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireMesh,
                   SwPoint out[4], uint32_t& cookedCount) {
  MTL::Buffer* vtx = makeQuadVtxBuffer(dev);
  MTL::Buffer* outBag = dev->newBuffer(4 * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  std::memset(outBag->contents(), 0, 4 * sizeof(SwPoint));

  PointCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.count = wireMesh ? 4 : 0;  // the count fork sizes to meshVtxCount; bind-drop → 0
  c.output = outBag;
  if (wireMesh) { c.meshVtx = vtx; c.meshVtxCount = 4; }  // else: meshVtx null / count 0 (RED tooth)
  cookMeshVerticesToPoints(c);

  cookedCount = c.count;
  std::memcpy(out, outBag->contents(), 4 * sizeof(SwPoint));
  vtx->release(); outBag->release();
  return true;
}

// (2) FLAT-DRIVER gather leg: a real flat Graph cooked through PointGraph::cook. wireMesh=false omits the
// QuadMesh→op wire (RED tooth) → the gather loses its mesh → empty bag.
bool flatDriverLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireMesh,
                   SwPoint out[4], uint32_t& cookedCount) {
  registerBuiltinPointOps();  // QuadMesh (mesh) + MeshVerticesToPoints (point) self-register / registered

  Graph g;
  Node quad; quad.id = 1; quad.type = "QuadMesh"; g.nodes.push_back(quad);  // defaults → 4 verts
  Node mvp; mvp.id = 2; mvp.type = "MeshVerticesToPoints"; g.nodes.push_back(mvp);

  int quadOut = -1, mvpMeshIn = 0;
  { const NodeSpec* qs = findSpec("QuadMesh");
    for (size_t i = 0; i < qs->ports.size(); ++i) if (!qs->ports[i].isInput) { quadOut = (int)i; break; }
    const NodeSpec* ms = findSpec("MeshVerticesToPoints");
    for (size_t i = 0; i < ms->ports.size(); ++i)
      if (ms->ports[i].isInput && ms->ports[i].dataType == "Mesh") { mvpMeshIn = (int)i; break; } }
  if (wireMesh) g.connections.push_back({100, pinId(1, quadOut), pinId(2, mvpMeshIn)});

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 256, 256);
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/2);  // cook the op itself (a Points producer)

  cookedCount = pg.debugCookedCount(2);
  for (int i = 0; i < 4; ++i) out[i] = SwPoint{};
  const MTL::Buffer* buf = pg.debugCookedBuffer(2);
  if (buf && cookedCount >= 4)
    std::memcpy(out, const_cast<MTL::Buffer*>(buf)->contents(), 4 * sizeof(SwPoint));
  return buf != nullptr;
}

// (3) RESIDENT production leg: QuadMesh→MeshVerticesToPoints→DrawPoints2→RenderTarget via cookResident.
// Returns the rendered pixels. RED tooth (wireMesh=false): OMIT the QuadMesh→op wire so the RESIDENT
// Mesh gather (cookNode's new Mesh branch → cookResidentMesh) loses its mesh → 0 points → no sprites.
// MeshVerticesToPoints is per-VERTEX and ignores indices, so QuadMesh's index-corrupting meshInjectBug
// would NOT move any point (it bites index-consuming consumers like DrawMeshUnlit); the production tooth
// that actually bites THIS op is the seam bind-drop on the resident gather.
bool residentLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireMesh,
                 std::vector<uint8_t>& px, uint32_t& ow, uint32_t& oh) {
  registerBuiltinPointOps();
  const uint32_t W = 256, H = 256;

  Graph g;
  Node quad; quad.id = 1; quad.type = "QuadMesh"; g.nodes.push_back(quad);  // verts at (0,0)…(1,1)
  Node mvp; mvp.id = 2; mvp.type = "MeshVerticesToPoints"; g.nodes.push_back(mvp);
  Node draw; draw.id = 3; draw.type = "DrawPoints2";
  draw.params["Color.x"] = 1.0f; draw.params["Color.y"] = 0.0f;
  draw.params["Color.z"] = 0.0f; draw.params["Color.w"] = 1.0f;  // RED sprites
  draw.params["Radius"] = 0.05f; draw.params["UseWForSize"] = 0.0f;  // fixed visible sprite
  g.nodes.push_back(draw);
  Node rt; rt.id = 4; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);

  int quadOut = -1, mvpMeshIn = 0, mvpOut = -1, drawPtsIn = 0, drawOut = -1, rtCmdIn = 0;
  { const NodeSpec* qs = findSpec("QuadMesh");
    for (size_t i = 0; i < qs->ports.size(); ++i) if (!qs->ports[i].isInput) { quadOut = (int)i; break; }
    const NodeSpec* ms = findSpec("MeshVerticesToPoints");
    for (size_t i = 0; i < ms->ports.size(); ++i) {
      if (ms->ports[i].isInput && ms->ports[i].dataType == "Mesh") mvpMeshIn = (int)i;
      if (!ms->ports[i].isInput) mvpOut = (int)i; }
    const NodeSpec* ds = findSpec("DrawPoints2");
    for (size_t i = 0; i < ds->ports.size(); ++i) {
      if (ds->ports[i].isInput && ds->ports[i].dataType == "Points") drawPtsIn = (int)i;
      if (!ds->ports[i].isInput) drawOut = (int)i; }
    const NodeSpec* rs = findSpec("RenderTarget");
    for (size_t i = 0; i < rs->ports.size(); ++i)
      if (rs->ports[i].isInput && rs->ports[i].dataType == "Command") { rtCmdIn = (int)i; break; } }
  if (wireMesh)
    g.connections.push_back({100, pinId(1, quadOut), pinId(2, mvpMeshIn)});  // QuadMesh → MeshVerticesToPoints
  g.connections.push_back({101, pinId(2, mvpOut), pinId(3, drawPtsIn)});    // op → DrawPoints2
  g.connections.push_back({102, pinId(3, drawOut), pinId(4, rtCmdIn)});     // DrawPoints2 → RenderTarget

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

}  // namespace

int runMeshVerticesToPointsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-meshverticestopoints] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // ── (1) FLAT direct-cook: Position == vertex pos, Color.rgb == (1,1,1), count == 4. ─────────────
  SwPoint d[4]; uint32_t dCount = 0;
  flatDirectLeg(dev, q, lib, /*wireMesh=*/!injectBug, d, dCount);
  bool flatPass = (dCount == 4);
  for (int i = 0; i < 4 && flatPass; ++i) {
    flatPass = flatPass && nearf(d[i].Position.x, kQuadVerts[i][0]) &&
               nearf(d[i].Position.y, kQuadVerts[i][1]) && nearf(d[i].Position.z, kQuadVerts[i][2]) &&
               nearf(d[i].Color.x, 1.0f) && nearf(d[i].Color.y, 1.0f) && nearf(d[i].Color.z, 1.0f) &&
               nearf(d[i].Color.w, 1.0f) && nearf(d[i].Scale.x, 1.0f) && nearf(d[i].FX1, 1.0f);
  }

  // ── (2) FLAT-DRIVER gather: 4 points at the quad verts via PointGraph::cook + debugCookedBuffer. ─
  SwPoint dr[4]; uint32_t drCount = 0;
  flatDriverLeg(dev, q, lib, /*wireMesh=*/!injectBug, dr, drCount);
  bool drvPass = (drCount == 4);
  for (int i = 0; i < 4 && drvPass; ++i)
    drvPass = drvPass && nearf(dr[i].Position.x, kQuadVerts[i][0]) &&
              nearf(dr[i].Position.y, kQuadVerts[i][1]) && nearf(dr[i].Position.z, kQuadVerts[i][2]);

  // ── (3) RESIDENT production: 4 RED sprites at the projected vertex positions. ────────────────────
  std::vector<uint8_t> px; uint32_t ow = 0, oh = 0;
  bool gotRes = residentLeg(dev, q, lib, /*wireMesh=*/!injectBug, px, ow, oh);
  // DrawPoints2 projects ORTHOGRAPHICALLY: NDC = Position.xy / viewExtent (viewExtent=3.5, the
  // RenderDrawItem default — NOT a camera matrix; see draw_points2.metal:58). A faithful cook lights a
  // RED sprite at each vertex's projected pixel. injectBug (meshInjectBug) corrupts a QuadMesh vertex
  // in the REAL cook → that point's sprite leaves its projected pixel → its probe goes dark (genuine).
  const float viewExtent = 3.5f;
  int litVerts = 0;
  if (gotRes) {
    for (int i = 0; i < 4; ++i) {
      float ndcX = kQuadVerts[i][0] / viewExtent, ndcY = kQuadVerts[i][1] / viewExtent;
      int xpx = (int)((ndcX * 0.5f + 0.5f) * (float)(ow - 1) + 0.5f);
      int ypx = (int)((1.0f - (ndcY * 0.5f + 0.5f)) * (float)(oh - 1) + 0.5f);
      if (xpx < 0 || ypx < 0 || xpx >= (int)ow || ypx >= (int)oh) continue;
      size_t k = ((size_t)ypx * ow + xpx) * 4;
      if (px[k] > 120 && px[k + 1] < 80 && px[k + 2] < 80) ++litVerts;  // RED sprite at the vertex
    }
  }
  bool resPass = gotRes && litVerts == 4;

  // ── (4) ROT+OFFSET: T=(0,1,0) B=(-1,0,0) N=(0,0,1) pos=(2,3,0) offset=(1,2,0.5) scale=2. ────────
  // Expected quat (0, 0, 1/√2, 1/√2) ≈ (0, 0, 0.7071, 0.7071); sign via |dot|≈1.
  // Expected position (-2, 5, 1). injectBug corrupts TBN → both assertions fail.
  SwPoint roPt{}; rotOffsetLeg(dev, q, lib, injectBug, roPt);
  const float kSqrt2Over2 = 0.7071067811865476f;
  float qdot = std::fabs(roPt.Rotation.x * 0.0f + roPt.Rotation.y * 0.0f +
                         roPt.Rotation.z * kSqrt2Over2 + roPt.Rotation.w * kSqrt2Over2);
  bool roPass = nearf(qdot, 1.0f, 1e-3f) &&
                nearf(roPt.Position.x, -2.0f) && nearf(roPt.Position.y, 5.0f) &&
                nearf(roPt.Position.z,  1.0f);

  bool pass = flatPass && drvPass && resPass && roPass;
  std::printf("[selftest-meshverticestopoints] FLAT-DIRECT: count=%u pos0=(%.2f,%.2f,%.2f) pass=%d | "
              "FLAT-DRIVER: count=%u pos3=(%.2f,%.2f,%.2f) pass=%d | RESIDENT: %ux%u litVerts=%d(need 4) "
              "pass=%d | ROT+OFFSET: pos=(%.2f,%.2f,%.2f) |qdot|=%.4f pass=%d | injectBug=%d -> %s\n",
              dCount, d[0].Position.x, d[0].Position.y, d[0].Position.z, flatPass ? 1 : 0, drCount,
              dr[3].Position.x, dr[3].Position.y, dr[3].Position.z, drvPass ? 1 : 0, ow, oh, litVerts,
              resPass ? 1 : 0, roPt.Position.x, roPt.Position.y, roPt.Position.z, qdot,
              roPass ? 1 : 0, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
