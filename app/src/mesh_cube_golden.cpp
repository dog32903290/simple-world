// mesh_cube_golden — --selftest-mesh-cube. CPU-READBACK golden for CubeMesh (4th cook flow).
//
// CASE (defaults): Segments=(1,1,1), Scale=1, Stretch=(1,1,1), Pivot=(0,0,0), Center=0, Rotation=0,
//   Margin=0, TexCoord=0 (Standard).
//   xSeg=ySeg=zSeg = 1+1 = 2. verticesCount = (y*x + y*z + x*z)*2 = (4+4+4)*2 = 24.
//   faceCount = ((1*1)+(1*1)+(1*1))*2*2 = 12.
//   FRONT side (index 0, sideRotation=0, columnAxis=X row=Y depth=Z): columnCount=rowCount=2,
//     step=1, depthScale=1, offset=-0.5. sideRot=identity, cubeRot=identity.
//     position = (TransformNormal(p+offset, I) + 0) * 1 * 1, then *I  => p - 0.5.
//       (col0,row0): p=(0,0,1) -> (-0.5,-0.5,0.5)   = verts[0]
//       (col0,row1): p=(0,1,1) -> (-0.5, 0.5,0.5)   = verts[1]
//       (col1,row0): p=(1,0,1) -> ( 0.5,-0.5,0.5)   = verts[2]
//       (col1,row1): p=(1,1,1) -> ( 0.5, 0.5,0.5)   = verts[3]
//     Front normal = TransformNormal(ForwardLH=(0,0,1), I*I) = (0,0,1).
//     faces (faceIndex 0): (0,2,1) , (2,3,1).
//   Front face all sit at z=+0.5 — the +Z face of a unit cube centered at origin. Good closed form.
//
// injectBug -> meshInjectBug() -> corrupts verts[0] in the REAL cook -> RED.
#include <cmath>
#include <cstdint>
#include <cstdio>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/mesh_op_registry.h"
#include "runtime/point_graph.h"
#include "runtime/sw_mesh.h"

namespace sw {
namespace {
bool nearf(float a, float b) { return std::fabs(a - b) < 1e-5f; }
bool posEq(const SwVertex& v, float x, float y, float z) {
  return nearf(v.Position.x, x) && nearf(v.Position.y, y) && nearf(v.Position.z, z);
}
bool nrmEq(const SwVertex& v, float x, float y, float z) {
  return nearf(v.Normal.x, x) && nearf(v.Normal.y, y) && nearf(v.Normal.z, z);
}
bool triEq(const SwTriIndex& t, int x, int y, int z) { return t.X == x && t.Y == y && t.Z == z; }
}  // namespace

int runMeshCubeGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  meshInjectBug() = injectBug;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, nullptr, q, 64, 64);

  Graph g;
  Node m; m.id = 1; m.type = "CubeMesh";
  m.params["Segments.x"] = 1.0f; m.params["Segments.y"] = 1.0f; m.params["Segments.z"] = 1.0f;
  m.params["Scale"] = 1.0f;
  m.params["Stretch.x"] = 1.0f; m.params["Stretch.y"] = 1.0f; m.params["Stretch.z"] = 1.0f;
  g.nodes.push_back(m);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, 1);
  pg.cook(g, ctx, nullptr, 1);

  const MTL::Buffer* vb = nullptr;
  const MTL::Buffer* ib = nullptr;
  uint32_t vc = 0, fc = 0;
  bool got = pg.debugCookedMesh(1, vb, vc, ib, fc);
  bool ok = got && vc == 24 && fc == 12;
  if (!got) std::printf("[selftest-mesh-cube] FAIL: no cooked mesh\n");

  if (ok) {
    const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
    const SwTriIndex* f = (const SwTriIndex*)const_cast<MTL::Buffer*>(ib)->contents();
    bool vOk = posEq(v[0], -0.5f, -0.5f, 0.5f) && posEq(v[1], -0.5f, 0.5f, 0.5f) &&
               posEq(v[2], 0.5f, -0.5f, 0.5f) && posEq(v[3], 0.5f, 0.5f, 0.5f);
    bool nrmOk = nrmEq(v[0], 0, 0, 1);  // Front face normal = ForwardLH = (0,0,1)
    bool fOk = triEq(f[0], 0, 2, 1) && triEq(f[1], 2, 3, 1);
    bool attrOk = nearf(v[0].Selection, 1.0f) && nearf(v[0].ColorRgb.x, 1.0f);
    ok = vOk && nrmOk && fOk && attrOk;
    std::printf("[selftest-mesh-cube] verts=%u faces=%u v0=(%.2f,%.2f,%.2f) v3=(%.2f,%.2f,%.2f) "
                "f0=(%d,%d,%d) vOk=%d nrmOk=%d fOk=%d attrOk=%d\n",
                vc, fc, v[0].Position.x, v[0].Position.y, v[0].Position.z, v[3].Position.x,
                v[3].Position.y, v[3].Position.z, f[0].X, f[0].Y, f[0].Z, vOk, nrmOk, fOk, attrOk);
  }

  meshInjectBug() = false;
  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-mesh-cube] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
