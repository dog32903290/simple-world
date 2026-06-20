// mesh_modify_golden — --selftest-mesh-flipnormals / --selftest-mesh-recomputenormals /
// --selftest-mesh-transformuvs. Goldens for the three pure mesh→mesh modify CONSUMERS (Phase C mesh-
// input leaves). Each op gets TWO legs:
//
//   FLAT (CPU-readback): QuadMesh → op → debugCookedMesh; assert the exact transformed vertex
//     attributes vs the hand-derived TiXL shader math (mesh-FlipNormals.hlsl / mesh-RecomputeNormals.hlsl
//     `computeNormal` / mesh-TransformUVs.hlsl), topology unchanged. injectBug corrupts the op's primary
//     output field (Normal / Normal+Selection / TexCoord) in the REAL cook → the field assertion fires.
//
//   ★ PRODUCTION PIXEL (R-2 rule): QuadMesh → op → DrawMeshUnlit → RenderTarget built through the
//     CANONICAL production path (libFromGraph → buildEvalGraph → cookResident), then read pg.target()
//     pixels and assert the quad is lit on screen → proves the op runs on the production resident mesh
//     gather (not just flat). injectBug ALSO flies v0.Position off (DrawMeshUnlit is UNLIT so it ignores
//     Normal/UV — only a position move shifts the quad), collapsing f0 → the lower-left probe reads
//     background → RED. Teeth on the actual cook path, not by inverting a faithful pass.
//
// QuadMesh defaults (Segments=(1,1), Scale=1, Pivot=0.5): verts v0=(0,0,0) v1=(0,1,0) v2=(1,0,0)
// v3=(1,1,0); faces f0=(0,2,1) f1=(2,3,1). Attributes (QuadMesh.cs/quadCook): Normal=(0,0,1),
// Tangent=(1,0,0), Bitangent=(0,1,0), Selection=1, TexCoord v0=(0,0) v1=(0,1) v2=(1,0) v3=(1,1).
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

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

bool nearf(float a, float b, float t = 1e-4f) { return std::fabs(a - b) < t; }
bool n3(const SW_MESH_PACKED3& v, float x, float y, float z) {
  return nearf(v.x, x) && nearf(v.y, y) && nearf(v.z, z);
}
bool uvEq(const SW_MESH_FLOAT2& v, float x, float y) { return nearf(v.x, x) && nearf(v.y, y); }

Node makeQuad(int id, float cx, float cy, float cz) {
  Node m; m.id = id; m.type = "QuadMesh";
  m.params["Segments.x"] = 1.0f; m.params["Segments.y"] = 1.0f; m.params["Scale"] = 1.0f;
  m.params["Stretch.x"] = 1.0f; m.params["Stretch.y"] = 1.0f;
  m.params["Pivot.x"] = 0.5f; m.params["Pivot.y"] = 0.5f;
  m.params["Center.x"] = cx; m.params["Center.y"] = cy; m.params["Center.z"] = cz;
  return m;
}

// Find a node spec's output pin index + its first Mesh-typed input pin index.
void meshPins(const char* type, int& outPin, int& meshInPin) {
  outPin = -1; meshInPin = 0;
  const NodeSpec* s = findSpec(type);
  for (size_t i = 0; i < s->ports.size(); ++i) {
    if (!s->ports[i].isInput && s->ports[i].dataType == "Mesh") outPin = (int)i;
    if (s->ports[i].isInput && s->ports[i].dataType == "Mesh" && meshInPin == 0) meshInPin = (int)i;
  }
  // outPin may also be a non-Mesh "Data"/"Result"; re-scan for the single output if not found above.
  if (outPin < 0) for (size_t i = 0; i < s->ports.size(); ++i) if (!s->ports[i].isInput) { outPin = (int)i; break; }
}

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
    // Normal -(0,0,1)=(0,0,-1); Tangent -(1,0,0)=(-1,0,0); Bitangent UNCHANGED (0,1,0); Position kept.
    bool nOk = n3(v[0].Normal, 0, 0, -1) && n3(v[3].Normal, 0, 0, -1);
    bool tOk = n3(v[0].Tangent, -1, 0, 0) && n3(v[3].Tangent, -1, 0, 0);
    bool bOk = n3(v[0].Bitangent, 0, 1, 0) && n3(v[3].Bitangent, 0, 1, 0);  // NOT flipped
    bool pOk = n3(v[3].Position, 1, 1, 0);  // position untouched (faithful; injectBug flies v0 only)
    flat = nOk && tOk && bOk && pOk;
    std::printf("[selftest-mesh-flipnormals] flat: N0=(%.1f,%.1f,%.1f) T0=(%.1f,%.1f,%.1f) "
                "B0=(%.1f,%.1f,%.1f) nOk=%d tOk=%d bOk=%d pOk=%d\n",
                v[0].Normal.x, v[0].Normal.y, v[0].Normal.z, v[0].Tangent.x, v[0].Tangent.y,
                v[0].Tangent.z, v[0].Bitangent.x, v[0].Bitangent.y, v[0].Bitangent.z, nOk, tOk, bOk, pOk);
  } else {
    std::printf("[selftest-mesh-flipnormals] flat FAIL: no cooked mesh (got=%d vc=%u fc=%u)\n", got, vc, fc);
  }
  q->release(); dev->release(); pool->release();

  int prod = productionLeg("FlipNormals", "selftest-mesh-flipnormals-prod", injectBug);
  bool ok = flat && prod == 0;
  std::printf("[selftest-mesh-flipnormals] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

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
    // Flat z=0 quad: every face N=(0,0,1) → normalSum∝(0,0,1) → newNormal=(0,0,1). newTangent=
    // cross(bitangent(0,1,0), N(0,0,1))=(1,0,0); newBitangent=cross(N,(1,0,0))=(0,1,0). Selection=faceCount:
    // v0 in f0 only=1; v1 in f0,f1=2; v2 in f0,f1=2; v3 in f1 only=1 (THE recompute-only discriminator).
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

  // ── SUB-CASE 1: pure UV translation (0.1,0.2). Pivot 0.5, Stretch 1, Rot 0 → M = T(0.1,0.2,0).
  //    (Pivot cancels for the identity scale/rotation block → pure translation regardless of pivot.) ──
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

  // ── SUB-CASE 2: pure UV scale-about-pivot (Stretch 2, Pivot 0.5). M scales UVs about (0.5,0.5):
  //    uv' = (uv - 0.5)*2 + 0.5. v0 (0,0)→(-0.5,-0.5); v3 (1,1)→(1.5,1.5); the (0.5,0.5) center is fixed. ──
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

  q->release(); dev->release(); pool->release();

  int prod = productionLeg("TransformMeshUVs", "selftest-mesh-transformuvs-prod", injectBug);
  bool ok = flat && prod == 0;
  std::printf("[selftest-mesh-transformuvs] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
