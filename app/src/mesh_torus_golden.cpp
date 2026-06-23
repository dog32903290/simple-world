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

  meshInjectBug() = false;
  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-mesh-torus] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
