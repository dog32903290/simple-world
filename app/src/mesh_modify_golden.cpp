// mesh_modify_golden — --selftest-mesh-recomputenormals / --selftest-mesh-transformuvs.
// Goldens for two pure mesh→mesh modify CONSUMERS (Phase C mesh-input leaves). (FlipNormals lived
// here too until its winding-reversal parity fix forked its production expectation — it now has its
// own TU, mesh_flipnormals_golden.cpp, per the ≤400-line rule.) Each op gets TWO legs:
//
//   FLAT (CPU-readback): QuadMesh → op → debugCookedMesh; assert the exact transformed vertex
//     attributes vs the hand-derived TiXL shader math (mesh-RecomputeNormals.hlsl `computeNormal` /
//     mesh-TransformUVs.hlsl), topology unchanged. injectBug corrupts the op's primary
//     output field (Normal+Selection / TexCoord) in the REAL cook → the field assertion fires.
//
//   ★ PRODUCTION PIXEL (R-2 rule): QuadMesh → op → DrawMeshUnlit → RenderTarget built through the
//     CANONICAL production path (libFromGraph → buildEvalGraph → cookResident), then read pg.target()
//     pixels and assert the quad is lit on screen → proves the op runs on the production resident mesh
//     gather (not just flat). injectBug ALSO flies v0.Position off (DrawMeshUnlit is UNLIT so it ignores
//     Normal/UV — only a position move shifts the quad), collapsing f0 → the lower-left probe reads
//     background → RED. Teeth on the actual cook path, not by inverting a faithful pass.
//
// QuadMesh defaults + every hand/python-derived case oracle live in mesh_modify_golden_cases.h
// (shared with mesh_modify2_golden.cpp), split out per ARCHITECTURE.md rule 4.
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

#include "mesh_modify_golden_cases.h"       // makeQuad/meshPins + case derivations/constants

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
bool uvEq(const SW_MESH_FLOAT2& v, float x, float y) { return nearf(v.x, x) && nearf(v.y, y); }

// ── shared production-pixel leg: QuadMesh(Center -0.5,-0.5,0) → op → DrawMeshUnlit → RenderTarget,
//    through libFromGraph → buildEvalGraph → cookResident; probe the lower-left f0 triangle. ──
int productionLeg(const char* opType, const char* tag, bool injectBug) {
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
  Node op; op.id = 2; op.type = opType;                  // op defaults (no transform of position)
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
  meshPins(opType, opOut, opMeshIn);
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
  bool interiorRed = false, cornerClear = false;
  if (sized) {
    std::vector<uint8_t> px((size_t)W * H * 4, 0);
    tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
    auto rd = [&](int x, int y, int& r, int& gg, int& b) {
      size_t i = ((size_t)y * W + x) * 4; r = px[i]; gg = px[i + 1]; b = px[i + 2]; };
    float ie[3]; project(-0.3f, -0.3f, 0.0f, ie);  // lower-left interior, covered ONLY by f0 (the v0 tri)
    float fc[3]; project(0.9f, 0.9f, 0.0f, fc);     // far corner outside the quad
    rd(ndcXToPx(ie[0]), ndcYToPx(ie[1]), ir, ig, ib);
    rd(ndcXToPx(fc[0]), ndcYToPx(fc[1]), cr, cg, cb);
    interiorRed = ir > 250 && ig < 5 && ib < 5;
    cornerClear = cr < 30 && cg < 30 && cb < 30;
  }
  bool pass = sized && interiorRed && cornerClear;
  std::printf("[%s] ★cookResident pixel: f0probe=(%d,%d,%d) corner=(%d,%d,%d) interiorRed=%d "
              "cornerClear=%d -> %s\n", tag, ir, ig, ib, cr, cg, cb, interiorRed ? 1 : 0,
              cornerClear ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;  // injectBug flies v0 off → f0 collapses → probe not RED → return 1
}

}  // namespace

// ============================== RecomputeNormals ==============================
int runMeshRecomputeNormalsGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, nullptr, q, 64, 64);

  Graph g;
  g.nodes.push_back(makeQuad(1, 0, 0, 0));
  Node op; op.id = 2; op.type = "RecomputeNormals"; g.nodes.push_back(op);
  int quadOut, dummy, opOut, opMeshIn;
  meshPins("QuadMesh", quadOut, dummy);
  meshPins("RecomputeNormals", opOut, opMeshIn);
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
    // Oracle: cases header §"RecomputeNormals, FLAT quad" — N/T/B identity, Selection=faceCount(1,2,2,1).
    bool nOk = n3(v[0].Normal, 0, 0, 1) && n3(v[1].Normal, 0, 0, 1) && n3(v[3].Normal, 0, 0, 1);
    bool tOk = n3(v[1].Tangent, 1, 0, 0) && n3(v[1].Bitangent, 0, 1, 0);
    bool selOk = nearf(v[0].Selection, 1.0f) && nearf(v[1].Selection, 2.0f) &&
                 nearf(v[2].Selection, 2.0f) && nearf(v[3].Selection, 1.0f);
    flat = nOk && tOk && selOk;
    std::printf("[selftest-mesh-recomputenormals] flat: N1=(%.2f,%.2f,%.2f) sel=(%.0f,%.0f,%.0f,%.0f) "
                "nOk=%d tOk=%d selOk=%d\n", v[1].Normal.x, v[1].Normal.y, v[1].Normal.z,
                v[0].Selection, v[1].Selection, v[2].Selection, v[3].Selection, nOk, tOk, selOk);
  } else {
    std::printf("[selftest-mesh-recomputenormals] flat FAIL: no cooked mesh (got=%d vc=%u fc=%u)\n", got, vc, fc);
  }
  q->release(); dev->release(); pool->release();

  // ── SUB-CASE 2: NON-COPLANAR fold (P2 fix) — derivation + kFold* in cases header §"RecomputeNormals, NON-COPLANAR fold" (mesh-RecomputeNormals.hlsl @395c4c55). ──
  {
    NS::AutoreleasePool* pool2 = NS::AutoreleasePool::alloc()->init();
    MTL::Device* dev2 = MTL::CreateSystemDefaultDevice();
    MTL::CommandQueue* q2 = dev2->newCommandQueue();
    PointGraph pg2(dev2, nullptr, q2, 64, 64);

    Graph g2;
    g2.nodes.push_back(makeQuad(1, 0, 0, 0));
    Node dm; dm.id = 2; dm.type = "DeformMesh";
    dm.params["UseVertexSelection"] = 0.0f; dm.params["Spherize"] = 0.0f; dm.params["Taper"] = 0.0f;
    dm.params["Twist"] = 90.0f; dm.params["TwistAxis"] = 0.0f;  // fold about X → v3=(1,0,1)
    g2.nodes.push_back(dm);
    Node rn; rn.id = 3; rn.type = "RecomputeNormals"; g2.nodes.push_back(rn);
    int quadOut, dummy2, dmOut, dmMeshIn, rnOut, rnMeshIn;
    meshPins("QuadMesh", quadOut, dummy2);
    meshPins("DeformMesh", dmOut, dmMeshIn);
    meshPins("RecomputeNormals", rnOut, rnMeshIn);
    g2.connections.push_back({100, pinId(1, quadOut), pinId(2, dmMeshIn)});
    g2.connections.push_back({101, pinId(2, dmOut), pinId(3, rnMeshIn)});

    EvaluationContext ctx2{}; ctx2.frameIndex = 0; ctx2.time = 0.0f; ctx2.deltaTime = 1.0f / 60.0f;
    meshInjectBug() = injectBug;
    pg2.cook(g2, ctx2, nullptr, 3);
    meshInjectBug() = false;

    const MTL::Buffer* vb2 = nullptr; const MTL::Buffer* ib2 = nullptr; uint32_t vc2 = 0, fc2 = 0;
    bool got2 = pg2.debugCookedMesh(3, vb2, vc2, ib2, fc2);
    bool pass2 = got2 && vc2 == 4 && fc2 == 2;
    if (pass2) {
      const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb2)->contents();
      bool n0Ok = n3(v[0].Normal, 0, 0, 1);
      bool n1Ok = n3(v[1].Normal, kFoldN12xy, kFoldN12xy, kFoldN12z) &&
                  n3(v[2].Normal, kFoldN12xy, kFoldN12xy, kFoldN12z);
      bool n3Ok = n3(v[3].Normal, kFoldN3xy, kFoldN3xy, 0.0f);
      bool t3Ok = n3(v[3].Tangent, 0.0f, 0.0f, kFoldT3z);  // cross((0,1,0), n3), unnormalized
      bool selOk2 = nearf(v[0].Selection, 1.0f) && nearf(v[1].Selection, 2.0f) &&
                    nearf(v[2].Selection, 2.0f) && nearf(v[3].Selection, 1.0f);
      pass2 = n0Ok && n1Ok && n3Ok && t3Ok && selOk2;
      std::printf("[selftest-mesh-recomputenormals] folded: N1=(%.5f,%.5f,%.5f) N3=(%.5f,%.5f,%.5f) "
                  "T3=(%.5f,%.5f,%.5f) n0Ok=%d n1Ok=%d n3Ok=%d t3Ok=%d selOk=%d\n",
                  v[1].Normal.x, v[1].Normal.y, v[1].Normal.z, v[3].Normal.x, v[3].Normal.y,
                  v[3].Normal.z, v[3].Tangent.x, v[3].Tangent.y, v[3].Tangent.z,
                  n0Ok, n1Ok, n3Ok, t3Ok, selOk2);
    } else {
      std::printf("[selftest-mesh-recomputenormals] folded FAIL: cook (got=%d vc=%u fc=%u)\n",
                  got2, vc2, fc2);
    }
    q2->release(); dev2->release(); pool2->release();
    flat = flat && pass2;
  }

  int prod = productionLeg("RecomputeNormals", "selftest-mesh-recomputenormals-prod", injectBug);
  bool ok = flat && prod == 0;
  std::printf("[selftest-mesh-recomputenormals] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ============================== TransformMeshUVs ==============================
int runMeshTransformUvsGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();

  bool flat = true;

  // ── SUB-CASE 1: pure UV translation (0.1,0.2) — cases header §"TransformMeshUVs sub-case 1". ──
  {
    PointGraph pg(dev, nullptr, q, 64, 64);
    Graph g;
    g.nodes.push_back(makeQuad(1, 0, 0, 0));
    Node op; op.id = 2; op.type = "TransformMeshUVs";
    op.params["Translate.x"] = 0.1f; op.params["Translate.y"] = 0.2f;
    g.nodes.push_back(op);
    int quadOut, dummy, opOut, opMeshIn;
    meshPins("QuadMesh", quadOut, dummy);
    meshPins("TransformMeshUVs", opOut, opMeshIn);
    g.connections.push_back({100, pinId(1, quadOut), pinId(2, opMeshIn)});

    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    meshInjectBug() = injectBug;
    pg.cook(g, ctx, nullptr, 2);
    pg.cook(g, ctx, nullptr, 2);
    meshInjectBug() = false;

    const MTL::Buffer* vb = nullptr; const MTL::Buffer* ib = nullptr; uint32_t vc = 0, fc = 0;
    bool got = pg.debugCookedMesh(2, vb, vc, ib, fc);
    bool pass = got && vc == 4;
    if (pass) {
      const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
      // TexCoord += (0.1,0.2): v0 (0,0)→(0.1,0.2); v3 (1,1)→(1.1,1.2). Position untouched.
      bool uOk = uvEq(v[0].Texcoord, 0.1f, 0.2f) && uvEq(v[3].Texcoord, 1.1f, 1.2f);
      bool pOk = n3(v[3].Position, 1, 1, 0);
      pass = uOk && pOk;
      std::printf("[selftest-mesh-transformuvs] translate(.1,.2): uv0=(%.2f,%.2f) uv3=(%.2f,%.2f) "
                  "uOk=%d pOk=%d\n", v[0].Texcoord.x, v[0].Texcoord.y, v[3].Texcoord.x, v[3].Texcoord.y,
                  uOk, pOk);
    } else {
      std::printf("[selftest-mesh-transformuvs] translate FAIL: no cooked mesh (got=%d vc=%u)\n", got, vc);
    }
    flat = flat && pass;
  }

  // ── SUB-CASE 2: pure UV scale-about-pivot (Stretch 2, Pivot 0.5) — cases header §"TransformMeshUVs sub-case 2": uv' = (uv-0.5)*2 + 0.5. ──
  {
    PointGraph pg2(dev, nullptr, q, 64, 64);
    Graph g;
    g.nodes.push_back(makeQuad(1, 0, 0, 0));
    Node op; op.id = 2; op.type = "TransformMeshUVs";
    op.params["Stretch.x"] = 2.0f; op.params["Stretch.y"] = 2.0f; op.params["Stretch.z"] = 1.0f;
    g.nodes.push_back(op);
    int quadOut, dummy, opOut, opMeshIn;
    meshPins("QuadMesh", quadOut, dummy);
    meshPins("TransformMeshUVs", opOut, opMeshIn);
    g.connections.push_back({100, pinId(1, quadOut), pinId(2, opMeshIn)});

    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    meshInjectBug() = injectBug;
    pg2.cook(g, ctx, nullptr, 2);
    meshInjectBug() = false;

    const MTL::Buffer* vb = nullptr; const MTL::Buffer* ib = nullptr; uint32_t vc = 0, fc = 0;
    bool got = pg2.debugCookedMesh(2, vb, vc, ib, fc);
    bool pass = got && vc == 4;
    if (pass) {
      const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
      bool uOk = uvEq(v[0].Texcoord, -0.5f, -0.5f) && uvEq(v[3].Texcoord, 1.5f, 1.5f);
      pass = uOk;
      std::printf("[selftest-mesh-transformuvs] scale-about-pivot(2): uv0=(%.2f,%.2f) uv3=(%.2f,%.2f) "
                  "uOk=%d\n", v[0].Texcoord.x, v[0].Texcoord.y, v[3].Texcoord.x, v[3].Texcoord.y, uOk);
    }
    flat = flat && pass;
  }

  // ── SUB-CASE 3: pure UV ROTATION (P2 fix) — oracle + kRotZ30Uv* in cases header §"TransformMeshUVs sub-case 3" (TransformMatrix.cs @395c4c55, RotZ(30°) about pivot). ──
  {
    PointGraph pg3(dev, nullptr, q, 64, 64);
    Graph g;
    g.nodes.push_back(makeQuad(1, 0, 0, 0));
    Node op; op.id = 2; op.type = "TransformMeshUVs";
    op.params["Rotate.x"] = 0.0f; op.params["Rotate.y"] = 0.0f; op.params["Rotate.z"] = 30.0f;
    g.nodes.push_back(op);
    int quadOut, dummy, opOut, opMeshIn;
    meshPins("QuadMesh", quadOut, dummy);
    meshPins("TransformMeshUVs", opOut, opMeshIn);
    g.connections.push_back({100, pinId(1, quadOut), pinId(2, opMeshIn)});

    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    meshInjectBug() = injectBug;
    pg3.cook(g, ctx, nullptr, 2);
    meshInjectBug() = false;

    const MTL::Buffer* vb = nullptr; const MTL::Buffer* ib = nullptr; uint32_t vc = 0, fc = 0;
    bool got = pg3.debugCookedMesh(2, vb, vc, ib, fc);
    bool pass = got && vc == 4;
    if (pass) {
      const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
      bool uOk = uvEq(v[0].Texcoord, kRotZ30Uv0x, kRotZ30Uv0y) &&
                 uvEq(v[3].Texcoord, kRotZ30Uv3x, kRotZ30Uv3y);
      bool pOk = n3(v[3].Position, 1, 1, 0);  // position untouched
      pass = uOk && pOk;
      std::printf("[selftest-mesh-transformuvs] rotateZ(30): uv0=(%.5f,%.5f) uv3=(%.5f,%.5f) "
                  "uOk=%d pOk=%d\n", v[0].Texcoord.x, v[0].Texcoord.y, v[3].Texcoord.x,
                  v[3].Texcoord.y, uOk, pOk);
    } else {
      std::printf("[selftest-mesh-transformuvs] rotate FAIL: no cooked mesh (got=%d vc=%u)\n", got, vc);
    }
    flat = flat && pass;
  }

  q->release(); dev->release(); pool->release();

  int prod = productionLeg("TransformMeshUVs", "selftest-mesh-transformuvs-prod", injectBug);
  bool ok = flat && prod == 0;
  std::printf("[selftest-mesh-transformuvs] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
