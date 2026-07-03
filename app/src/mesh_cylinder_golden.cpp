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

  // ---- Case 2 (P2 fix: Twist/Spin/RadiusOffset non-zero + CapSegments>0; angles off 90-degree grid).
  // Radius=0.5, RadiusOffset=0.25, Height=2, Rows=1, Columns=4, CapSegments=1, Spin=30, Twist=90,
  // Fill=360, BasePivot=0.5. Hand-derived from CylinderMesh.cs:
  //   upperRadius = 0.5+0.25 = 0.75 [:29]; radiusAngleFraction = 1/4*2PI = 90deg [:67];
  //   squeezeAngle = atan2(0.25, 2) = 0.1243550 rad [:70].
  //   counts: hull 10 verts / 8 tris [:50-51]; caps 2*(1*5+1)=12 verts / 2*((0)*8+4)=8 tris [:53-54]
  //     -> totals 22 / 16 [:56-57].
  //   HULL columnAngle = col*90deg + spin + twist*h + 180deg [:81]; p=(sin*a rowRadius, rowLevel, cos*rowRadius) [:87-89]:
  //     v0 (row0,col0): h=0, rowRadius=0.5, rowLevel=2*(0-0.5)=-1, angle=210deg
  //       -> (sin210*0.5, -1, cos210*0.5) = (-0.25, -1, -0.4330127)
  //     v1 (row0,col1): angle=300deg -> (-0.4330127, -1, 0.25)
  //     v5 (row1,col0): h=1, rowRadius=0.75, rowLevel=+1, angle=30+90+180=300deg
  //       -> (sin300*0.75, 1, cos300*0.75) = (-0.6495191, 1, 0.375)
  //   HULL normal v0 = (sin210*cos(sq), cos(-sq-PI/2), cos210*cos(sq))                 [:94-97]
  //       = (-0.4961389, -0.1240347, -0.8593378)  — bites squeezeAngle (RadiusOffset/Height).
  //   CAPS (capSegments=1 -> the single ring IS the center segment) [:140-248]:
  //     centerVertexIndex = 10 + (12/2)*(capIndex+1) - 1 -> v15 (lower), v21 (upper)   [:149]
  //     center verts = (0, capLevel, 0) + center, capLevel = ((0|1)-0.5)*2 = -/+1      [:145,:201]
  //     upper-cap ring col0: angle = 30 + twist*1 = 120deg (NO +PI) [:163];
  //       p = (-sin120*0.75, 1, -cos120*0.75) = (-0.6495191, 1, 0.375)                 [:168-170]
  //     cap fan faces col0: lower isReverse=true -> (v+1, v, center) = (11,10,15)      [:223-225]
  //       at faceIndex 0+8+0=8; upper isReverse=false -> (center, v, v+1) = (21,16,17) at 0+8+4=12.
  // A dropped twist leaves v5 at angle 210 (x=-0.375); a dropped spin snaps everything back to the
  // 90-degree grid; a dropped RadiusOffset shrinks row1/upper-cap radii 33% and zeroes the squeeze.
  {
    Graph g2;
    Node m2; m2.id = 2; m2.type = "CylinderMesh";
    m2.params["Radius"] = 0.5f; m2.params["RadiusOffset"] = 0.25f; m2.params["Height"] = 2.0f;
    m2.params["Rows"] = 1.0f; m2.params["Columns"] = 4.0f; m2.params["CapSegments"] = 1.0f;
    m2.params["Spin"] = 30.0f; m2.params["Twist"] = 90.0f; m2.params["Fill"] = 360.0f;
    m2.params["BasePivot"] = 0.5f;
    g2.nodes.push_back(m2);
    pg.cook(g2, ctx, nullptr, 2);

    const MTL::Buffer* vb2 = nullptr;
    const MTL::Buffer* ib2 = nullptr;
    uint32_t vc2 = 0, fc2 = 0;
    bool got2 = pg.debugCookedMesh(2, vb2, vc2, ib2, fc2);
    bool ok2 = got2 && vc2 == 22 && fc2 == 16;
    if (ok2) {
      const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb2)->contents();
      const SwTriIndex* f = (const SwTriIndex*)const_cast<MTL::Buffer*>(ib2)->contents();
      bool hullOk = posEq(v[0], -0.25f, -1.0f, -0.4330127f) &&
                    posEq(v[1], -0.4330127f, -1.0f, 0.25f) &&
                    posEq(v[5], -0.6495191f, 1.0f, 0.375f);
      bool nrmOk2 = nrmEq(v[0], -0.4961389f, -0.1240347f, -0.8593378f);
      bool capOk = posEq(v[15], 0.0f, -1.0f, 0.0f) && posEq(v[21], 0.0f, 1.0f, 0.0f) &&
                   posEq(v[16], -0.6495191f, 1.0f, 0.375f);
      bool capFOk = triEq(f[8], 11, 10, 15) && triEq(f[12], 21, 16, 17);
      ok2 = hullOk && nrmOk2 && capOk && capFOk;
      std::printf("[selftest-mesh-cylinder] case2 verts=%u faces=%u v5=(%.4f,%.4f,%.4f) "
                  "f8=(%d,%d,%d) hullOk=%d nrmOk=%d capOk=%d capFOk=%d\n",
                  vc2, fc2, v[5].Position.x, v[5].Position.y, v[5].Position.z, f[8].X, f[8].Y,
                  f[8].Z, hullOk, nrmOk2, capOk, capFOk);
    } else {
      std::printf("[selftest-mesh-cylinder] case2 FAIL: got=%d verts=%u faces=%u\n", got2, vc2, fc2);
    }
    ok = ok && ok2;
  }

  meshInjectBug() = false;
  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-mesh-cylinder] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
