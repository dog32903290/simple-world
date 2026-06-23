// mesh_icosahedron_golden — --selftest-mesh-icosahedron. CPU-READBACK golden for IcosahedronMesh.
//
// CASE (defaults): Subdivisions=0, Spherical=Off, Strength=1, Shading=0/Flat, Scale=1, Stretch=(1,1),
//   Pivot=0, Center=0, Rotation=0.
//   Flat-split base icosahedron: 60 verts / 20 faces, tri i = (3i, 3i+1, 3i+2).
//   ★ Even at Rotation=0 the mesh is rotated by Rz(-tiltAngle), tiltAngle=atan(1/phi). This rotates
//     baseVertices[0]=normalize(-1,phi,0) to EXACTLY (0,1,0) (hand-derived: the canonical "vertex up"
//     tilt). So verts[0] = (0, 1, 0).
//   INVARIANT (load-bearing): every vertex lies on the UNIT SPHERE (base verts normalized * scale 1 *
//     rotation preserves length) -> |pos| == 1. Catches scale / transform / normalize regressions.
//   UV (Faces mode 0): verts[0] uv = _baseUvs[0] = (0.5, 1.0).
//   Flat normal of face0 = normalize(cross(v1-v0, v2-v0)) (cross-checked from the readback).
//
// injectBug -> meshInjectBug() -> corrupts verts[0] in the REAL cook -> RED (also breaks |pos|==1).
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
bool nearf(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) < eps; }
bool posEq(const SwVertex& v, float x, float y, float z) {
  return nearf(v.Position.x, x) && nearf(v.Position.y, y) && nearf(v.Position.z, z);
}
float plen(const SwVertex& v) {
  return std::sqrt(v.Position.x * v.Position.x + v.Position.y * v.Position.y +
                   v.Position.z * v.Position.z);
}
}  // namespace

int runMeshIcosahedronGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  meshInjectBug() = injectBug;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, nullptr, q, 64, 64);

  Graph g;
  Node m; m.id = 1; m.type = "IcosahedronMesh";
  m.params["Subdivisions"] = 0.0f; m.params["Spherical"] = 0.0f; m.params["Strength"] = 1.0f;
  m.params["Scale"] = 1.0f; m.params["Stretch.x"] = 1.0f; m.params["Stretch.y"] = 1.0f;
  m.params["Shading"] = 0.0f;
  g.nodes.push_back(m);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, 1);
  pg.cook(g, ctx, nullptr, 1);

  const MTL::Buffer* vb = nullptr;
  const MTL::Buffer* ib = nullptr;
  uint32_t vc = 0, fc = 0;
  bool got = pg.debugCookedMesh(1, vb, vc, ib, fc);
  bool ok = got && vc == 60 && fc == 20;
  if (!got) std::printf("[selftest-mesh-icosahedron] FAIL: no cooked mesh\n");

  if (ok) {
    const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
    const SwTriIndex* f = (const SwTriIndex*)const_cast<MTL::Buffer*>(ib)->contents();
    bool v0Ok = posEq(v[0], 0, 1, 0);                          // tilt puts baseVertices[0] straight up
    bool sphereOk = true;
    for (uint32_t i = 0; i < vc; ++i)
      if (!nearf(plen(v[i]), 1.0f, 1e-3f)) { sphereOk = false; break; }
    bool idxOk = true;
    for (uint32_t i = 0; i < fc; ++i)
      if (f[i].X != (int)(3 * i) || f[i].Y != (int)(3 * i + 1) || f[i].Z != (int)(3 * i + 2)) {
        idxOk = false; break;
      }
    bool uvOk = nearf(v[0].Texcoord.x, 0.5f) && nearf(v[0].Texcoord.y, 1.0f);
    bool attrOk = nearf(v[0].Selection, 1.0f) && nearf(v[0].ColorRgb.x, 1.0f);
    ok = v0Ok && sphereOk && idxOk && uvOk && attrOk;
    std::printf("[selftest-mesh-icosahedron] verts=%u faces=%u v0=(%.3f,%.3f,%.3f) |v30|=%.4f "
                "v0Ok=%d sphereOk=%d idxOk=%d uvOk=%d attrOk=%d\n",
                vc, fc, v[0].Position.x, v[0].Position.y, v[0].Position.z, plen(v[30]), v0Ok,
                sphereOk, idxOk, uvOk, attrOk);
  }

  meshInjectBug() = false;
  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-mesh-icosahedron] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
