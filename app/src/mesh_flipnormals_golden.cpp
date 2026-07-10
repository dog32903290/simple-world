// mesh_flipnormals_golden — --selftest-mesh-flipnormals. Golden for the FlipNormals mesh→mesh
// modify CONSUMER (split from mesh_modify_golden.cpp per the ≤400-line rule when the winding-
// reversal parity fix forked its production expectation from the other modify ops).
//
// TiXL authority: FlipNormals.t3 dispatches TWO ComputeShader children (both cited by the .t3's
// Source values):
//   1. mesh-FlipNormals.hlsl:21-27 — Normal = -Normal, Tangent = -Tangent, everything else copied
//      (Bitangent NOT flipped).
//   2. mesh-ReverseFaceVertexIndexOrder.hlsl:20 — `ResultIndices[i.x] = SourceIndices[i.x].zyx`
//      → every face's winding order reverses (counts unchanged).
//
// TWO legs:
//   FLAT (CPU-readback): QuadMesh → FlipNormals → debugCookedMesh; assert the negated
//     Normal/Tangent, untouched Bitangent/Position, AND the reversed index triples:
//     QuadMesh base f0=(0,2,1) f1=(2,3,1) (QuadMesh.cs:106-107) → .zyx → f0=(1,2,0) f1=(1,3,2).
//
//   ★ PRODUCTION PIXEL (R-2 rule): QuadMesh → FlipNormals → DrawMeshUnlit → RenderTarget through
//     the CANONICAL production path (libFromGraph → buildEvalGraph → cookResident). The production
//     mesh draw is CCW-front + CullBack (point_ops_rendertarget.cpp DrawKind::Mesh, TiXL
//     FrontCounterClockwise=true + Cull Back), so the reversed winding turns the front-facing quad
//     back-facing → the whole quad is CULLED → the interior probe must read BACKGROUND. This
//     distinguishes flipped (culled) from not-flipped (visible): a regression to verbatim index
//     copy lights the probe RED and fails.
//
// Teeth (injectBug corrupts the REAL cook in mesh_ops_flipnormals.cpp, not the expectations):
//   dst[0].Normal = -999 → the flat Normal assertion fires; face 0 written VERBATIM (the pre-fix
//   parity bug itself) → flat iOk fires. The -bug polarity is carried by the FLAT leg: in the
//   production leg the upstream QuadMesh tooth corrupts the source face to a degenerate (99,99,99)
//   under the same shared meshInjectBug() flag, masking the visibility flip. The production culled
//   assertion is not vacuous — it was watched RED against the real pre-fix verbatim-copy cook
//   (probe read (255,0,0)) before the fix landed.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"           // EvaluationContext
#include "runtime/field_camera.h"           // defaultLayerCameraForward / objectToClipSpace (host project)
#include "runtime/graph.h"                  // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"           // libFromGraph
#include "runtime/mesh_op_registry.h"       // meshInjectBug
#include "runtime/point_graph.h"            // PointGraph::cook/cookResident + debugCookedMesh + registerBuiltinPointOps
#include "runtime/resident_eval_graph.h"    // buildEvalGraph (production path)
#include "runtime/sw_mesh.h"                // SwVertex / SwTriIndex

#include "mesh_modify_golden_cases.h"       // makeQuad/meshPins (shared quad fixture)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

using namespace mesh_golden;

bool nearf(float a, float b, float t = 1e-4f) { return std::fabs(a - b) < t; }
bool n3(const SW_MESH_PACKED3& v, float x, float y, float z) {
  return nearf(v.x, x) && nearf(v.y, y) && nearf(v.z, z);
}

// ── production-pixel leg: QuadMesh(Center -0.5,-0.5,0) → FlipNormals → DrawMeshUnlit →
//    RenderTarget via libFromGraph → buildEvalGraph → cookResident. Same rig as mesh_modify_golden's
//    productionLeg but with the CULLED expectation: the reversed winding must make the quad
//    invisible, so the lower-left f0 probe reads BACKGROUND (and the far corner stays background).
//    (-bug polarity lives in the flat leg — see the file-top Teeth note.) ──
int productionLegCulled(const char* tag, bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;
  const float aspect = 1.0f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib = dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) { std::printf("[%s] FAIL: no metallib\n", tag); q->release(); dev->release(); pool->release(); return 1; }
  registerBuiltinPointOps();

  PointGraph pg(dev, lib, q, W, H);
  Graph g;
  g.nodes.push_back(makeQuad(1, -0.5f, -0.5f, 0.0f));   // footprint NDC [-0.5,0.5]²
  Node op; op.id = 2; op.type = "FlipNormals";
  g.nodes.push_back(op);
  Node draw; draw.id = 3; draw.type = "DrawMeshUnlit";
  draw.params["Color.x"] = 1.0f; draw.params["Color.y"] = 0.0f;
  draw.params["Color.z"] = 0.0f; draw.params["Color.w"] = 1.0f;  // RED
  g.nodes.push_back(draw);
  Node rt; rt.id = 4; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);

  int quadOut, dummy, opOut, opMeshIn, drawOut, drawMeshIn;
  meshPins("QuadMesh", quadOut, dummy);
  meshPins("FlipNormals", opOut, opMeshIn);
  meshPins("DrawMeshUnlit", drawOut, drawMeshIn);
  int rtCmdIn = 0;
  { const NodeSpec* rs = findSpec("RenderTarget");
    for (size_t i = 0; i < rs->ports.size(); ++i)
      if (rs->ports[i].isInput && rs->ports[i].dataType == "Command") { rtCmdIn = (int)i; break; } }
  g.connections.push_back({100, pinId(1, quadOut), pinId(2, opMeshIn)});
  g.connections.push_back({101, pinId(2, opOut), pinId(3, drawMeshIn)});
  g.connections.push_back({102, pinId(3, drawOut), pinId(4, rtCmdIn)});

  SymbolLibrary slib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  meshInjectBug() = injectBug;
  pg.cookResident(rg, ctx, nullptr, /*RenderTarget path*/ "4");
  meshInjectBug() = false;

  MTL::Texture* tex = pg.target();
  bool sized = tex && (uint32_t)tex->width() == W && (uint32_t)tex->height() == H;
  auto project = [&](float wx, float wy, float wz, float out[3]) {
    LayerCameraForward cam = defaultLayerCameraForward(aspect);
    Mat4 o2c = objectToClipSpace(mat4Identity(), cam.worldToCamera, cam.cameraToClipSpace);
    mat4TransformPointDivW(o2c, wx, wy, wz, out);
  };
  auto ndcXToPx = [&](float n) { return (int)((n * 0.5f + 0.5f) * (float)(W - 1) + 0.5f); };
  auto ndcYToPx = [&](float n) { return (int)((1.0f - (n * 0.5f + 0.5f)) * (float)(H - 1) + 0.5f); };

  int ir = 0, ig = 0, ib = 0, cr = 0, cg = 0, cb = 0;
  bool interiorClear = false, cornerClear = false;
  if (sized) {
    std::vector<uint8_t> px((size_t)W * H * 4, 0);
    tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
    auto rd = [&](int x, int y, int& r, int& gg, int& b) {
      size_t i = ((size_t)y * W + x) * 4; r = px[i]; gg = px[i + 1]; b = px[i + 2]; };
    float ie[3]; project(-0.3f, -0.3f, 0.0f, ie);  // lower-left interior, covered ONLY by f0 (the v0 tri)
    float fc[3]; project(0.9f, 0.9f, 0.0f, fc);     // far corner outside the quad
    rd(ndcXToPx(ie[0]), ndcYToPx(ie[1]), ir, ig, ib);
    rd(ndcXToPx(fc[0]), ndcYToPx(fc[1]), cr, cg, cb);
    interiorClear = ir < 30 && ig < 30 && ib < 30;  // CULLED quad → probe = background
    cornerClear = cr < 30 && cg < 30 && cb < 30;
  }
  bool pass = sized && interiorClear && cornerClear;
  std::printf("[%s] ★cookResident pixel: f0probe=(%d,%d,%d) corner=(%d,%d,%d) interiorClear=%d "
              "cornerClear=%d -> %s\n", tag, ir, ig, ib, cr, cg, cb, interiorClear ? 1 : 0,
              cornerClear ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace

// ============================== FlipNormals ==============================
int runMeshFlipNormalsGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, nullptr, q, 64, 64);

  Graph g;
  g.nodes.push_back(makeQuad(1, 0, 0, 0));
  Node op; op.id = 2; op.type = "FlipNormals"; g.nodes.push_back(op);
  int quadOut, dummy, opOut, opMeshIn;
  meshPins("QuadMesh", quadOut, dummy);
  meshPins("FlipNormals", opOut, opMeshIn);
  g.connections.push_back({100, pinId(1, quadOut), pinId(2, opMeshIn)});

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  meshInjectBug() = injectBug;
  pg.cook(g, ctx, nullptr, 2);
  pg.cook(g, ctx, nullptr, 2);  // buffer reuse
  meshInjectBug() = false;

  const MTL::Buffer* vb = nullptr; const MTL::Buffer* ib = nullptr; uint32_t vc = 0, fc = 0;
  bool got = pg.debugCookedMesh(2, vb, vc, ib, fc);
  bool flat = got && vc == 4 && fc == 2;
  if (flat) {
    const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
    // Normal -(0,0,1)=(0,0,-1); Tangent -(1,0,0)=(-1,0,0); Bitangent UNCHANGED (0,1,0); Position
    // kept (mesh-FlipNormals.hlsl:21-27).
    bool nOk = n3(v[0].Normal, 0, 0, -1) && n3(v[3].Normal, 0, 0, -1);
    bool tOk = n3(v[0].Tangent, -1, 0, 0) && n3(v[3].Tangent, -1, 0, 0);
    bool bOk = n3(v[0].Bitangent, 0, 1, 0) && n3(v[3].Bitangent, 0, 1, 0);  // NOT flipped
    bool pOk = n3(v[3].Position, 1, 1, 0);  // position untouched (faithful)
    // Winding REVERSED (mesh-ReverseFaceVertexIndexOrder.hlsl:20, `.zyx`): QuadMesh base
    // f0=(0,2,1) f1=(2,3,1) (QuadMesh.cs:106-107) → f0=(1,2,0) f1=(1,3,2).
    const SwTriIndex* ix = (const SwTriIndex*)const_cast<MTL::Buffer*>(ib)->contents();
    bool iOk = ix[0].X == 1 && ix[0].Y == 2 && ix[0].Z == 0 &&
               ix[1].X == 1 && ix[1].Y == 3 && ix[1].Z == 2;
    flat = nOk && tOk && bOk && pOk && iOk;
    std::printf("[selftest-mesh-flipnormals] flat: N0=(%.1f,%.1f,%.1f) T0=(%.1f,%.1f,%.1f) "
                "B0=(%.1f,%.1f,%.1f) f0=(%d,%d,%d) f1=(%d,%d,%d) nOk=%d tOk=%d bOk=%d pOk=%d iOk=%d\n",
                v[0].Normal.x, v[0].Normal.y, v[0].Normal.z, v[0].Tangent.x, v[0].Tangent.y,
                v[0].Tangent.z, v[0].Bitangent.x, v[0].Bitangent.y, v[0].Bitangent.z,
                ix[0].X, ix[0].Y, ix[0].Z, ix[1].X, ix[1].Y, ix[1].Z, nOk, tOk, bOk, pOk, iOk);
  } else {
    std::printf("[selftest-mesh-flipnormals] flat FAIL: no cooked mesh (got=%d vc=%u fc=%u)\n", got, vc, fc);
  }
  q->release(); dev->release(); pool->release();

  int prod = productionLegCulled("selftest-mesh-flipnormals-prod", injectBug);
  bool ok = flat && prod == 0;
  std::printf("[selftest-mesh-flipnormals] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
