// mesh_input_golden — --selftest-mesh-transform / --selftest-mesh-combine. The mesh-input seam goldens
// (the 4th cook flow gaining INPUTS: a mesh op consuming upstream meshes). Proves the seam two ways +
// the ★ R-2 PRODUCTION leg:
//
//   TransformMesh (flat CPU-readback): QuadMesh → TransformMesh → debugCookedMesh; assert the
//     transformed vertex positions vs the hand-derived TiXL matrix (TransformMatrix.cs +
//     mesh-TransformVertices.hlsl), topology unchanged. Two sub-cases: pure translation, pure scale.
//   CombineMeshes (flat CPU-readback): two QuadMesh → CombineMeshes (MultiInput) → debugCookedMesh;
//     assert verts concatenated + the second mesh's face indices REBASED by +4 (vertex offset).
//   ★ PRODUCTION PIXEL (R-2): QuadMesh → TransformMesh → DrawMeshUnlit → RenderTarget built through
//     the CANONICAL production path (libFromGraph → buildEvalGraph → cookResident), then read
//     pg.target() pixels and assert the TRANSFORMED quad is lit on screen. This is the leg that proves
//     the mesh gather LIVES on the production resident path AND nails the pre-existing DrawMeshUnlit
//     production black-hole (resident cookCommand had no Mesh branch → meshVtx null → black). injectBug
//     corrupts the FIRST transformed vertex in the REAL cook → the quad's footprint moves → the probe
//     pixel reads background → RED. Teeth on the actual cook path, not by flipping the expected value.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"           // EvaluationContext
#include "runtime/field_camera.h"           // defaultLayerCameraForward / objectToClipSpace (host project)
#include "runtime/graph.h"                  // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"           // libFromGraph (flat Graph -> SymbolLibrary, paths == ids)
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
bool posEq(const SwVertex& v, float x, float y, float z) {
  return nearf(v.Position.x, x) && nearf(v.Position.y, y) && nearf(v.Position.z, z);
}
bool triEq(const SwTriIndex& t, int x, int y, int z) { return t.X == x && t.Y == y && t.Z == z; }

// QuadMesh defaults (Segments=(1,1), Scale=1, Pivot=0.5, Center=0): verts v0=(0,0,0) v1=(0,1,0)
// v2=(1,0,0) v3=(1,1,0); faces f0=(0,2,1) f1=(2,3,1). (Confirmed by mesh_golden.cpp's quad case.)
Node makeQuad(int id, float cx, float cy, float cz) {
  Node m; m.id = id; m.type = "QuadMesh";
  m.params["Segments.x"] = 1.0f; m.params["Segments.y"] = 1.0f; m.params["Scale"] = 1.0f;
  m.params["Stretch.x"] = 1.0f; m.params["Stretch.y"] = 1.0f;
  m.params["Pivot.x"] = 0.5f; m.params["Pivot.y"] = 0.5f;
  m.params["Center.x"] = cx; m.params["Center.y"] = cy; m.params["Center.z"] = cz;
  return m;
}

}  // namespace

// ============================== TransformMesh (flat) ==============================
int runMeshTransformGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  bool ok = true;

  // ── SUB-CASE 1: pure translation (1,2,3). Pivot 0, Rotation 0, Scale 1 → M = T(1,2,3). ──
  {
    Graph g;
    g.nodes.push_back(makeQuad(1, 0, 0, 0));
    Node t; t.id = 2; t.type = "TransformMesh";
    t.params["Translation.x"] = 1.0f; t.params["Translation.y"] = 2.0f; t.params["Translation.z"] = 3.0f;
    g.nodes.push_back(t);
    // QuadMesh.Data (out port = index 13) → TransformMesh.Mesh (in port 0). pinId resolves spec order.
    int quadOut = -1, txMeshIn = 0;
    {  // find QuadMesh output pin index + TransformMesh Mesh input pin index from the specs
      const NodeSpec* qs = findSpec("QuadMesh");
      for (size_t i = 0; i < qs->ports.size(); ++i) if (!qs->ports[i].isInput) { quadOut = (int)i; break; }
      const NodeSpec* tsp = findSpec("TransformMesh");
      for (size_t i = 0; i < tsp->ports.size(); ++i)
        if (tsp->ports[i].isInput && tsp->ports[i].dataType == "Mesh") { txMeshIn = (int)i; break; }
    }
    g.connections.push_back({100, pinId(1, quadOut), pinId(2, txMeshIn)});

    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    meshInjectBug() = injectBug;
    pg.cook(g, ctx, nullptr, /*terminal=*/2);
    pg.cook(g, ctx, nullptr, /*terminal=*/2);  // second cook: buffer reuse, same result
    meshInjectBug() = false;

    const MTL::Buffer* vb = nullptr; const MTL::Buffer* ib = nullptr; uint32_t vc = 0, fc = 0;
    bool got = pg.debugCookedMesh(2, vb, vc, ib, fc);
    bool pass = got && vc == 4 && fc == 2;
    if (pass) {
      const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
      const SwTriIndex* f = (const SwTriIndex*)const_cast<MTL::Buffer*>(ib)->contents();
      // v0=(0,0,0)+t=(1,2,3); v1=(0,1,0)+t=(1,3,3); v2=(1,0,0)+t=(2,2,3); v3=(1,1,0)+t=(2,3,3).
      bool vOk = posEq(v[0], 1, 2, 3) && posEq(v[1], 1, 3, 3) && posEq(v[2], 2, 2, 3) && posEq(v[3], 2, 3, 3);
      bool fOk = triEq(f[0], 0, 2, 1) && triEq(f[1], 2, 3, 1);  // topology unchanged
      pass = vOk && fOk;
      std::printf("[selftest-mesh-transform] translate(1,2,3): v0=(%.1f,%.1f,%.1f) v3=(%.1f,%.1f,%.1f) "
                  "vOk=%d fOk=%d\n", v[0].Position.x, v[0].Position.y, v[0].Position.z,
                  v[3].Position.x, v[3].Position.y, v[3].Position.z, vOk, fOk);
    } else {
      std::printf("[selftest-mesh-transform] translate FAIL: no cooked mesh (got=%d vc=%u fc=%u)\n", got, vc, fc);
    }
    ok = ok && pass;
  }

  // ── SUB-CASE 2: pure scale (2,2,2). Pivot 0 → M = S(2). v3=(1,1,0)→(2,2,0). ──
  {
    PointGraph pg2(dev, nullptr, q, 64, 64);
    Graph g;
    g.nodes.push_back(makeQuad(1, 0, 0, 0));
    Node t; t.id = 2; t.type = "TransformMesh";
    t.params["Scale.x"] = 2.0f; t.params["Scale.y"] = 2.0f; t.params["Scale.z"] = 2.0f;
    g.nodes.push_back(t);
    int quadOut = -1, txMeshIn = 0;
    { const NodeSpec* qs = findSpec("QuadMesh");
      for (size_t i = 0; i < qs->ports.size(); ++i) if (!qs->ports[i].isInput) { quadOut = (int)i; break; }
      const NodeSpec* tsp = findSpec("TransformMesh");
      for (size_t i = 0; i < tsp->ports.size(); ++i)
        if (tsp->ports[i].isInput && tsp->ports[i].dataType == "Mesh") { txMeshIn = (int)i; break; } }
    g.connections.push_back({100, pinId(1, quadOut), pinId(2, txMeshIn)});

    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    meshInjectBug() = injectBug;
    pg2.cook(g, ctx, nullptr, /*terminal=*/2);
    meshInjectBug() = false;

    const MTL::Buffer* vb = nullptr; const MTL::Buffer* ib = nullptr; uint32_t vc = 0, fc = 0;
    bool got = pg2.debugCookedMesh(2, vb, vc, ib, fc);
    bool pass = got && vc == 4;
    if (pass) {
      const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
      bool vOk = posEq(v[0], 0, 0, 0) && posEq(v[2], 2, 0, 0) && posEq(v[3], 2, 2, 0);
      pass = vOk;
      std::printf("[selftest-mesh-transform] scale(2): v2=(%.1f,%.1f,%.1f) v3=(%.1f,%.1f,%.1f) vOk=%d\n",
                  v[2].Position.x, v[2].Position.y, v[2].Position.z,
                  v[3].Position.x, v[3].Position.y, v[3].Position.z, vOk);
    }
    ok = ok && pass;
  }

  q->release(); dev->release(); pool->release();
  std::printf("[selftest-mesh-transform] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;  // injectBug corrupts v[0] in the REAL cook → vOk false → return 1 (tooth bites)
}

// ============================== CombineMeshes (flat) ==============================
int runMeshCombineGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, nullptr, q, 64, 64);

  Graph g;
  g.nodes.push_back(makeQuad(1, 0, 0, 0));      // mesh A at origin
  g.nodes.push_back(makeQuad(2, 10, 0, 0));     // mesh B shifted +10 in x (verts distinguishable)
  Node cm; cm.id = 3; cm.type = "CombineMeshes"; g.nodes.push_back(cm);
  int quadOut = -1, combMeshIn = 0;
  { const NodeSpec* qs = findSpec("QuadMesh");
    for (size_t i = 0; i < qs->ports.size(); ++i) if (!qs->ports[i].isInput) { quadOut = (int)i; break; }
    const NodeSpec* cs = findSpec("CombineMeshes");
    for (size_t i = 0; i < cs->ports.size(); ++i)
      if (cs->ports[i].isInput && cs->ports[i].dataType == "Mesh") { combMeshIn = (int)i; break; } }
  // Two wires into the SAME MultiInput Mesh port, A first then B (wire-declaration order).
  g.connections.push_back({100, pinId(1, quadOut), pinId(3, combMeshIn)});
  g.connections.push_back({101, pinId(2, quadOut), pinId(3, combMeshIn)});

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  meshInjectBug() = injectBug;
  pg.cook(g, ctx, nullptr, /*terminal=*/3);
  meshInjectBug() = false;

  const MTL::Buffer* vb = nullptr; const MTL::Buffer* ib = nullptr; uint32_t vc = 0, fc = 0;
  bool got = pg.debugCookedMesh(3, vb, vc, ib, fc);
  bool ok = got && vc == 8 && fc == 4;  // 4+4 verts, 2+2 faces
  if (!got) std::printf("[selftest-mesh-combine] FAIL: no cooked mesh\n");
  if (ok) {
    const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
    const SwTriIndex* f = (const SwTriIndex*)const_cast<MTL::Buffer*>(ib)->contents();
    // First mesh verts at origin (v0=(0,0,0)..v3=(1,1,0)); second mesh verts +10 x (v4=(10,0,0)..v7=(11,1,0)).
    bool vOk = posEq(v[0], 0, 0, 0) && posEq(v[3], 1, 1, 0) && posEq(v[4], 10, 0, 0) && posEq(v[7], 11, 1, 0);
    // Faces: A = (0,2,1),(2,3,1); B REBASED by +4 = (4,6,5),(6,7,5).
    bool fOk = triEq(f[0], 0, 2, 1) && triEq(f[1], 2, 3, 1) && triEq(f[2], 4, 6, 5) && triEq(f[3], 6, 7, 5);
    ok = vOk && fOk;
    std::printf("[selftest-mesh-combine] verts=%u faces=%u v4=(%.1f,%.1f,%.1f) f2=(%d,%d,%d) f3=(%d,%d,%d) "
                "vOk=%d fOk=%d\n", vc, fc, v[4].Position.x, v[4].Position.y, v[4].Position.z,
                f[2].X, f[2].Y, f[2].Z, f[3].X, f[3].Y, f[3].Z, vOk, fOk);
  }

  q->release(); dev->release(); pool->release();
  std::printf("[selftest-mesh-combine] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;  // injectBug corrupts merged face[0] in the REAL cook → fOk false → return 1
}

// ============================== ★ PRODUCTION PIXEL (R-2) ==============================
// QuadMesh → TransformMesh → DrawMeshUnlit → RenderTarget through the canonical production path
// (libFromGraph → buildEvalGraph → cookResident). The TRANSFORM moves the quad's center ON the camera
// axis (Center=(-0.5,-0.5,0) on the QuadMesh, identity transform) so its footprint is NDC [-0.5,0.5]²;
// an interior probe must read the draw color. This is the leg that proves: (a) the resident MESH gather
// LIVES (cookResidentMesh runs the QuadMesh→TransformMesh chain on the production path), AND (b) the
// pre-existing DrawMeshUnlit production black-hole is FIXED (resident cookCommand now has a Mesh branch).
int runMeshInputProductionGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;
  const float aspect = 1.0f;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-mesh-production] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // DrawMeshUnlit(cmd) + RenderTarget(tex); QuadMesh/TransformMesh self-register

  PointGraph pg(dev, lib, q, W, H);
  Graph g;
  // QuadMesh centered on the camera axis (Center=(-0.5,-0.5,0)) → after the IDENTITY transform its
  // footprint is NDC [-0.5,0.5]² (default camera: z=0 quad projects NDC=world.xy).
  g.nodes.push_back(makeQuad(1, -0.5f, -0.5f, 0.0f));
  Node t; t.id = 2; t.type = "TransformMesh";  // identity (defaults) — proves the gather chain, not the math
  g.nodes.push_back(t);
  Node draw; draw.id = 3; draw.type = "DrawMeshUnlit";
  draw.params["Color.x"] = 1.0f; draw.params["Color.y"] = 0.0f;
  draw.params["Color.z"] = 0.0f; draw.params["Color.w"] = 1.0f;  // RED
  g.nodes.push_back(draw);
  Node rt; rt.id = 4; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f;  // Custom
  rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
  g.nodes.push_back(rt);

  int quadOut = -1, txMeshIn = 0, txOut = -1, drawMeshIn = 0, drawOut = -1, rtCmdIn = 0;
  { const NodeSpec* qs = findSpec("QuadMesh");
    for (size_t i = 0; i < qs->ports.size(); ++i) if (!qs->ports[i].isInput) { quadOut = (int)i; break; }
    const NodeSpec* tsp = findSpec("TransformMesh");
    for (size_t i = 0; i < tsp->ports.size(); ++i) {
      if (tsp->ports[i].isInput && tsp->ports[i].dataType == "Mesh") txMeshIn = (int)i;
      if (!tsp->ports[i].isInput) txOut = (int)i;
    }
    const NodeSpec* ds = findSpec("DrawMeshUnlit");
    for (size_t i = 0; i < ds->ports.size(); ++i) {
      if (ds->ports[i].isInput && ds->ports[i].dataType == "Mesh") drawMeshIn = (int)i;
      if (!ds->ports[i].isInput) drawOut = (int)i;
    }
    const NodeSpec* rs = findSpec("RenderTarget");
    for (size_t i = 0; i < rs->ports.size(); ++i)
      if (rs->ports[i].isInput && rs->ports[i].dataType == "Command") { rtCmdIn = (int)i; break; } }
  g.connections.push_back({100, pinId(1, quadOut), pinId(2, txMeshIn)});    // QuadMesh → TransformMesh
  g.connections.push_back({101, pinId(2, txOut), pinId(3, drawMeshIn)});    // TransformMesh → DrawMeshUnlit
  g.connections.push_back({102, pinId(3, drawOut), pinId(4, rtCmdIn)});     // DrawMeshUnlit → RenderTarget

  // PRODUCTION path: flat Graph → SymbolLibrary (paths == ids) → resident graph → cookResident.
  SymbolLibrary slib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  meshInjectBug() = injectBug;
  pg.cookResident(rg, ctx, nullptr, /*RenderTarget path*/ "4");
  meshInjectBug() = false;

  MTL::Texture* tex = pg.target();
  bool sized = tex && (uint32_t)tex->width() == W && (uint32_t)tex->height() == H;
  // Host-project the quad interior (NDC 0.2,0.2) + a far corner (NDC 0.9,0.9 outside the quad).
  auto project = [&](float wx, float wy, float wz, float out[3]) {
    LayerCameraForward cam = defaultLayerCameraForward(aspect);
    Mat4 o2c = objectToClipSpace(mat4Identity(), cam.worldToCamera, cam.cameraToClipSpace);
    mat4TransformPointDivW(o2c, wx, wy, wz, out);
  };
  auto ndcXToPx = [&](float ndcX) { return (int)((ndcX * 0.5f + 0.5f) * (float)(W - 1) + 0.5f); };
  auto ndcYToPx = [&](float ndcY) { return (int)((1.0f - (ndcY * 0.5f + 0.5f)) * (float)(H - 1) + 0.5f); };

  // ★The probe sits in the LOWER-LEFT triangle f0=(v0,v2,v1) ONLY — v0 is the object corner
  // (-0.5,-0.5,0)→NDC(-0.5,-0.5). meshInjectBug flies v0 to (-999,...), which COLLAPSES f0 (its other
  // two verts v1/v2 stay, but the triangle degenerates / flies off) → the lower-left probe (NDC
  // -0.3,-0.3, on the x+y<0 side of the v1↔v2 diagonal, covered ONLY by f0) reads BACKGROUND. So the
  // bite is GENUINE: it moves the asserted pixel, not merely inverting a faithful pass (Cut47 trap).
  int interiorR = 0, interiorG = 0, interiorB = 0, cornerR = 0, cornerG = 0, cornerB = 0;
  bool interiorRed = false, cornerClear = false;
  if (sized) {
    std::vector<uint8_t> px((size_t)W * H * 4, 0);
    tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
    auto readRGB = [&](int x, int y, int& r, int& gg, int& b) {
      size_t i = ((size_t)y * W + x) * 4; r = px[i]; gg = px[i + 1]; b = px[i + 2];
    };
    float ie[3]; project(-0.3f, -0.3f, 0.0f, ie);  // lower-left interior, in f0 (the v0 triangle) only
    float fc[3]; project(0.9f, 0.9f, 0.0f, fc);     // far corner, outside the quad → background
    readRGB(ndcXToPx(ie[0]), ndcYToPx(ie[1]), interiorR, interiorG, interiorB);
    readRGB(ndcXToPx(fc[0]), ndcYToPx(fc[1]), cornerR, cornerG, cornerB);
    interiorRed = interiorR > 250 && interiorG < 5 && interiorB < 5;       // RED inside the transformed quad
    cornerClear = cornerR < 30 && cornerG < 30 && cornerB < 30;            // background outside
  }

  // faithful: interior RED + corner clear → the transformed quad is on screen via the production resident
  // mesh gather. NO injectBug branch (mesh_golden.cpp convention): injectBug collapses f0 (v0 flies off
  // in the REAL cook) → the f0-only probe reads background → interiorRed false → pass false → return 1
  // (the tooth bites on the actual cook path, not by inverting a faithful pass).
  bool pass = sized && interiorRed && cornerClear;
  std::printf("[selftest-mesh-production] ★cookResident pixel: f0probe=(%d,%d,%d) corner=(%d,%d,%d) "
              "interiorRed=%d cornerClear=%d size=%lux%lu -> %s\n",
              interiorR, interiorG, interiorB, cornerR, cornerG, cornerB, interiorRed ? 1 : 0,
              cornerClear ? 1 : 0, tex ? tex->width() : 0, tex ? tex->height() : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  std::printf("[selftest-mesh-production] %s\n", pass ? "PASS" : "FAIL");
  return pass ? 0 : 1;  // injectBug corrupts the cook → quad wrecked → interior not RED → return 1
}

}  // namespace sw
