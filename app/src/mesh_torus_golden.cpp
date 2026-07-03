// mesh_torus_golden — --selftest-mesh-torus. CPU-READBACK golden for TorusMesh (4th cook flow).
//
// CASE: Radius(major)=1, Thickness(tube)=0.5, Segments=(4,4), Spin=(0,0), Fill=(360,360).
//   radiusSegments=4+1=5, tubeSegments=4+1=5.
//   verticesCount = tubeSegments*radiusSegments = 25 ; faceCount = (5-1)*(5-1)*2 = 32.
//   fillRadius=tubeFill=1 ; tubeAngleFraction=(1/4)*2PI=PI/2 ; radiusAngleFraction=PI/2.
//   tubeIndex=0: tubeAngle=0 -> tubeX=sin0*0.5=0, tubeY=cos0*0.5=0.5.
//     radiusIndex=0 (vi=0): radiusAngle=0    -> p=(sin0*(0+1), cos0*(0+1), 0.5) = (0, 1, 0.5)
//     radiusIndex=1 (vi=1): radiusAngle=PI/2 -> p=(sin(PI/2)*1, cos(PI/2)*1, 0.5) = (1, 0, 0.5)
//     radiusIndex=2 (vi=2): radiusAngle=PI   -> p=(0, -1, 0.5)
//   tubeIndex=1 (vi base=5): tubeAngle=PI/2 -> tubeX=sin(PI/2)*0.5=0.5, tubeY=cos(PI/2)*0.5=0.
//     radiusIndex=0 (vi=5): radiusAngle=0   -> p=(sin0*(0.5+1), cos0*(0.5+1), 0) = (0, 1.5, 0)
//   face0 = (0, 1, radiusSegments=5) ; face1 = (5, 1, 6).
// Golden asserts EXACT positions + counts + first face winding (TBN computed but not asserted — its
// hand-derivation is brittle; scope is the position/topology spine, the visible parity surface).
//
// CASE 2 (non-identity: P2 fix — Radius=1/Spin=0/Fill=360 above are identity for the param wiring):
//   Radius=2, Thickness=0.5, Segments=(4,4), Spin=(90,90), Fill=(180,180).
//   Hand-derived from TiXL TorusMesh.cs @395c4c55 (:29-34 spin/fill, :51-52 angle fractions,
//   :56-59 tubeAngle/tubePosition, :74-78 radiusAngle/p):
//     radiusSpin=spinMinor=pi/2 (:29-30); fillRadius=tubeFill=0.5 (:33-34);
//     tubeAngleFraction=radiusAngleFraction=0.5/4*2pi=pi/4 (:51-52).
//   tubeIndex=0: tubeAngle=0+pi/2 -> tubeX=sin(pi/2)*0.5=0.5, tubeY=cos(pi/2)*0.5=0.
//     vi=0: radiusAngle=0+pi/2    -> p=(sin(pi/2)*(0.5+2), cos(pi/2)*2.5, 0)       = (2.5, 0, 0)
//     vi=1: radiusAngle=3pi/4     -> p=(0.70710678*2.5, -0.70710678*2.5, 0)
//                                   = (1.76776695, -1.76776695, 0)   # python: sin(3*pi/4)*2.5
//     vi=2: radiusAngle=pi        -> p=(0, -2.5, 0)
//   tubeIndex=1 (vi=5): tubeAngle=pi/4+pi/2=3pi/4 -> tubeX=sin(3pi/4)*0.5=0.35355339,
//     tubeY=cos(3pi/4)*0.5=-0.35355339; radiusAngle=pi/2
//     -> p=(1*(0.35355339+2), 0, -0.35355339) = (2.35355339, 0, -0.35355339)
//   Counts unchanged (fill does not change vertex/face counts, :39-40): vc=25, fc=32.
//   A formula error in Radius plumbing, Spin offset, or Fill fraction diverges every one of these pins.
//
// injectBug -> meshInjectBug() -> corrupts verts[0] in the REAL cook -> RED (both cases assert v[0]).
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
bool triEq(const SwTriIndex& t, int x, int y, int z) { return t.X == x && t.Y == y && t.Z == z; }
}  // namespace

int runMeshTorusGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  meshInjectBug() = injectBug;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, nullptr, q, 64, 64);

  Graph g;
  Node m; m.id = 1; m.type = "TorusMesh";
  m.params["Radius"] = 1.0f; m.params["Thickness"] = 0.5f;
  m.params["Segments.x"] = 4.0f; m.params["Segments.y"] = 4.0f;
  m.params["Spin.x"] = 0.0f; m.params["Spin.y"] = 0.0f;
  m.params["Fill.x"] = 360.0f; m.params["Fill.y"] = 360.0f;
  g.nodes.push_back(m);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, 1);
  pg.cook(g, ctx, nullptr, 1);

  const MTL::Buffer* vb = nullptr;
  const MTL::Buffer* ib = nullptr;
  uint32_t vc = 0, fc = 0;
  bool got = pg.debugCookedMesh(1, vb, vc, ib, fc);
  bool ok = got && vc == 25 && fc == 32;
  if (!got) std::printf("[selftest-mesh-torus] FAIL: no cooked mesh\n");

  if (ok) {
    const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
    const SwTriIndex* f = (const SwTriIndex*)const_cast<MTL::Buffer*>(ib)->contents();
    bool vOk = posEq(v[0], 0, 1, 0.5f) && posEq(v[1], 1, 0, 0.5f) && posEq(v[2], 0, -1, 0.5f) &&
               posEq(v[5], 0, 1.5f, 0);
    bool fOk = triEq(f[0], 0, 1, 5) && triEq(f[1], 5, 1, 6);
    bool attrOk = nearf(v[0].Selection, 1.0f) && nearf(v[0].ColorRgb.x, 1.0f) &&
                  nearf(v[0].ColorRgb.y, 1.0f) && nearf(v[0].ColorRgb.z, 1.0f);
    ok = vOk && fOk && attrOk;
    std::printf("[selftest-mesh-torus] verts=%u faces=%u v0=(%.2f,%.2f,%.2f) v5=(%.2f,%.2f,%.2f) "
                "f0=(%d,%d,%d) vOk=%d fOk=%d attrOk=%d\n",
                vc, fc, v[0].Position.x, v[0].Position.y, v[0].Position.z, v[5].Position.x,
                v[5].Position.y, v[5].Position.z, f[0].X, f[0].Y, f[0].Z, vOk, fOk, attrOk);
  }

  // ── CASE 2: non-identity params (Radius=2, Spin=(90,90), Fill=(180,180)) — the P2 fix.
  //    Expected values hand-derived from TiXL TorusMesh.cs (see header). ──
  if (ok) {
    PointGraph pg2(dev, nullptr, q, 64, 64);
    Graph g2;
    Node m2; m2.id = 1; m2.type = "TorusMesh";
    m2.params["Radius"] = 2.0f; m2.params["Thickness"] = 0.5f;
    m2.params["Segments.x"] = 4.0f; m2.params["Segments.y"] = 4.0f;
    m2.params["Spin.x"] = 90.0f; m2.params["Spin.y"] = 90.0f;
    m2.params["Fill.x"] = 180.0f; m2.params["Fill.y"] = 180.0f;
    g2.nodes.push_back(m2);

    pg2.cook(g2, ctx, nullptr, 1);
    const MTL::Buffer* vb2 = nullptr;
    const MTL::Buffer* ib2 = nullptr;
    uint32_t vc2 = 0, fc2 = 0;
    bool got2 = pg2.debugCookedMesh(1, vb2, vc2, ib2, fc2);
    bool pass2 = got2 && vc2 == 25 && fc2 == 32;
    if (pass2) {
      const SwVertex* v2 = (const SwVertex*)const_cast<MTL::Buffer*>(vb2)->contents();
      // TorusMesh.cs:74-78 with radiusSpin=spinMinor=pi/2, fractions pi/4 (hand-derived, header).
      bool vOk2 = posEq(v2[0], 2.5f, 0.0f, 0.0f) &&
                  posEq(v2[1], 1.76776695f, -1.76776695f, 0.0f) &&
                  posEq(v2[2], 0.0f, -2.5f, 0.0f) &&
                  posEq(v2[5], 2.35355339f, 0.0f, -0.35355339f);
      pass2 = vOk2;
      std::printf("[selftest-mesh-torus] case2 spin/fill: v0=(%.4f,%.4f,%.4f) v1=(%.4f,%.4f,%.4f) "
                  "v5=(%.4f,%.4f,%.4f) vOk=%d\n",
                  v2[0].Position.x, v2[0].Position.y, v2[0].Position.z, v2[1].Position.x,
                  v2[1].Position.y, v2[1].Position.z, v2[5].Position.x, v2[5].Position.y,
                  v2[5].Position.z, vOk2);
    } else {
      std::printf("[selftest-mesh-torus] case2 FAIL: no cooked mesh (got=%d vc=%u fc=%u)\n", got2,
                  vc2, fc2);
    }
    ok = ok && pass2;
  }

  meshInjectBug() = false;
  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-mesh-torus] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
