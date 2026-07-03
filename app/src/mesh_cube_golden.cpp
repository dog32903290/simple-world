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
    // P2 fix: non-front sides were previously unasserted (sideRot=identity only). Hand-derived from
    // the _sides table (CubeMesh.cs:209-289) + position math (:115-116):
    //   Right (sideIndex 1, verts 4..7): sideRot yaw=PI/2 -> (x,y,z)->(z,y,-x); p+offset=(c-.5,r-.5,.5)
    //     -> (0.5, r-0.5, 0.5-c). v4(c0,r0)=(0.5,-0.5,0.5) ; v6(c1,r0)=(0.5,-0.5,-0.5).
    //     Normal = TransformNormal(ForwardLH, sideRot) = (1,0,0).
    //   Fifth side (sideIndex 4, verts 16..19): sideRot pitch=PI/2 -> (x,y,z)->(x,-z,y)
    //     -> (c-0.5, -0.5, r-0.5). v16(c0,r0)=(-0.5,-0.5,-0.5) ; v19(c1,r1)=(0.5,-0.5,0.5).
    //     Normal = (0,-1,0). (TiXL labels this side "Top"; its pitch=+PI/2 maps depth +0.5 to y=-0.5
    //     — the formula, not the label, is the oracle.)
    bool sideOk = posEq(v[4], 0.5f, -0.5f, 0.5f) && posEq(v[6], 0.5f, -0.5f, -0.5f) &&
                  nrmEq(v[4], 1, 0, 0) &&
                  posEq(v[16], -0.5f, -0.5f, -0.5f) && posEq(v[19], 0.5f, -0.5f, 0.5f) &&
                  nrmEq(v[16], 0, -1, 0);
    bool fOk = triEq(f[0], 0, 2, 1) && triEq(f[1], 2, 3, 1);
    bool attrOk = nearf(v[0].Selection, 1.0f) && nearf(v[0].ColorRgb.x, 1.0f);
    ok = vOk && nrmOk && sideOk && fOk && attrOk;
    std::printf("[selftest-mesh-cube] verts=%u faces=%u v0=(%.2f,%.2f,%.2f) v3=(%.2f,%.2f,%.2f) "
                "f0=(%d,%d,%d) vOk=%d nrmOk=%d sideOk=%d fOk=%d attrOk=%d\n",
                vc, fc, v[0].Position.x, v[0].Position.y, v[0].Position.z, v[3].Position.x,
                v[3].Position.y, v[3].Position.z, f[0].X, f[0].Y, f[0].Z, vOk, nrmOk, sideOk, fOk,
                attrOk);
  }

  // ---- Case 2 (P2 fix: Scale/Stretch/Pivot/Rotation/Center off identity + non-front sides).
  // Segments=(1,1,1), Scale=0.7, Stretch=(1,2,1.5), Pivot=(0.5,0,0), Rotation=(0,30,0) [deg X,Y,Z],
  // Center=(0.1,0.2,0.3). Hand-derived from CubeMesh.cs:115-116,:120:
  //   position = (TransformNormal(p+offset, sideRot) + pivot) * stretch * scale        [:115]
  //   position = TransformNormal(position, cubeRot(yaw=30deg)) ; final = position + center
  //   cubeRot yaw=30 row-vec: (x*c + z*s, y, -x*s + z*c), c=0.8660254, s=0.5.
  //   Front v0 (c0,r0): ps=(-0.5,-0.5,0.5) +pv=(0,-0.5,0.5) *st*sc=(0,-0.7,0.525)
  //     -> rot (0.2625, -0.7, 0.4546633) +C = (0.3625, -0.5, 0.7546633)
  //   Front v3 (c1,r1): ps=(0.5,0.5,0.5) +pv=(1,0.5,0.5) *st*sc=(0.7,0.7,0.525)
  //     -> rot (0.86871778, 0.7, 0.10466330) +C = (0.9687178, 0.9, 0.4046633)
  //   Right v6 (c1,r0): sideRot->(0.5,-0.5,-0.5) +pv=(1,-0.5,-0.5) *st*sc=(0.7,-0.7,-0.525)
  //     -> rot (0.3437178, -0.7, -0.8046633) +C = (0.4437178, -0.5, -0.5046633)
  //   Side4 v16 (c0,r0): sideRot->(-0.5,-0.5,-0.5) +pv=(0,-0.5,-0.5) *st*sc=(0,-0.7,-0.525)
  //     -> rot (-0.2625, -0.7, -0.4546633) +C = (-0.1625, -0.5, -0.1546633)
  //   Right normal = TransformNormal((1,0,0), cubeRot) = (0.8660254, 0, -0.5)          [:89]
  // A dropped Scale/Stretch component, a pivot added after (instead of before) the stretch multiply,
  // or a skipped cubeRot pass each diverges at these probes.
  {
    Graph g2;
    Node m2; m2.id = 2; m2.type = "CubeMesh";
    m2.params["Segments.x"] = 1.0f; m2.params["Segments.y"] = 1.0f; m2.params["Segments.z"] = 1.0f;
    m2.params["Scale"] = 0.7f;
    m2.params["Stretch.x"] = 1.0f; m2.params["Stretch.y"] = 2.0f; m2.params["Stretch.z"] = 1.5f;
    m2.params["Pivot.x"] = 0.5f;
    m2.params["Rotation.y"] = 30.0f;
    m2.params["Center.x"] = 0.1f; m2.params["Center.y"] = 0.2f; m2.params["Center.z"] = 0.3f;
    g2.nodes.push_back(m2);
    pg.cook(g2, ctx, nullptr, 2);

    const MTL::Buffer* vb2 = nullptr;
    const MTL::Buffer* ib2 = nullptr;
    uint32_t vc2 = 0, fc2 = 0;
    bool got2 = pg.debugCookedMesh(2, vb2, vc2, ib2, fc2);
    bool ok2 = got2 && vc2 == 24 && fc2 == 12;
    if (ok2) {
      const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb2)->contents();
      bool vOk2 = posEq(v[0], 0.3625f, -0.5f, 0.7546633f) &&
                  posEq(v[3], 0.9687178f, 0.9f, 0.4046633f) &&
                  posEq(v[6], 0.4437178f, -0.5f, -0.5046633f) &&
                  posEq(v[16], -0.1625f, -0.5f, -0.1546633f);
      bool nOk2 = nrmEq(v[4], 0.8660254f, 0.0f, -0.5f);
      ok2 = vOk2 && nOk2;
      std::printf("[selftest-mesh-cube] case2 v3=(%.4f,%.4f,%.4f) v16=(%.4f,%.4f,%.4f) vOk=%d nOk=%d\n",
                  v[3].Position.x, v[3].Position.y, v[3].Position.z, v[16].Position.x,
                  v[16].Position.y, v[16].Position.z, vOk2, nOk2);
    } else {
      std::printf("[selftest-mesh-cube] case2 FAIL: got=%d verts=%u faces=%u\n", got2, vc2, fc2);
    }
    ok = ok && ok2;
  }

  meshInjectBug() = false;
  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-mesh-cube] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
