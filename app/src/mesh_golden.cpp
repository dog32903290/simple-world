// mesh_golden — --selftest-mesh-ngon + --selftest-mesh-quad. CPU-READBACK goldens for the 4th cook
// flow (MeshBuffers): build a single mesh-generator node as the cook terminal, run PointGraph::cook
// (the cookMeshNode branch sizes the owned vertex+index pair + runs the leaf), read the two buffers
// back via debugCookedMesh()->contents()+memcpy, and assert EXACT vertex positions + index triples +
// counts against the closed-form TiXL math (NGonMesh.cs / QuadMesh.cs). NO GPU draw / camera — the
// Mesh currency is CPU-self-sufficient.
//
// injectBug routes through meshInjectBug() so the RED case corrupts the REAL cook output (a vertex
// pos / index triple), not the expected value — teeth on the actual op path.
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
bool triEq(const SwTriIndex& t, int x, int y, int z) { return t.X == x && t.Y == y && t.Z == z; }

}  // namespace

// NGonMesh golden: Segments=4, Radius=1, Stretch=(1,1), Center=0, Rotation=0, TextureMode=Planar.
// Exact trig at 0/90/180/270 -> verts (0,0,0),(0,1,0),(1,0,0),(0,-1,0),(-1,0,0); 4 faces (triangle
// fan): (0,2,1),(0,3,2),(0,4,3),(0,1,4). The (i+2)>segments wraparound makes the last face close to 1.
int runMeshNGonGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  meshInjectBug() = injectBug;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  Graph g;
  Node m; m.id = 1; m.type = "NGonMesh";
  m.params["Segments"] = 4.0f; m.params["Radius"] = 1.0f;
  m.params["Stretch.x"] = 1.0f; m.params["Stretch.y"] = 1.0f;
  m.params["TextureMode"] = 0.0f;  // Planar
  g.nodes.push_back(m);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, 1);
  pg.cook(g, ctx, nullptr, 1);  // second cook: exercise buffer reuse (no realloc, same result)

  const MTL::Buffer* vb = nullptr;
  const MTL::Buffer* ib = nullptr;
  uint32_t vc = 0, fc = 0;
  bool got = pg.debugCookedMesh(1, vb, vc, ib, fc);
  bool ok = got && vc == 5 && fc == 4;
  if (!got) std::printf("[selftest-mesh-ngon] FAIL: no cooked mesh\n");

  if (ok) {
    const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
    const SwTriIndex* f = (const SwTriIndex*)const_cast<MTL::Buffer*>(ib)->contents();
    bool vOk = posEq(v[0], 0, 0, 0) && posEq(v[1], 0, 1, 0) && posEq(v[2], 1, 0, 0) &&
               posEq(v[3], 0, -1, 0) && posEq(v[4], -1, 0, 0);
    bool fOk = triEq(f[0], 0, 2, 1) && triEq(f[1], 0, 3, 2) && triEq(f[2], 0, 4, 3) &&
               triEq(f[3], 0, 1, 4);
    // Selection=1 + ColorRgb=(1,1,1) on the center vertex (field-default parity probe).
    bool attrOk = nearf(v[0].Selection, 1.0f) && nearf(v[0].ColorRgb.x, 1.0f) &&
                  nearf(v[0].ColorRgb.y, 1.0f) && nearf(v[0].ColorRgb.z, 1.0f);
    ok = vOk && fOk && attrOk;
    std::printf("[selftest-mesh-ngon] verts=%u faces=%u v1=(%.2f,%.2f,%.2f) f3=(%d,%d,%d) "
                "vOk=%d fOk=%d attrOk=%d\n",
                vc, fc, v[1].Position.x, v[1].Position.y, v[1].Position.z, f[3].X, f[3].Y, f[3].Z,
                vOk, fOk, attrOk);
  }

  meshInjectBug() = false;
  q->release();
  dev->release();
  pool->release();

  // Harness convention (run_all_selftests.sh --bite): the -bug variant must exit NON-zero. injectBug
  // corrupts vertex[1] in the REAL cook -> ok is false -> return 1 (the tooth bites). No inversion.
  std::printf("[selftest-mesh-ngon] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// QuadMesh golden: Segments=(1,1), Scale=1, Stretch=(1,1), Pivot=(0.5,0.5), Center=0, Rotation=0.
// columns=rows=2 -> 4 verts, 2 faces. offset=0, step=1. Vertex layout vertexIndex=row+col*rows:
//   v0=(c0,r0)=(0,0,0); v1=(c0,r1)=(0,1,0); v2=(c1,r0)=(1,0,0); v3=(c1,r1)=(1,1,0).
// Faces (cell at col0,row0, v=0,rows=2): (0,2,1) + (2,3,1).
int runMeshQuadGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  meshInjectBug() = injectBug;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  Graph g;
  Node m; m.id = 1; m.type = "QuadMesh";
  m.params["Segments.x"] = 1.0f; m.params["Segments.y"] = 1.0f;
  m.params["Scale"] = 1.0f;
  m.params["Stretch.x"] = 1.0f; m.params["Stretch.y"] = 1.0f;
  m.params["Pivot.x"] = 0.5f; m.params["Pivot.y"] = 0.5f;
  g.nodes.push_back(m);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, 1);
  pg.cook(g, ctx, nullptr, 1);

  const MTL::Buffer* vb = nullptr;
  const MTL::Buffer* ib = nullptr;
  uint32_t vc = 0, fc = 0;
  bool got = pg.debugCookedMesh(1, vb, vc, ib, fc);
  bool ok = got && vc == 4 && fc == 2;
  if (!got) std::printf("[selftest-mesh-quad] FAIL: no cooked mesh\n");

  if (ok) {
    const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
    const SwTriIndex* f = (const SwTriIndex*)const_cast<MTL::Buffer*>(ib)->contents();
    // offset = stretch*scale*(pivot-0.5) = 1*1*0 = 0; step = scale*stretch/(cols-1) = 1.
    bool vOk = posEq(v[0], 0, 0, 0) && posEq(v[1], 0, 1, 0) && posEq(v[2], 1, 0, 0) &&
               posEq(v[3], 1, 1, 0);
    bool fOk = triEq(f[0], 0, 2, 1) && triEq(f[1], 2, 3, 1);
    bool attrOk = nearf(v[0].Selection, 1.0f) && nearf(v[3].ColorRgb.x, 1.0f) &&
                  nearf(v[3].ColorRgb.y, 1.0f) && nearf(v[3].ColorRgb.z, 1.0f);
    ok = vOk && fOk && attrOk;
    std::printf("[selftest-mesh-quad] verts=%u faces=%u f0=(%d,%d,%d) f1=(%d,%d,%d) "
                "vOk=%d fOk=%d attrOk=%d\n",
                vc, fc, f[0].X, f[0].Y, f[0].Z, f[1].X, f[1].Y, f[1].Z, vOk, fOk, attrOk);
  }

  meshInjectBug() = false;
  q->release();
  dev->release();
  pool->release();

  // Harness convention (--bite): the -bug variant must exit NON-zero. injectBug corrupts face[0]'s
  // index triple in the REAL cook -> ok is false -> return 1 (the tooth bites). No inversion.
  std::printf("[selftest-mesh-quad] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
