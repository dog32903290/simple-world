// mesh_modify2_golden — goldens for the second batch of mesh→mesh MODIFY consumers (Phase C mesh-input
// leaves): SplitMeshVertices / SelectVertices / DeformMesh / CollapseVertices / MeshProjectUV.
//
// Each op gets a FLAT (CPU-readback) leg: QuadMesh → op → debugCookedMesh; assert the exact transformed
// vertex attributes / counts against the hand-derived TiXL shader math (mesh-SplitVertices.hlsl,
// mesh-SelectVertices.hlsl, mesh-Deform.hlsl, mesh-CollapseVertices.hlsl, mesh-ProjectUV.hlsl), plus a
// PRODUCTION-PIXEL leg (R-2): QuadMesh → op → DrawMeshUnlit → RenderTarget through the canonical resident
// path; assert the quad lights on screen (proves the op runs on the production resident mesh gather).
// injectBug corrupts the op's primary output AND v0.Position in the REAL cook → flat assertion fails and
// the production f0 probe goes dark → RED on the actual cook path.
//
// QuadMesh defaults (Segments=(1,1), Scale=1, Pivot=0.5, Center varies): verts v0=(0,0,0) v1=(0,1,0)
// v2=(1,0,0) v3=(1,1,0); faces f0=(0,2,1) f1=(2,3,1). Attrs: Normal=(0,0,1), Tangent=(1,0,0),
// Bitangent=(0,1,0), Selection=1, TexCoord v0=(0,0) v1=(0,1) v2=(1,0) v3=(1,1).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/field_camera.h"        // defaultLayerCameraForward / objectToClipSpace
#include "runtime/graph.h"               // Graph/Node/Connection/pinId/findSpec
#include "runtime/graph_bridge.h"        // libFromGraph
#include "runtime/mesh_op_registry.h"    // meshInjectBug
#include "runtime/point_graph.h"         // PointGraph::cook/cookResident + debugCookedMesh + registerBuiltinPointOps
#include "runtime/resident_eval_graph.h" // buildEvalGraph
#include "runtime/sw_mesh.h"             // SwVertex / SwTriIndex

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

bool nearf(float a, float b, float t = 1e-3f) { return std::fabs(a - b) < t; }
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

void meshPins(const char* type, int& outPin, int& meshInPin) {
  outPin = -1; meshInPin = 0;
  const NodeSpec* s = findSpec(type);
  for (size_t i = 0; i < s->ports.size(); ++i) {
    if (!s->ports[i].isInput && s->ports[i].dataType == "Mesh") outPin = (int)i;
    if (s->ports[i].isInput && s->ports[i].dataType == "Mesh" && meshInPin == 0) meshInPin = (int)i;
  }
  if (outPin < 0) for (size_t i = 0; i < s->ports.size(); ++i) if (!s->ports[i].isInput) { outPin = (int)i; break; }
}

// Shared production-pixel leg (identical to mesh_modify_golden's): QuadMesh(Center -0.5,-0.5,0) → op →
// DrawMeshUnlit → RenderTarget via cookResident; probe the lower-left f0 triangle is RED.
int productionLeg(const char* opType, const char* tag, bool injectBug,
                  const std::vector<std::pair<const char*, float>>& opParams) {
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
  g.nodes.push_back(makeQuad(1, -0.5f, -0.5f, 0.0f));
  Node op; op.id = 2; op.type = opType;
  for (auto& kv : opParams) op.params[kv.first] = kv.second;
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
  pg.cookResident(rg, ctx, nullptr, "4");
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
    float ie[3]; project(-0.3f, -0.3f, 0.0f, ie);
    float fc[3]; project(0.9f, 0.9f, 0.0f, fc);
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
  return pass ? 0 : 1;
}

// Cook QuadMesh → op (with params) on the flat path and hand back the cooked mesh buffers.
bool cookFlat(MTL::Device* dev, MTL::CommandQueue* q, const char* opType, bool injectBug,
              const std::vector<std::pair<const char*, float>>& opParams,
              const SwVertex** vOut, uint32_t& vc, const SwTriIndex** iOut, uint32_t& fc,
              PointGraph& pg) {
  Graph g;
  g.nodes.push_back(makeQuad(1, 0, 0, 0));
  Node op; op.id = 2; op.type = opType;
  for (auto& kv : opParams) op.params[kv.first] = kv.second;
  g.nodes.push_back(op);
  int quadOut, dummy, opOut, opMeshIn;
  meshPins("QuadMesh", quadOut, dummy);
  meshPins(opType, opOut, opMeshIn);
  g.connections.push_back({100, pinId(1, quadOut), pinId(2, opMeshIn)});

  EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  meshInjectBug() = injectBug;
  pg.cook(g, ctx, nullptr, 2);
  pg.cook(g, ctx, nullptr, 2);  // buffer reuse
  meshInjectBug() = false;

  const MTL::Buffer* vb = nullptr; const MTL::Buffer* ib = nullptr;
  bool got = pg.debugCookedMesh(2, vb, vc, ib, fc);
  if (!got) return false;
  *vOut = (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
  *iOut = (const SwTriIndex*)const_cast<MTL::Buffer*>(ib)->contents();
  return true;
}

}  // namespace

// ============================== SplitMeshVertices ==============================
// QuadMesh → SplitMeshVertices (ShadeFlat=0). Topology un-weld: 2 faces → 6 verts, faces re-indexed
// (0,1,2) and (3,4,5). Each face's 3 verts are copies of the source corners; at ShadeFlat=0 the normals
// stay (0,0,1). Face f0=(v0,v2,v1) → out verts [v0,v2,v1] at positions (0,0,0)(1,0,0)(0,1,0).
int runMeshSplitVerticesGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, nullptr, q, 64, 64);

  const SwVertex* v = nullptr; const SwTriIndex* idx = nullptr; uint32_t vc = 0, fc = 0;
  bool flat = cookFlat(dev, q, "SplitMeshVertices", injectBug, {}, &v, vc, &idx, fc, pg);
  flat = flat && vc == 6 && fc == 2;
  if (flat) {
    // f0=(0,2,1): out[0]=v0(0,0,0), out[1]=v2(1,0,0), out[2]=v1(0,1,0). Re-index = (0,1,2).
    bool p0 = n3(v[0].Position, 0, 0, 0) && n3(v[1].Position, 1, 0, 0) && n3(v[2].Position, 0, 1, 0);
    bool nOk = n3(v[0].Normal, 0, 0, 1) && n3(v[5].Normal, 0, 0, 1);  // ShadeFlat=0 → normals kept
    bool iOk = idx[0].X == 0 && idx[0].Y == 1 && idx[0].Z == 2 &&
               idx[1].X == 3 && idx[1].Y == 4 && idx[1].Z == 5;
    flat = p0 && nOk && iOk;
    std::printf("[selftest-mesh-splitvertices] flat: vc=%u fc=%u p0=(%.1f,%.1f,%.1f) idx0=(%d,%d,%d) "
                "p0ok=%d nOk=%d iOk=%d\n", vc, fc, v[0].Position.x, v[0].Position.y, v[0].Position.z,
                idx[0].X, idx[0].Y, idx[0].Z, p0, nOk, iOk);
  } else {
    std::printf("[selftest-mesh-splitvertices] flat FAIL: cook (vc=%u fc=%u)\n", vc, fc);
  }
  q->release(); dev->release(); pool->release();

  int prod = productionLeg("SplitMeshVertices", "selftest-mesh-splitvertices-prod", injectBug, {});
  bool ok = flat && prod == 0;
  std::printf("[selftest-mesh-splitvertices] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ============================== SelectVertices ==============================
// QuadMesh → SelectVertices (Box volume, Center=(0.5,0.5,0) so the quad's [0,1]² centers on the gizmo,
// Stretch=1, Scale=1, FallOff=0, Mode=Override, Strength=1, ClampResult off).
// TransformVolume = inverse(T(0.5,0.5,0)) → posInVolume = pos - (0.5,0.5,0).
//   v0 (0,0,0)→(-0.5,-0.5,0): |max|=0.5 → smoothstep(1,1→clamps)... d=0.5 < 1 → t=(0.5-1)/(1-1)... uses
//   smoothstep(1+0, 1, 0.5): edge0=1,edge1=1 degenerate; HLSL smoothstep with edge0==edge1 → step. To
//   avoid the degenerate edge we use FallOff=1 → smoothstep(2,1,d). d(v0)=0.5 → t=(0.5-2)/(1-2)=1.5→clamp1
//   → s=1 (fully selected, INSIDE). A far point: Center=(5,5,0) → posInVolume huge → d>2 → s=0.
// We assert: with Center on the quad, all 4 verts Selection==1; with Center far away, all ==0.
int runMeshSelectVerticesGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  bool flat = true;

  // INSIDE: Box, Center on quad, FallOff=1 (non-degenerate edges), Mode=Override, Strength=1.
  {
    PointGraph pg(dev, nullptr, q, 64, 64);
    const SwVertex* v = nullptr; const SwTriIndex* idx = nullptr; uint32_t vc = 0, fc = 0;
    std::vector<std::pair<const char*, float>> P = {
      {"VolumeShape", 1.0f}, {"Center.x", 0.5f}, {"Center.y", 0.5f}, {"Center.z", 0.0f},
      {"FallOff", 1.0f}, {"Mode", 0.0f}, {"Strength", 1.0f}};
    bool got = cookFlat(dev, q, "SelectVertices", injectBug, P, &v, vc, &idx, fc, pg);
    bool pass = got && vc == 4;
    if (pass) {
      // d(corner)=max|posInVolume|=0.5 → smoothstep(2,1,0.5): t=(0.5-2)/(1-2)=1.5→1 → s=1.
      bool selOk = nearf(v[0].Selection, 1.0f) && nearf(v[1].Selection, 1.0f) &&
                   nearf(v[2].Selection, 1.0f) && nearf(v[3].Selection, 1.0f);
      bool pOk = n3(v[3].Position, 1, 1, 0);  // position untouched
      pass = selOk && pOk;
      std::printf("[selftest-mesh-selectvertices] inside: sel=(%.2f,%.2f,%.2f,%.2f) selOk=%d pOk=%d\n",
                  v[0].Selection, v[1].Selection, v[2].Selection, v[3].Selection, selOk, pOk);
    } else std::printf("[selftest-mesh-selectvertices] inside FAIL: cook vc=%u\n", vc);
    flat = flat && pass;
  }
  // OUTSIDE: same but Center far away → posInVolume large → d>2 → s=0 → Selection=0.
  {
    PointGraph pg(dev, nullptr, q, 64, 64);
    const SwVertex* v = nullptr; const SwTriIndex* idx = nullptr; uint32_t vc = 0, fc = 0;
    std::vector<std::pair<const char*, float>> P = {
      {"VolumeShape", 1.0f}, {"Center.x", 5.0f}, {"Center.y", 5.0f}, {"Center.z", 0.0f},
      {"FallOff", 1.0f}, {"Mode", 0.0f}, {"Strength", 1.0f}};
    bool got = cookFlat(dev, q, "SelectVertices", false, P, &v, vc, &idx, fc, pg);  // no bug (outside leg)
    bool pass = got && vc == 4;
    if (pass) {
      bool selOk = nearf(v[0].Selection, 0.0f) && nearf(v[3].Selection, 0.0f);
      pass = selOk;
      std::printf("[selftest-mesh-selectvertices] outside: sel0=%.2f sel3=%.2f selOk=%d\n",
                  v[0].Selection, v[3].Selection, selOk);
    }
    flat = flat && pass;
  }
  q->release(); dev->release(); pool->release();

  int prod = productionLeg("SelectVertices", "selftest-mesh-selectvertices-prod", injectBug, {});
  bool ok = flat && prod == 0;
  std::printf("[selftest-mesh-selectvertices] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ============================== DeformMesh ==============================
// QuadMesh → DeformMesh, UseVertexSelection=false (s=1). Twist about Z, TwistAxis=2, Twist=90°,
// TwistPivot=0, Spherize=0, Taper=0. Only Twist active.
//   twist angle for a vertex = pos.z * radians(90). All quad verts have z=0 → angle=0 → no change.
//   → Position unchanged. To get a measurable twist we set TwistAxis=0 (about X), Twist=90:
//   angle = pos.x * radians(90). v2 (1,0,0): angle = 1 * π/2; rotate (twp=pos, twPivot=0) about X:
//     tw.x=twp.x=1; tw.y=twp.y*cos - twp.z*sin = 0; tw.z=twp.y*sin + twp.z*cos = 0 → (1,0,0) unchanged
//     (y=z=0). v3 (1,1,0): angle=π/2; tw.y=1*cos(π/2)-0=~0; tw.z=1*sin(π/2)+0=1 → (1, 0, 1).
//   Assert v3.Position ≈ (1, 0, 1); v0 (0,0,0) angle=0 → (0,0,0).
int runMeshDeformGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, nullptr, q, 64, 64);

  const SwVertex* v = nullptr; const SwTriIndex* idx = nullptr; uint32_t vc = 0, fc = 0;
  std::vector<std::pair<const char*, float>> P = {
    {"UseVertexSelection", 0.0f}, {"Spherize", 0.0f}, {"Taper", 0.0f},
    {"Twist", 90.0f}, {"TwistAxis", 0.0f}};  // about X, 90°
  bool flat = cookFlat(dev, q, "DeformMesh", injectBug, P, &v, vc, &idx, fc, pg);
  flat = flat && vc == 4;
  if (flat) {
    // v3 (1,1,0): angle = 1*π/2 → (1, cos(π/2), sin(π/2)) = (1, 0, 1). v0 (0,0,0): angle 0 → unchanged.
    bool v3Ok = n3(v[3].Position, 1.0f, 0.0f, 1.0f);
    bool v0Ok = n3(v[0].Position, 0.0f, 0.0f, 0.0f);
    flat = v3Ok && v0Ok;
    std::printf("[selftest-mesh-deform] twistX90: v3=(%.3f,%.3f,%.3f) v0=(%.3f,%.3f,%.3f) v3Ok=%d v0Ok=%d\n",
                v[3].Position.x, v[3].Position.y, v[3].Position.z, v[0].Position.x, v[0].Position.y,
                v[0].Position.z, v3Ok, v0Ok);
  } else std::printf("[selftest-mesh-deform] flat FAIL: cook vc=%u\n", vc);
  q->release(); dev->release(); pool->release();

  // production leg: no params (identity deform) → quad unchanged → lit; injectBug flies v0 off → dark.
  int prod = productionLeg("DeformMesh", "selftest-mesh-deform-prod", injectBug, {});
  bool ok = flat && prod == 0;
  std::printf("[selftest-mesh-deform] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ============================== CollapseVertices ==============================
// QuadMesh → CollapseVertices, Box volume Center=(0.5,0.5,0) FallOff=1 (so s=1 everywhere on the quad),
// StepCount=1, Strength=2, GridOffset=0, Amount=1, SmoothSteps=0 (→ BlendStep=0 → smoothstep(0,1,0)=0 →
// take snap1 only). With s=1, StepCount=1: xx=1, step=1 → but step clamps via (1<<step). To get a clean
// snap pick StepCount=1, s=1 → xx=1.0, step=1, ff=0. maxS=1<<1=2. ss1=(1<<1)/2*2 = 2. snap1=floor(pos/2+0.5)*2.
//   v0 (0,0,0): floor(0/2+0.5)*2 = floor(0.5)*2 = 0 → (0,0,0). v3 (1,1,0): floor(1/2+0.5)*2=floor(1)*2=2 →
//   (2,2,0). Position = lerp(pos, snap1+GridOffset, Amount=1) = snap1. Assert v3 ≈ (2,2,0), v0 ≈ (0,0,0).
//   (ff=0 → blend uses snap1 regardless of BlendStep; SmoothSteps irrelevant here, keeps the golden exact.)
int runMeshCollapseGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, nullptr, q, 64, 64);

  const SwVertex* v = nullptr; const SwTriIndex* idx = nullptr; uint32_t vc = 0, fc = 0;
  std::vector<std::pair<const char*, float>> P = {
    {"VolumeShape", 1.0f}, {"Center.x", 0.5f}, {"Center.y", 0.5f}, {"Center.z", 0.0f}, {"FallOff", 1.0f},
    {"StepCount", 1.0f}, {"Strength", 2.0f}, {"Amount", 1.0f}, {"SmoothSteps", 0.0f}};
  bool flat = cookFlat(dev, q, "CollapseVertices", injectBug, P, &v, vc, &idx, fc, pg);
  flat = flat && vc == 4;
  if (flat) {
    // s=1 → xx=1, step=1, ff=0; ss1=(1<<1)/2*2=2; snap1=floor(pos/2+0.5)*2. v0→0, v3→(2,2,0).
    bool v0Ok = n3(v[0].Position, 0, 0, 0);
    bool v3Ok = n3(v[3].Position, 2, 2, 0);
    flat = v0Ok && v3Ok;
    std::printf("[selftest-mesh-collapse] grid2: v0=(%.2f,%.2f,%.2f) v3=(%.2f,%.2f,%.2f) v0Ok=%d v3Ok=%d\n",
                v[0].Position.x, v[0].Position.y, v[0].Position.z, v[3].Position.x, v[3].Position.y,
                v[3].Position.z, v0Ok, v3Ok);
  } else std::printf("[selftest-mesh-collapse] flat FAIL: cook vc=%u\n", vc);
  q->release(); dev->release(); pool->release();

  // production leg: Amount=0 → no collapse → quad lit; injectBug flies v0 off → dark.
  int prod = productionLeg("CollapseVertices", "selftest-mesh-collapse-prod", injectBug,
                           {{"Amount", 0.0f}});
  bool ok = flat && prod == 0;
  std::printf("[selftest-mesh-collapse] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// ============================== MeshProjectUV ==============================
// QuadMesh → MeshProjectUV (identity: Translate=0, Rotate=0, Stretch=1, Scale=1, ToTexCoord2=false).
//   Transform = Identity → uv = pos.xy + (1,1). v0 (0,0,0) → TexCoord (1,1); v3 (1,1,0) → (2,2).
//   With Translate=(0.5, -0.5, 0): uv = (pos.x+0.5, pos.y-0.5) + (1,1). v0 → (1.5, 0.5); v3 → (2.5,1.5).
int runMeshProjectUvGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  bool flat = true;

  // SUB-CASE 1: identity → uv = pos.xy + (1,1).
  {
    PointGraph pg(dev, nullptr, q, 64, 64);
    const SwVertex* v = nullptr; const SwTriIndex* idx = nullptr; uint32_t vc = 0, fc = 0;
    bool got = cookFlat(dev, q, "MeshProjectUV", injectBug, {}, &v, vc, &idx, fc, pg);
    bool pass = got && vc == 4;
    if (pass) {
      bool uOk = uvEq(v[0].Texcoord, 1.0f, 1.0f) && uvEq(v[3].Texcoord, 2.0f, 2.0f);
      bool pOk = n3(v[3].Position, 1, 1, 0);  // position untouched
      pass = uOk && pOk;
      std::printf("[selftest-mesh-projectuv] identity: uv0=(%.2f,%.2f) uv3=(%.2f,%.2f) uOk=%d pOk=%d\n",
                  v[0].Texcoord.x, v[0].Texcoord.y, v[3].Texcoord.x, v[3].Texcoord.y, uOk, pOk);
    } else std::printf("[selftest-mesh-projectuv] identity FAIL: cook vc=%u\n", vc);
    flat = flat && pass;
  }
  // SUB-CASE 2: Translate (0.5,-0.5,0) → uv = (pos.x+0.5, pos.y-0.5) + (1,1).
  {
    PointGraph pg(dev, nullptr, q, 64, 64);
    const SwVertex* v = nullptr; const SwTriIndex* idx = nullptr; uint32_t vc = 0, fc = 0;
    std::vector<std::pair<const char*, float>> P = {{"Translate.x", 0.5f}, {"Translate.y", -0.5f}};
    bool got = cookFlat(dev, q, "MeshProjectUV", false, P, &v, vc, &idx, fc, pg);
    bool pass = got && vc == 4;
    if (pass) {
      bool uOk = uvEq(v[0].Texcoord, 1.5f, 0.5f) && uvEq(v[3].Texcoord, 2.5f, 1.5f);
      pass = uOk;
      std::printf("[selftest-mesh-projectuv] translate: uv0=(%.2f,%.2f) uv3=(%.2f,%.2f) uOk=%d\n",
                  v[0].Texcoord.x, v[0].Texcoord.y, v[3].Texcoord.x, v[3].Texcoord.y, uOk);
    }
    flat = flat && pass;
  }
  q->release(); dev->release(); pool->release();

  int prod = productionLeg("MeshProjectUV", "selftest-mesh-projectuv-prod", injectBug, {});
  bool ok = flat && prod == 0;
  std::printf("[selftest-mesh-projectuv] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
