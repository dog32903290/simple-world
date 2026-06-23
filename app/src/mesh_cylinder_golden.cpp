// mesh_cylinder_golden — --selftest-mesh-cylinder. CPU-READBACK golden for CylinderMesh.
//
// CASE (hull only, CapSegments=0): Radius=0.5, RadiusOffset=0, Height=1, Rows=1, Columns=4,
//   CapSegments=0, Spin=0, Twist=0, Fill=360, Center=0, BasePivot=0.5, Rotation=0.
//   vertexHullColumns = columns+1 = 5 ; addCaps=false.
//   hullVertices = (rows+1)*vhc = 2*5 = 10 ; hullTris = rows*columns*2 = 8. totals: 10 verts / 8 faces.
//   radiusAngleFraction = (fill/360)/(vhc-1)*2PI = 1/4*2PI = PI/2 ; squeezeAngle = atan2(0,1)=0.
//   rowIndex=0: h=0 -> rowRadius=0.5, rowLevel = 1*(0-0.5) = -0.5.
//     col0: columnAngle = 0+0+0+PI = PI       -> p=(sin(PI)*0.5, -0.5, cos(PI)*0.5)  = (0, -0.5, -0.5)
//     col1: columnAngle = PI/2+PI = 3PI/2     -> p=(sin(3PI/2)*0.5, -0.5, cos(3PI/2)*0.5) = (-0.5, -0.5, 0)
//     col2: columnAngle = PI+PI = 2PI         -> p=(0, -0.5, 0.5)
//   rowIndex=1 (vi base=5): h=1 -> rowLevel = 1*(1-0.5) = 0.5.
//     col0 (vi=5): columnAngle=PI -> p=(0, 0.5, -0.5)
//   face0 (row0,col0, not flipped): (0,1,5) ; face1: (5,1,6).
//   Hull normals at squeeze=0: normal0=(sin(a)*1, cos(-PI/2)=0, cos(a)*1); col0(a=PI): (0,0,-1).
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

int runMeshCylinderGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  meshInjectBug() = injectBug;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, nullptr, q, 64, 64);

  Graph g;
  Node m; m.id = 1; m.type = "CylinderMesh";
  m.params["Radius"] = 0.5f; m.params["RadiusOffset"] = 0.0f; m.params["Height"] = 1.0f;
  m.params["Rows"] = 1.0f; m.params["Columns"] = 4.0f; m.params["CapSegments"] = 0.0f;
  m.params["Spin"] = 0.0f; m.params["Twist"] = 0.0f; m.params["Fill"] = 360.0f;
  m.params["BasePivot"] = 0.5f;
  g.nodes.push_back(m);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, 1);
  pg.cook(g, ctx, nullptr, 1);

  const MTL::Buffer* vb = nullptr;
  const MTL::Buffer* ib = nullptr;
  uint32_t vc = 0, fc = 0;
  bool got = pg.debugCookedMesh(1, vb, vc, ib, fc);
  bool ok = got && vc == 10 && fc == 8;
  if (!got) std::printf("[selftest-mesh-cylinder] FAIL: no cooked mesh\n");

  if (ok) {
    const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
    const SwTriIndex* f = (const SwTriIndex*)const_cast<MTL::Buffer*>(ib)->contents();
    bool vOk = posEq(v[0], 0, -0.5f, -0.5f) && posEq(v[1], -0.5f, -0.5f, 0) &&
               posEq(v[2], 0, -0.5f, 0.5f) && posEq(v[5], 0, 0.5f, -0.5f);
    bool fOk = triEq(f[0], 0, 1, 5) && triEq(f[1], 5, 1, 6);
    bool nrmOk = nrmEq(v[0], 0, 0, -1);  // col0: normal=(sin(PI), 0, cos(PI))=(0,0,-1)
    bool attrOk = nearf(v[0].Selection, 1.0f) && nearf(v[0].ColorRgb.x, 1.0f);
    ok = vOk && fOk && nrmOk && attrOk;
    std::printf("[selftest-mesh-cylinder] verts=%u faces=%u v0=(%.2f,%.2f,%.2f) v5=(%.2f,%.2f,%.2f) "
                "f0=(%d,%d,%d) vOk=%d fOk=%d nrmOk=%d attrOk=%d\n",
                vc, fc, v[0].Position.x, v[0].Position.y, v[0].Position.z, v[5].Position.x,
                v[5].Position.y, v[5].Position.z, f[0].X, f[0].Y, f[0].Z, vOk, fOk, nrmOk, attrOk);
  }

  meshInjectBug() = false;
  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-mesh-cylinder] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
