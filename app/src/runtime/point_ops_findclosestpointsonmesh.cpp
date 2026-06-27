// FindClosestPointsOnMesh — a point-MODIFY op that snaps each input Point onto the nearest surface
// point of a mesh. Faithful port of external/tixl
//   .../point/transform/FindClosestPointsOnMesh.cs   (thin shader op: Points + Mesh -> Points)
//   .../Assets/shaders/points/onmesh/FindClosestPointOnMesh.hlsl  (brute-force per-point tri loop)
//
// SEAMS it rides (BOTH already built + production-wired): the mesh-into-points seam
// (PointCookCtx::meshVtx + meshIdx, filled by the cook drivers' Mesh-port loop) AND the ordinary
// Points-input flow (inputs[0]). It is a LEAF on those rails — it touches NO driver/registry/spine code.
//
// Count policy: output bag == INPUT Points count (one result point per input point) → registers with
// countFromFirstPointsInput=true (the value spine sizes `count` to firstPointsCount, point_graph.cpp:325 /
// point_graph_resident.cpp:317). This is a MODIFY op (NOT a generator) — distinct from MeshVerticesToPoints
// (countFromMeshVtx) and PointsOnMesh (Count Float port).
//
// .t3 audit (FindClosestPointsOnMesh.t3): NO user-facing scalar params. Inputs = Mesh (DefaultValue null) +
// Points (DefaultValue null); the .hlsl cbuffer is EMPTY. The .cs declares Points THEN Mesh; we register
// the NodeSpec ports in that order so inputs[0] is the Points bag (countFromFirstPointsInput reads it).
//
// NAMED FORKS (see also findclosestpointsonmesh.metal headers):
//   • The .hlsl's dead `udTriangle`/`dot2` helpers are NOT ported (never called; the live algorithm is
//     `closestPointOnTriangle`). NO behavioural change.
//   • Out-of-range threads write SwPoint.FX2 = 0 (== LegacyPoint.W slot @60); matches .hlsl:158.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/tex_op_cache.h"                  // cachedComputePSO
#include "runtime/dispatch.h"                      // calcDispatchCount
#include "runtime/eval_context.h"
#include "runtime/findclosestpointsonmesh_params.h"  // FcpomParams + FCPOM_* bindings
#include "runtime/graph.h"                         // Graph/Node/pinId/findSpec (golden flat-driver leg)
#include "runtime/graph_bridge.h"                  // libFromGraph (resident production leg)
#include "runtime/point_graph.h"                   // PointCookCtx, registerPointOp, PointGraph
#include "runtime/resident_eval_graph.h"           // buildEvalGraph (resident production leg)
#include "runtime/sw_mesh.h"                       // SwVertex (80B) + SwTriIndex (12B)
#include "runtime/tixl_point.h"                    // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// FindClosestPointsOnMesh cook: dispatch the brute-force kernel, one thread per INPUT point. No Points
// input (count 0) OR no Mesh (meshVtx/meshIdx null / 0 faces) → pass-through (output already holds the
// input bag's geometry is NOT auto-copied — so with no mesh we copy the input through, matching the
// .hlsl's "closestIndex<0 → leave Position unchanged" when faceCount==0). We copy inputs[0]→output.
void cookFindClosestPointsOnMesh(PointCookCtx& c) {
  if (!c.output || !c.lib || c.count == 0) return;
  const MTL::Buffer* inBag = (c.inputCount > 0) ? c.inputs[0] : nullptr;
  if (!inBag) return;  // unwired Points input -> empty bag (count is 0 anyway via firstPointsCount)

  const MTL::Buffer* verts = c.meshVtx;
  const MTL::Buffer* faces = c.meshIdx;

  // No Mesh (or 0 faces): faithful to the .hlsl (closestIndex stays -1 → Position unchanged) — pass the
  // input bag straight through so the op is an identity when its Mesh input is unwired.
  if (!verts || !faces || c.meshFaceCount == 0) {
    std::memcpy(c.output->contents(), const_cast<MTL::Buffer*>(inBag)->contents(),
                (size_t)c.count * sizeof(SwPoint));
    return;
  }

  MTL::ComputePipelineState* pso = cachedComputePSO(c.dev, c.lib, "findclosestpointsonmesh");
  if (!pso) return;

  FcpomParams P{};
  P.Count = c.count;
  uint32_t faceCount = c.meshFaceCount;

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setBuffer(const_cast<MTL::Buffer*>(inBag), 0, FCPOM_Points);
  enc->setBuffer(const_cast<MTL::Buffer*>(verts), 0, FCPOM_Vertices);
  enc->setBuffer(const_cast<MTL::Buffer*>(faces), 0, FCPOM_Indices);
  enc->setBuffer(c.output, 0, FCPOM_ResultPoints);
  enc->setBytes(&P, sizeof(P), FCPOM_Params);
  enc->setBytes(&faceCount, sizeof(faceCount), FCPOM_FaceCount);
  const uint32_t tg = 64;
  enc->dispatchThreadgroups(MTL::Size::Make(calcDispatchCount(c.count, tg), 1, 1),
                            MTL::Size::Make(tg, 1, 1));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  // PSO owned by device-global computePsoCache (released in clearTexOpCache); do NOT release here.
}

void registerFindClosestPointsOnMeshOp() {
  // countFromFirstPointsInput=true: output bag == the input Points count (one snapped point per input).
  registerPointOp("FindClosestPointsOnMesh", cookFindClosestPointsOnMesh, /*stNew=*/nullptr,
                  /*stFree=*/nullptr, /*countTransform=*/nullptr, /*countFromFirstPointsInput=*/true,
                  /*countFromMeshVtx=*/false);
}

// =============================================================================================
// Golden — hand-computed closest-point snaps on ONE triangle, three legs:
//   (1) FLAT direct-cook: build a 4-point input bag + a 1-triangle SwVertex/SwTriIndex mesh, dispatch
//       cookFindClosestPointsOnMesh directly, CPU-readback → assert each Position snapped to the
//       hand-computed nearest surface point. RED tooth: corrupt the Indices binding (face refs vertex 0
//       three times → a DEGENERATE triangle at the origin) so every point snaps to (0,0,0) instead of
//       its true nearest surface point → the closest-point claim FAILS.
//   (2) FLAT-DRIVER gather: a real flat Graph (LinePoints → FindClosestPointsOnMesh ← QuadMesh) cooked
//       through PointGraph::cook(target=op), read back the cooked bag → snapped onto the quad. RED tooth:
//       OMIT the Mesh wire → no mesh → identity pass-through (points stay on the line, NOT on the quad).
//   (3) RESIDENT (production): the same graph → DrawPoints2 → RenderTarget through libFromGraph →
//       buildEvalGraph → cookResident, read the rendered pixels → sprites at the SNAPPED positions. RED
//       tooth: OMIT the Mesh wire → identity → sprites at the un-snapped line positions (probe goes dark
//       at the snapped pixel). All teeth bite the REAL cook path.
//
// Triangle fixture (leg 1): v0=(0,0,0), v1=(1,0,0), v2=(0,1,0) in the z=0 plane, one face (0,1,2).
//   Input point A=(0.25,0.25, 1.0) → interior projection → closest=(0.25,0.25,0)   [region: invDet]
//   Input point B=(-1,-1, 0)       → vertex v0           → closest=(0, 0, 0)        [region: s<0,t<0,d>=0]
//   Input point C=( 2, 0, 0)       → vertex v1           → closest=(1, 0, 0)        [region: s+t>=det edge]
//   Input point D=( 0.5,-1, 0)     → edge v0-v1          → closest=(0.5,0,0)        [region: s>=0,t<0]
// (All four verified by stepping the closestPointOnTriangle decision tree by hand.)
// =============================================================================================

namespace {

bool nearf(float a, float b, float t = 1e-4f) { return std::fabs(a - b) < t; }

// Build the one-triangle mesh. wireIndices=false (RED tooth) makes the face (0,0,0) → a degenerate
// triangle collapsed at v0 → every closest point is the origin (the closest-point math is defeated).
MTL::Buffer* makeTriVtxBuffer(MTL::Device* dev) {
  SwVertex v[3];
  const float pos[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  for (int i = 0; i < 3; ++i) {
    v[i] = SwVertex{};
    v[i].Position = {pos[i][0], pos[i][1], pos[i][2]};
    v[i].Normal = {0, 0, 1}; v[i].Tangent = {1, 0, 0}; v[i].Bitangent = {0, 1, 0};
    v[i].Selection = 1.0f; v[i].ColorRgb = {1, 1, 1};
  }
  return dev->newBuffer(v, 3 * sizeof(SwVertex), MTL::ResourceStorageModeShared);
}
MTL::Buffer* makeTriIdxBuffer(MTL::Device* dev, bool wireIndices) {
  SwTriIndex f{};
  if (wireIndices) { f.X = 0; f.Y = 1; f.Z = 2; }     // the real triangle
  else             { f.X = 0; f.Y = 0; f.Z = 0; }     // RED tooth: degenerate (collapsed at v0)
  return dev->newBuffer(&f, sizeof(SwTriIndex), MTL::ResourceStorageModeShared);
}

constexpr float kInPos[4][3]  = {{0.25f, 0.25f, 1.0f}, {-1, -1, 0}, {2, 0, 0}, {0.5f, -1, 0}};
constexpr float kSnapPos[4][3] = {{0.25f, 0.25f, 0.0f}, {0, 0, 0}, {1, 0, 0}, {0.5f, 0, 0}};

// (1) FLAT direct-cook leg. wireIndices=false (RED tooth) collapses the triangle → snaps to origin.
bool flatDirectLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireIndices,
                   SwPoint out[4]) {
  SwPoint in[4];
  for (int i = 0; i < 4; ++i) {
    in[i] = SwPoint{};
    in[i].Position = {kInPos[i][0], kInPos[i][1], kInPos[i][2]};
    in[i].FX1 = 7.0f; in[i].FX2 = 9.0f;  // sentinels: prove the rest of the Point is carried through
    in[i].Color = {0.2f, 0.3f, 0.4f, 1.0f};
  }
  MTL::Buffer* inBag = dev->newBuffer(in, 4 * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  MTL::Buffer* outBag = dev->newBuffer(4 * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  std::memset(outBag->contents(), 0, 4 * sizeof(SwPoint));
  MTL::Buffer* vtx = makeTriVtxBuffer(dev);
  MTL::Buffer* idx = makeTriIdxBuffer(dev, wireIndices);

  const MTL::Buffer* ins[1] = {inBag};
  PointCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.count = 4;
  c.inputs = ins; c.inputCount = 1;
  c.output = outBag;
  c.meshVtx = vtx; c.meshVtxCount = 3;
  c.meshIdx = idx; c.meshFaceCount = 1;
  cookFindClosestPointsOnMesh(c);

  std::memcpy(out, outBag->contents(), 4 * sizeof(SwPoint));
  inBag->release(); outBag->release(); vtx->release(); idx->release();
  return true;
}

// (2) FLAT-DRIVER gather leg: LinePoints → FindClosestPointsOnMesh ← QuadMesh through PointGraph::cook.
// QuadMesh default = unit quad in z=0 spanning x,y∈[0,1] (2 tris). The line runs along Y at x=2, z=1
// (Center=(2,0,1), Direction=(0,1,0), Length=4, Pivot=0.5 → 4 points at (2,-2,1)…(2,2,1)), entirely OFF
// the quad. A faithful snap pulls every point ONTO the quad surface: x clamps from 2→1 (the +x edge),
// z→0. So every snapped point has x≤1 and z≈0. wireMesh=false (RED tooth) omits the Mesh wire → identity
// → the points stay at x=2, z=1 (the snap never happens).
bool flatDriverLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireMesh,
                   SwPoint out[4], uint32_t& cookedCount) {
  registerBuiltinPointOps();

  Graph g;
  Node line; line.id = 1; line.type = "LinePoints";
  line.params["Count"] = 4.0f; line.params["Length"] = 4.0f; line.params["Pivot"] = 0.5f;
  line.params["Center.x"] = 2.0f; line.params["Center.y"] = 0.0f; line.params["Center.z"] = 1.0f;
  line.params["Direction.x"] = 0.0f; line.params["Direction.y"] = 1.0f; line.params["Direction.z"] = 0.0f;
  g.nodes.push_back(line);
  Node quad; quad.id = 2; quad.type = "QuadMesh"; g.nodes.push_back(quad);
  Node fcp; fcp.id = 3; fcp.type = "FindClosestPointsOnMesh"; g.nodes.push_back(fcp);

  int lineOut = -1, quadOut = -1, fcpPtsIn = -1, fcpMeshIn = -1;
  { const NodeSpec* ls = findSpec("LinePoints");
    for (size_t i = 0; i < ls->ports.size(); ++i) if (!ls->ports[i].isInput) { lineOut = (int)i; break; }
    const NodeSpec* qs = findSpec("QuadMesh");
    for (size_t i = 0; i < qs->ports.size(); ++i) if (!qs->ports[i].isInput) { quadOut = (int)i; break; }
    const NodeSpec* fs = findSpec("FindClosestPointsOnMesh");
    for (size_t i = 0; i < fs->ports.size(); ++i) {
      if (fs->ports[i].isInput && fs->ports[i].dataType == "Points") fcpPtsIn = (int)i;
      if (fs->ports[i].isInput && fs->ports[i].dataType == "Mesh") fcpMeshIn = (int)i; } }
  g.connections.push_back({100, pinId(1, lineOut), pinId(3, fcpPtsIn)});       // LinePoints -> op
  if (wireMesh) g.connections.push_back({101, pinId(2, quadOut), pinId(3, fcpMeshIn)});  // QuadMesh -> op

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, 256, 256);
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/3);

  cookedCount = pg.debugCookedCount(3);
  for (int i = 0; i < 4; ++i) out[i] = SwPoint{};
  const MTL::Buffer* buf = pg.debugCookedBuffer(3);
  if (buf && cookedCount >= 4)
    std::memcpy(out, const_cast<MTL::Buffer*>(buf)->contents(), 4 * sizeof(SwPoint));
  return buf != nullptr;
}

// (3) RESIDENT production leg: the same graph → DrawPoints2 → RenderTarget via cookResident. Same line
// (x=2, along Y) → a faithful snap clamps x from 2→1 (onto the +x edge of the quad). DrawPoints2 projects
// ORTHOGRAPHICALLY (ignores z), so the SNAP IN X is the production tooth: faithful → sprites at x=1;
// identity (wireMesh=false RED tooth) → sprites stay at x=2.
bool residentLeg(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool wireMesh,
                 std::vector<uint8_t>& px, uint32_t& ow, uint32_t& oh) {
  registerBuiltinPointOps();
  const uint32_t W = 256, H = 256;

  Graph g;
  Node line; line.id = 1; line.type = "LinePoints";
  line.params["Count"] = 4.0f; line.params["Length"] = 4.0f; line.params["Pivot"] = 0.5f;
  line.params["Center.x"] = 2.0f; line.params["Center.y"] = 0.0f; line.params["Center.z"] = 1.0f;
  line.params["Direction.x"] = 0.0f; line.params["Direction.y"] = 1.0f; line.params["Direction.z"] = 0.0f;
  g.nodes.push_back(line);
  Node quad; quad.id = 2; quad.type = "QuadMesh"; g.nodes.push_back(quad);
  Node fcp; fcp.id = 3; fcp.type = "FindClosestPointsOnMesh"; g.nodes.push_back(fcp);
  Node draw; draw.id = 4; draw.type = "DrawPoints2";
  draw.params["Color.x"] = 1.0f; draw.params["Color.y"] = 0.0f;
  draw.params["Color.z"] = 0.0f; draw.params["Color.w"] = 1.0f;
  draw.params["Radius"] = 0.05f; draw.params["UseWForSize"] = 0.0f;
  g.nodes.push_back(draw);
  Node rt; rt.id = 5; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);

  int lineOut = -1, quadOut = -1, fcpPtsIn = -1, fcpMeshIn = -1, fcpOut = -1,
      drawPtsIn = -1, drawOut = -1, rtCmdIn = -1;
  { const NodeSpec* ls = findSpec("LinePoints");
    for (size_t i = 0; i < ls->ports.size(); ++i) if (!ls->ports[i].isInput) { lineOut = (int)i; break; }
    const NodeSpec* qs = findSpec("QuadMesh");
    for (size_t i = 0; i < qs->ports.size(); ++i) if (!qs->ports[i].isInput) { quadOut = (int)i; break; }
    const NodeSpec* fs = findSpec("FindClosestPointsOnMesh");
    for (size_t i = 0; i < fs->ports.size(); ++i) {
      if (fs->ports[i].isInput && fs->ports[i].dataType == "Points") fcpPtsIn = (int)i;
      if (fs->ports[i].isInput && fs->ports[i].dataType == "Mesh") fcpMeshIn = (int)i;
      if (!fs->ports[i].isInput) fcpOut = (int)i; }
    const NodeSpec* ds = findSpec("DrawPoints2");
    for (size_t i = 0; i < ds->ports.size(); ++i) {
      if (ds->ports[i].isInput && ds->ports[i].dataType == "Points") drawPtsIn = (int)i;
      if (!ds->ports[i].isInput) drawOut = (int)i; }
    const NodeSpec* rs = findSpec("RenderTarget");
    for (size_t i = 0; i < rs->ports.size(); ++i)
      if (rs->ports[i].isInput && rs->ports[i].dataType == "Command") { rtCmdIn = (int)i; break; } }
  g.connections.push_back({100, pinId(1, lineOut), pinId(3, fcpPtsIn)});
  if (wireMesh) g.connections.push_back({101, pinId(2, quadOut), pinId(3, fcpMeshIn)});
  g.connections.push_back({102, pinId(3, fcpOut), pinId(4, drawPtsIn)});
  g.connections.push_back({103, pinId(4, drawOut), pinId(5, rtCmdIn)});

  SymbolLibrary slib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  PointGraph pg(dev, lib, q, W, H);
  pg.cookResident(rg, ctx, nullptr, "5");

  MTL::Texture* tex = pg.target();
  ow = tex ? (uint32_t)tex->width() : 0;
  oh = tex ? (uint32_t)tex->height() : 0;
  if (!tex || ow == 0 || oh == 0) return false;
  px.assign((size_t)ow * oh * 4, 0);
  tex->getBytes(px.data(), ow * 4, MTL::Region::Make2D(0, 0, ow, oh), 0);
  return true;
}

}  // namespace

int runFindClosestPointsOnMeshSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  clearTexOpCache();  // P1: drop stale PSO built on this self-built device before teardown
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-findclosestpointsonmesh] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // ── (1) FLAT direct-cook: each Position snapped to the hand-computed nearest surface point. ─────
  SwPoint d[4];
  flatDirectLeg(dev, q, lib, /*wireIndices=*/!injectBug, d);
  bool flatPass = true;
  for (int i = 0; i < 4; ++i)
    flatPass = flatPass && nearf(d[i].Position.x, kSnapPos[i][0]) &&
               nearf(d[i].Position.y, kSnapPos[i][1]) && nearf(d[i].Position.z, kSnapPos[i][2]) &&
               nearf(d[i].FX1, 7.0f) && nearf(d[i].FX2, 9.0f) &&  // rest of the Point carried through
               nearf(d[i].Color.x, 0.2f);

  // ── (2) FLAT-DRIVER gather: LinePoints(x=2,z=1) → op ← QuadMesh → snapped onto the quad (x≤1, z≈0). ─
  SwPoint dr[4]; uint32_t drCount = 0;
  flatDriverLeg(dev, q, lib, /*wireMesh=*/!injectBug, dr, drCount);
  bool drvPass = (drCount == 4);
  for (int i = 0; i < 4 && drvPass; ++i)
    drvPass = drvPass && (dr[i].Position.x <= 1.0f + 1e-3f) &&   // x clamped from 2 onto the quad's +x edge
              nearf(dr[i].Position.z, 0.0f, 1e-3f);              // snapped onto the quad's z=0 plane

  // ── (3) RESIDENT production: sprites at the SNAPPED x (the line's x=2 clamped onto the quad's x=1). ─
  std::vector<uint8_t> px; uint32_t ow = 0, oh = 0;
  bool gotRes = residentLeg(dev, q, lib, /*wireMesh=*/!injectBug, px, ow, oh);
  // DrawPoints2 projects ORTHOGRAPHICALLY: NDC = Position.xy / viewExtent (viewExtent=3.5,
  // draw_points2.metal); z is ignored. The line point that started at (2, 0.667, 1) snaps to (1, 0.667, 0)
  // — INSIDE the quad in y. The production tooth is the SNAP IN X: faithful → the sprite is at x=1 (lit at
  // the x=1 pixel, DARK at the x=2 pixel); the Mesh-wire RED tooth makes the op an identity so the point
  // stays at x=2 (LIT at x=2, dark at x=1). We probe (1, 0.667) vs (2, 0.667).
  const float viewExtent = 3.5f;
  auto litRedAt = [&](float wx, float wy) -> bool {
    float ndcX = wx / viewExtent, ndcY = wy / viewExtent;
    int xpx = (int)((ndcX * 0.5f + 0.5f) * (float)(ow - 1) + 0.5f);
    int ypx = (int)((1.0f - (ndcY * 0.5f + 0.5f)) * (float)(oh - 1) + 0.5f);
    if (xpx < 0 || ypx < 0 || xpx >= (int)ow || ypx >= (int)oh) return false;
    size_t k = ((size_t)ypx * ow + xpx) * 4;
    return px[k] > 120 && px[k + 1] < 80 && px[k + 2] < 80;
  };
  const float kProbeY = 0.667f;  // the snapped y of the (2, 0.667, 1) line point (inside [0,1], unchanged)
  bool snappedLit = gotRes && litRedAt(1.0f, kProbeY);      // faithful snap puts the sprite at x=1
  bool unsnappedDark = gotRes && !litRedAt(2.0f, kProbeY);  // the original x=2 position is empty when snapped
  bool resPass = gotRes && snappedLit && unsnappedDark;

  bool pass = flatPass && drvPass && resPass;
  std::printf("[selftest-findclosestpointsonmesh] FLAT-DIRECT: "
              "A=(%.3f,%.3f,%.3f) B=(%.3f,%.3f,%.3f) C=(%.3f,%.3f,%.3f) D=(%.3f,%.3f,%.3f) pass=%d | "
              "FLAT-DRIVER: count=%u x0=%.4f z0=%.4f pass=%d | RESIDENT: %ux%u snapLit=%d unsnapDark=%d "
              "pass=%d | injectBug=%d -> %s\n",
              d[0].Position.x, d[0].Position.y, d[0].Position.z,
              d[1].Position.x, d[1].Position.y, d[1].Position.z,
              d[2].Position.x, d[2].Position.y, d[2].Position.z,
              d[3].Position.x, d[3].Position.y, d[3].Position.z, flatPass ? 1 : 0,
              drCount, dr[0].Position.x, dr[0].Position.z, drvPass ? 1 : 0, ow, oh,
              snappedLit ? 1 : 0, unsnappedDark ? 1 : 0, resPass ? 1 : 0,
              injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
