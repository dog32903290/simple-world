// mesh_loadobj_golden — --selftest-mesh-loadobj. CPU-READBACK + production-pixel golden for LoadObj
// (mesh generate: Wavefront .OBJ → SwVertex/SwTriIndex pair via the obj_parse + distinct/TBN seam).
//
// The golden feeds an EMBEDDED 4-vertex colored quad .obj (written to a temp file — the op's Path
// rides the mesh STRING channel):
//     v 0 0 0 1 0 0 / v 1 0 0 0 1 0 / v 1 1 0 0 0 1 / v 0 1 0 0.5 0.5 0.5
//     vt (0,0)(1,0)(1,1)(0,1) ; vn (0,0,1) ; f 1/1/1 2/2/1 3/3/1 4/4/1  (quad → 2 tris)
//
// EXPECTED — hand-derived from LoadObj.cs MeshDataSet (:121-169) + ObjMesh.cs / MeshUtils.cs:
//   Distinct vertices: 4 unique (pos,normal,uv) triples, discovered in face order (0,1,2 then 3),
//   position-index re-sort keeps 0,1,2,3 (ObjMesh.cs:292-326).
//   TBN (MeshUtils.cs:5, both faces): q1/q2 vs st1/st2 give tRaw=(1,0,0); bitangent =
//   normalize(cross(n,t)) = (0,1,0); tangent = normalize(cross(b,n)) = (1,0,0). Normal (0,0,1).
//   CASE 1 (SortVertices=Ignore(6), ScaleFactor=2 — the .t3-default sort + a NON-identity scale):
//     verts[i] = Position*2 → (0,0,0)(2,0,0)(2,2,0)(0,2,0); Texcoord (0,0)(1,0)(1,1)(0,1);
//     ColorRgb (1,0,0)(0,1,0)(0,0,1)(0.5,0.5,0.5)  [Colors parallel: every v has color, :128-132];
//     Selection=1; faces = (0,1,2),(0,2,3)  [OBJ order verbatim, NO winding flip, :158-169].
//   CASE 2 (SortVertices=XForward(0), ScaleFactor=1 — the SORT + reversedLookup tooth):
//     distinct positions X = [0,1,1,0] → stable XForward order = [0,3,1,2]
//     → verts = (0,0,0)(0,1,0)(1,0,0)(1,1,0); reversedLookup remaps the SAME faces to
//       (0,2,3),(0,3,1)  [LoadObj.cs:125 reversedLookup + :158-169].
//   CASE 3 (PRODUCTION PIXEL, resident leg — proves the RESIDENT mesh STRING channel): an NDC-sized
//     quad obj → LoadObj → DrawMeshUnlit(RED) → RenderTarget through libFromGraph → buildEvalGraph →
//     cookResident (the mesh_input_golden R-2 recipe). Interior probe RED + far corner background.
//     Without the resident Path channel the load yields 0 verts → nothing drawn → probe RED fails.
//
// injectBug → meshInjectBug() → the REAL cook corrupts verts[0] → CASE 1's v0 assert diverges (flat)
// AND CASE 3's v0-triangle probe loses its corner → RED on the actual cook path.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/field_camera.h"         // defaultLayerCameraForward/objectToClipSpace/mat4TransformPointDivW
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"         // libFromGraph
#include "runtime/mesh_op_registry.h"
#include "runtime/point_graph.h"
#include "runtime/resident_eval_graph.h"  // buildEvalGraph
#include "runtime/sw_mesh.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

bool nearLo(float a, float b) { return std::fabs(a - b) < 1e-5f; }
bool pos3Eq(const SwVertex& v, float x, float y, float z) {
  return nearLo(v.Position.x, x) && nearLo(v.Position.y, y) && nearLo(v.Position.z, z);
}
bool triEqLo(const SwTriIndex& t, int x, int y, int z) { return t.X == x && t.Y == y && t.Z == z; }

// Write `text` to a temp .obj under TMPDIR; returns the path ("" on failure).
std::string writeTempObj(const char* name, const char* text) {
  const char* tmp = std::getenv("TMPDIR");
  std::string dir = tmp && *tmp ? tmp : "/tmp";
  if (dir.back() != '/') dir += '/';
  std::string path = dir + name;
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return std::string();
  std::fwrite(text, 1, std::strlen(text), f);
  std::fclose(f);
  return path;
}

const char* kQuadObj =
    "v 0 0 0 1 0 0\n"
    "v 1 0 0 0 1 0\n"
    "v 1 1 0 0 0 1\n"
    "v 0 1 0 0.5 0.5 0.5\n"
    "vt 0 0\n"
    "vt 1 0\n"
    "vt 1 1\n"
    "vt 0 1\n"
    "vn 0 0 1\n"
    "f 1/1/1 2/2/1 3/3/1 4/4/1\n";

// NDC-footprint quad ([-0.5,0.5]², z=0) for the production-pixel resident leg.
const char* kNdcQuadObj =
    "v -0.5 -0.5 0\n"
    "v 0.5 -0.5 0\n"
    "v 0.5 0.5 0\n"
    "v -0.5 0.5 0\n"
    "vt 0 0\n"
    "vt 1 0\n"
    "vt 1 1\n"
    "vt 0 1\n"
    "vn 0 0 1\n"
    "f 1/1/1 2/2/1 3/3/1 4/4/1\n";

}  // namespace

int runMeshLoadObjGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  meshInjectBug() = injectBug;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);

  const std::string objPath = writeTempObj("sw_loadobj_golden.obj", kQuadObj);
  const std::string ndcPath = writeTempObj("sw_loadobj_golden_ndc.obj", kNdcQuadObj);
  bool ok = !objPath.empty() && !ndcPath.empty();
  if (!ok) std::printf("[selftest-mesh-loadobj] FAIL: temp .obj write failed\n");

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  // ── CASE 1 (flat leg): Ignore sort + ScaleFactor=2 — full vertex/face/TBN/color spine. ──
  if (ok) {
    PointGraph pg(dev, lib, q, 64, 64);
    Graph g;
    Node m; m.id = 1; m.type = "LoadObj";
    m.strParams["Path"] = objPath;          // the mesh STRING channel (flat strParams override)
    m.params["ScaleFactor"] = 2.0f;
    m.params["SortVertices"] = 6.0f;        // Ignore (.t3 default)
    m.params["ClearGPUCache"] = 1.0f;       // fresh parse (drops any stale cache entry for this path)
    g.nodes.push_back(m);
    pg.cook(g, ctx, nullptr, 1);

    const MTL::Buffer* vb = nullptr; const MTL::Buffer* ib = nullptr;
    uint32_t vc = 0, fc = 0;
    bool got = pg.debugCookedMesh(1, vb, vc, ib, fc);
    bool pass = got && vc == 4 && fc == 2;
    if (pass) {
      const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
      const SwTriIndex* f = (const SwTriIndex*)const_cast<MTL::Buffer*>(ib)->contents();
      bool vOk = pos3Eq(v[0], 0, 0, 0) && pos3Eq(v[1], 2, 0, 0) && pos3Eq(v[2], 2, 2, 0) &&
                 pos3Eq(v[3], 0, 2, 0);
      bool nOk = nearLo(v[0].Normal.z, 1.0f) && nearLo(v[2].Normal.z, 1.0f);
      bool tbnOk = nearLo(v[0].Tangent.x, 1.0f) && nearLo(v[0].Tangent.y, 0.0f) &&
                   nearLo(v[0].Bitangent.y, 1.0f) && nearLo(v[0].Bitangent.x, 0.0f) &&
                   nearLo(v[3].Tangent.x, 1.0f) && nearLo(v[3].Bitangent.y, 1.0f);
      bool uvOk = nearLo(v[1].Texcoord.x, 1.0f) && nearLo(v[1].Texcoord.y, 0.0f) &&
                  nearLo(v[2].Texcoord.x, 1.0f) && nearLo(v[2].Texcoord.y, 1.0f);
      bool colOk = nearLo(v[0].ColorRgb.x, 1.0f) && nearLo(v[0].ColorRgb.y, 0.0f) &&
                   nearLo(v[1].ColorRgb.y, 1.0f) && nearLo(v[2].ColorRgb.z, 1.0f) &&
                   nearLo(v[3].ColorRgb.x, 0.5f) && nearLo(v[3].ColorRgb.y, 0.5f);
      bool selOk = nearLo(v[0].Selection, 1.0f) && nearLo(v[3].Selection, 1.0f);
      bool fOk = triEqLo(f[0], 0, 1, 2) && triEqLo(f[1], 0, 2, 3);  // OBJ order, NO winding flip
      pass = vOk && nOk && tbnOk && uvOk && colOk && selOk && fOk;
      std::printf("[selftest-mesh-loadobj] case1 scale2/ignore: vc=%u fc=%u v1=(%.1f,%.1f,%.1f) "
                  "f0=(%d,%d,%d) f1=(%d,%d,%d) vOk=%d tbnOk=%d uvOk=%d colOk=%d fOk=%d\n",
                  vc, fc, v[1].Position.x, v[1].Position.y, v[1].Position.z, f[0].X, f[0].Y, f[0].Z,
                  f[1].X, f[1].Y, f[1].Z, vOk, tbnOk, uvOk, colOk, fOk);
    } else {
      std::printf("[selftest-mesh-loadobj] case1 FAIL: got=%d vc=%u fc=%u (want 4/2)\n", got, vc, fc);
    }
    ok = ok && pass;
  }

  // ── CASE 2 (flat leg): XForward sort — the SortedVertexIndices + reversedLookup tooth. ──
  if (ok && !injectBug) {  // injectBug already proven on case 1's v0; keep case 2 a pure sort pin
    PointGraph pg(dev, lib, q, 64, 64);
    Graph g;
    Node m; m.id = 1; m.type = "LoadObj";
    m.strParams["Path"] = objPath;
    m.params["ScaleFactor"] = 1.0f;
    m.params["SortVertices"] = 0.0f;  // XForward
    g.nodes.push_back(m);
    pg.cook(g, ctx, nullptr, 1);

    const MTL::Buffer* vb = nullptr; const MTL::Buffer* ib = nullptr;
    uint32_t vc = 0, fc = 0;
    bool got = pg.debugCookedMesh(1, vb, vc, ib, fc);
    bool pass = got && vc == 4 && fc == 2;
    if (pass) {
      const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
      const SwTriIndex* f = (const SwTriIndex*)const_cast<MTL::Buffer*>(ib)->contents();
      // stable XForward on X=[0,1,1,0] → order [0,3,1,2] (header derivation).
      bool vOk = pos3Eq(v[0], 0, 0, 0) && pos3Eq(v[1], 0, 1, 0) && pos3Eq(v[2], 1, 0, 0) &&
                 pos3Eq(v[3], 1, 1, 0);
      // reversedLookup remap: faces (0,1,2)/(0,2,3) → (0,2,3)/(0,3,1).
      bool fOk = triEqLo(f[0], 0, 2, 3) && triEqLo(f[1], 0, 3, 1);
      pass = vOk && fOk;
      std::printf("[selftest-mesh-loadobj] case2 xforward: v1=(%.0f,%.0f,%.0f) f0=(%d,%d,%d) "
                  "f1=(%d,%d,%d) vOk=%d fOk=%d\n",
                  v[1].Position.x, v[1].Position.y, v[1].Position.z, f[0].X, f[0].Y, f[0].Z, f[1].X,
                  f[1].Y, f[1].Z, vOk, fOk);
    } else {
      std::printf("[selftest-mesh-loadobj] case2 FAIL: got=%d vc=%u fc=%u (want 4/2)\n", got, vc, fc);
    }
    ok = ok && pass;
  }

  // ── CASE 3 (PRODUCTION PIXEL, resident leg): LoadObj → DrawMeshUnlit → RenderTarget. ──
  if (ok && lib) {
    const uint32_t W = 256, H = 256;
    registerBuiltinPointOps();  // DrawMeshUnlit(cmd) + RenderTarget(tex); LoadObj self-registers
    PointGraph pg(dev, lib, q, W, H);
    Graph g;
    Node m; m.id = 1; m.type = "LoadObj";
    m.strParams["Path"] = ndcPath;  // → strOverrides → flatten strInputs (the RESIDENT channel)
    g.nodes.push_back(m);
    Node draw; draw.id = 2; draw.type = "DrawMeshUnlit";
    draw.params["Color.x"] = 1.0f; draw.params["Color.y"] = 0.0f;
    draw.params["Color.z"] = 0.0f; draw.params["Color.w"] = 1.0f;  // RED
    g.nodes.push_back(draw);
    Node rt; rt.id = 3; rt.type = "RenderTarget";
    rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)W; rt.params["CustomH"] = (float)H;
    g.nodes.push_back(rt);

    int objOut = -1, drawMeshIn = 0, drawOut = -1, rtCmdIn = 0;
    { const NodeSpec* os = findSpec("LoadObj");
      for (size_t i = 0; i < os->ports.size(); ++i) if (!os->ports[i].isInput) { objOut = (int)i; break; }
      const NodeSpec* ds = findSpec("DrawMeshUnlit");
      for (size_t i = 0; i < ds->ports.size(); ++i) {
        if (ds->ports[i].isInput && ds->ports[i].dataType == "Mesh") drawMeshIn = (int)i;
        if (!ds->ports[i].isInput) drawOut = (int)i;
      }
      const NodeSpec* rs = findSpec("RenderTarget");
      for (size_t i = 0; i < rs->ports.size(); ++i)
        if (rs->ports[i].isInput && rs->ports[i].dataType == "Command") { rtCmdIn = (int)i; break; } }
    g.connections.push_back({100, pinId(1, objOut), pinId(2, drawMeshIn)});
    g.connections.push_back({101, pinId(2, drawOut), pinId(3, rtCmdIn)});

    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    pg.cookResident(rg, ctx, nullptr, /*RenderTarget path*/ "3");

    MTL::Texture* tex = pg.target();
    bool sized = tex && (uint32_t)tex->width() == W && (uint32_t)tex->height() == H;
    bool interiorRed = false, cornerClear = false;
    int ir = 0, ig = 0, ib2 = 0, cr = 0, cg = 0, cb = 0;
    if (sized) {
      std::vector<uint8_t> px((size_t)W * H * 4, 0);
      tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
      auto project = [&](float wx, float wy, float wz, float out[3]) {
        LayerCameraForward cam = defaultLayerCameraForward(1.0f);
        Mat4 o2c = objectToClipSpace(mat4Identity(), cam.worldToCamera, cam.cameraToClipSpace);
        mat4TransformPointDivW(o2c, wx, wy, wz, out);
      };
      auto ndcXToPx = [&](float x) { return (int)((x * 0.5f + 0.5f) * (float)(W - 1) + 0.5f); };
      auto ndcYToPx = [&](float y) { return (int)((1.0f - (y * 0.5f + 0.5f)) * (float)(H - 1) + 0.5f); };
      auto readRGB = [&](int x, int y, int& r, int& gg, int& b) {
        size_t i = ((size_t)y * W + x) * 4; r = px[i]; gg = px[i + 1]; b = px[i + 2];
      };
      // Interior probe in the v0-corner triangle f0=(v0,v1,v2), x>y half (NDC 0.2,-0.2); corner outside.
      float ie[3]; project(0.2f, -0.2f, 0.0f, ie);
      float fc2[3]; project(0.9f, 0.9f, 0.0f, fc2);
      readRGB(ndcXToPx(ie[0]), ndcYToPx(ie[1]), ir, ig, ib2);
      readRGB(ndcXToPx(fc2[0]), ndcYToPx(fc2[1]), cr, cg, cb);
      interiorRed = ir > 250 && ig < 5 && ib2 < 5;
      cornerClear = cr < 30 && cg < 30 && cb < 30;
    }
    bool pass = sized && interiorRed && cornerClear;
    std::printf("[selftest-mesh-loadobj] case3 resident-pixel: interior=(%d,%d,%d) corner=(%d,%d,%d) "
                "interiorRed=%d cornerClear=%d -> %s\n",
                ir, ig, ib2, cr, cg, cb, interiorRed, cornerClear, pass ? "ok" : "FAIL");
    ok = ok && pass;
  } else if (ok && !lib) {
    std::printf("[selftest-mesh-loadobj] case3 SKIP: no metallib\n");
    ok = false;
  }

  meshInjectBug() = false;
  std::remove(objPath.c_str());
  std::remove(ndcPath.c_str());
  if (lib) lib->release();
  q->release();
  dev->release();
  pool->release();

  if (injectBug) {
    if (ok) {
      std::printf("[selftest-mesh-loadobj] injectBug did not trip (v0 corruption changed nothing)\n");
      return 0;  // dead tooth → exit 0 so --bite's NO-BITE list catches it (GOLDEN_STANDARD P1)
    }
    std::printf("[selftest-mesh-loadobj] injectBug correctly RED (REAL cook corrupted verts[0] → case1 "
                "v0/TBN asserts diverged)\n");
    return 1;
  }
  std::printf("[selftest-mesh-loadobj] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
