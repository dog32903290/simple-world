// mesh_sphere_golden — --selftest-mesh-sphere. CPU-READBACK golden for SphereMesh (4th cook flow).
// Builds a SphereMesh node as the cook terminal, runs PointGraph::cook (cookMeshNode sizes the
// vertex+index pair + runs the leaf), reads both buffers back via debugCookedMesh()->contents(), and
// asserts EXACT vertex positions + counts against the closed-form TiXL math (SphereMesh.cs). No GPU.
//
// CASE: Radius=1, Segments=(4,4)  ->  uSegments=4+1=5, vSegments=4+1=5.
//   verticesCount = (vSegments+1)*uSegments = 6*5 = 30.
//   polTri = 2*uSegments = 10 ; sideTri = (vSegments-2)*uSegments*2 = 3*5*2 = 30 ; faces = 40.
//   vAngleFraction = (1/4)*PI = PI/4 ; uAngleFraction = (1/4)*2PI = PI/2.
//   Top pole (vIndex 0)  -> rows 0..4 all at (0, +1, 0).
//   Bottom pole (vIndex 4) -> rows 20..24 all at (0, -1, 0).
//   Equator ring vIndex=2: vAngle=PI/2 -> tubeY=cos(PI/2)=0, radius1=sin(PI/2)*1=1. Row base=2*5=10.
//     uIndex 0: uAngle=0    -> p=(sin0*1, 0, cos0*1)   = (0, 0, 1)    -> verts[10]
//     uIndex 1: uAngle=PI/2 -> p=(sin(PI/2), 0, cos(PI/2)) = (1, 0, 0) -> verts[11]
//     uIndex 2: uAngle=PI   -> p=(0, 0, -1)            -> verts[12]
//     uIndex 3: uAngle=3PI/2-> p=(-1, 0, 0)            -> verts[13]
//   Normal at verts[10] = normalize((0,0,1)) = (0,0,1); at verts[11] = (1,0,0).
//
// injectBug routes through meshInjectBug() -> corrupts the top-pole vertex in the REAL cook -> RED.
#include <cmath>
#include <cstdint>
#include <cstdio>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"       // EvaluationContext
#include "runtime/graph.h"             // Graph/Node
#include "runtime/mesh_op_registry.h"  // meshInjectBug
#include "runtime/point_graph.h"       // PointGraph::cook + debugCookedMesh
#include "runtime/sw_mesh.h"           // SwVertex / SwTriIndex

namespace sw {
namespace {
bool nearf(float a, float b) { return std::fabs(a - b) < 1e-5f; }
bool posEq(const SwVertex& v, float x, float y, float z) {
  return nearf(v.Position.x, x) && nearf(v.Position.y, y) && nearf(v.Position.z, z);
}
bool nrmEq(const SwVertex& v, float x, float y, float z) {
  return nearf(v.Normal.x, x) && nearf(v.Normal.y, y) && nearf(v.Normal.z, z);
}
}  // namespace

int runMeshSphereGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  meshInjectBug() = injectBug;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  Graph g;
  Node m; m.id = 1; m.type = "SphereMesh";
  m.params["Radius"] = 1.0f;
  m.params["Segments.x"] = 4.0f; m.params["Segments.y"] = 4.0f;
  g.nodes.push_back(m);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, 1);
  pg.cook(g, ctx, nullptr, 1);  // second cook: exercise buffer reuse

  const MTL::Buffer* vb = nullptr;
  const MTL::Buffer* ib = nullptr;
  uint32_t vc = 0, fc = 0;
  bool got = pg.debugCookedMesh(1, vb, vc, ib, fc);
  bool ok = got && vc == 30 && fc == 40;
  if (!got) std::printf("[selftest-mesh-sphere] FAIL: no cooked mesh\n");

  if (ok) {
    const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
    bool poleOk = posEq(v[0], 0, 1, 0) && posEq(v[4], 0, 1, 0) &&     // top pole row
                  posEq(v[20], 0, -1, 0) && posEq(v[24], 0, -1, 0);   // bottom pole row
    bool eqOk = posEq(v[10], 0, 0, 1) && posEq(v[11], 1, 0, 0) &&
                posEq(v[12], 0, 0, -1) && posEq(v[13], -1, 0, 0);
    bool nrmOk = nrmEq(v[10], 0, 0, 1) && nrmEq(v[11], 1, 0, 0);
    bool attrOk = nearf(v[10].Selection, 1.0f) && nearf(v[10].ColorRgb.x, 1.0f) &&
                  nearf(v[10].ColorRgb.y, 1.0f) && nearf(v[10].ColorRgb.z, 1.0f);
    ok = poleOk && eqOk && nrmOk && attrOk;
    std::printf("[selftest-mesh-sphere] verts=%u faces=%u eq0=(%.2f,%.2f,%.2f) eq1=(%.2f,%.2f,%.2f) "
                "poleOk=%d eqOk=%d nrmOk=%d attrOk=%d\n",
                vc, fc, v[10].Position.x, v[10].Position.y, v[10].Position.z, v[11].Position.x,
                v[11].Position.y, v[11].Position.z, poleOk, eqOk, nrmOk, attrOk);
  }

  meshInjectBug() = false;
  q->release();
  dev->release();
  pool->release();

  // --bite: -bug must exit NON-zero. injectBug corrupts verts[0] in the REAL cook -> ok false -> 1.
  std::printf("[selftest-mesh-sphere] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
