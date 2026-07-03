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
bool tanEq(const SwVertex& v, float x, float y, float z) {
  return nearf(v.Tangent.x, x) && nearf(v.Tangent.y, y) && nearf(v.Tangent.z, z);
}

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

  // ---- Case 2 (P2 fix: parameter wiring off identity — Radius/Stretch/Rotation/Center biting).
  // Segments=4, Radius=0.7, Stretch=(1.5,0.5), Rotation=(0,0,30), Center=(0.1,0.2,0.3).
  // Hand-derived from NGonMesh.cs: p = (R*sin(phi)*st.X, R*cos(phi)*st.Y, 0)  [NGonMesh.cs:87-89];
  // posRotated = TransformNormal(p, CreateFromYawPitchRoll(0,0,roll=30deg))    [:33-39,:109]
  //   = Rz(30) row-vector: (x*c - y*s, x*s + y*c, z), c=cos30=0.8660254, s=0.5;
  // Position = posRotated + center [:112]; vertex[0] = center [:73].
  //   phi=0:   p=(0, 0.35, 0)  -> (-0.175, 0.3031089, 0)      +C = (-0.075,     0.5031089, 0.3)
  //   phi=90:  p=(1.05, 0, 0)  -> (0.9093267, 0.525, 0)       +C = (1.0093267,  0.725,     0.3)
  //   phi=180: p=(0, -0.35, 0) -> (0.175, -0.3031089, 0)      +C = (0.275,     -0.1031089, 0.3)
  //   phi=270: p=(-1.05, 0, 0) -> (-0.9093267, -0.525, 0)     +C = (-0.8093267, -0.325,    0.3)
  // Tangent = TransformNormal(Right, R) = (cos30, sin30, 0) = (0.8660254, 0.5, 0)  [:55].
  // A dropped Radius multiply reads 30% high; a dropped Stretch.x reads 50% high; a dropped
  // rotation leaves v1 at x=0 — every leg of the position product chain diverges here.
  {
    Graph g2;
    Node m2; m2.id = 2; m2.type = "NGonMesh";
    m2.params["Segments"] = 4.0f; m2.params["Radius"] = 0.7f;
    m2.params["Stretch.x"] = 1.5f; m2.params["Stretch.y"] = 0.5f;
    m2.params["Rotation.z"] = 30.0f;
    m2.params["Center.x"] = 0.1f; m2.params["Center.y"] = 0.2f; m2.params["Center.z"] = 0.3f;
    m2.params["TextureMode"] = 0.0f;
    g2.nodes.push_back(m2);
    pg.cook(g2, ctx, nullptr, 2);

    const MTL::Buffer* vb2 = nullptr;
    const MTL::Buffer* ib2 = nullptr;
    uint32_t vc2 = 0, fc2 = 0;
    bool got2 = pg.debugCookedMesh(2, vb2, vc2, ib2, fc2);
    bool ok2 = got2 && vc2 == 5 && fc2 == 4;
    if (ok2) {
      const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb2)->contents();
      bool vOk2 = posEq(v[0], 0.1f, 0.2f, 0.3f) &&
                  posEq(v[1], -0.075f, 0.5031089f, 0.3f) &&
                  posEq(v[2], 1.0093267f, 0.725f, 0.3f) &&
                  posEq(v[3], 0.275f, -0.1031089f, 0.3f) &&
                  posEq(v[4], -0.8093267f, -0.325f, 0.3f);
      bool tOk2 = tanEq(v[0], 0.8660254f, 0.5f, 0.0f);
      ok2 = vOk2 && tOk2;
      std::printf("[selftest-mesh-ngon] case2 v1=(%.4f,%.4f,%.4f) v2=(%.4f,%.4f,%.4f) vOk=%d tOk=%d\n",
                  v[1].Position.x, v[1].Position.y, v[1].Position.z, v[2].Position.x,
                  v[2].Position.y, v[2].Position.z, vOk2, tOk2);
    } else {
      std::printf("[selftest-mesh-ngon] case2 FAIL: got=%d verts=%u faces=%u\n", got2, vc2, fc2);
    }
    ok = ok && ok2;
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

  // ---- Case 2 (P2 fix: Scale/Stretch/Pivot/Rotation/Center all off identity and biting).
  // Segments=(1,1), Scale=0.7, Stretch=(2,0.5), Pivot=(0,1), Rotation=(0,0,30), Center=(0.1,-0.2,0.25).
  // Hand-derived from QuadMesh.cs:
  //   offset = (st.X*sc*(pv.X-0.5), st.Y*sc*(pv.Y-0.5), 0) = (-0.7, 0.175, 0)      [:38-40]
  //   columnStep = sc*st.X/(cols-1) = 1.4 ; rowStep = sc*st.Y/(rows-1) = 0.35      [:58-59]
  //   Position = TransformNormal(p+offset, Rz(30)) + center                        [:94]
  //     Rz(30) row-vec: (x*c - y*s, x*s + y*c, z), c=0.8660254, s=0.5.
  //   v0=(c0,r0): p+off=(-0.7, 0.175,0) -> (-0.6937178,-0.1984456,0) +C = (-0.5937178,-0.3984456,0.25)
  //   v1=(c0,r1): p+off=(-0.7, 0.525,0) -> (-0.8687178, 0.1046633,0) +C = (-0.7687178,-0.0953367,0.25)
  //   v2=(c1,r0): p+off=( 0.7, 0.175,0) -> ( 0.5187178, 0.5015544,0) +C = ( 0.6187178, 0.3015544,0.25)
  //   v3=(c1,r1): p+off=( 0.7, 0.525,0) -> ( 0.3437178, 0.8046633,0) +C = ( 0.4437178, 0.6046633,0.25)
  //   Tangent = TransformNormal(Right, R) = (0.8660254, 0.5, 0)                    [:66]
  // A dropped Scale multiplies the span by 1/0.7; a dropped Pivot.y kills the +0.175 offset; a
  // dropped rotation zeroes the x/y cross-terms — each leg diverges at these probes.
  {
    Graph g2;
    Node m2; m2.id = 2; m2.type = "QuadMesh";
    m2.params["Segments.x"] = 1.0f; m2.params["Segments.y"] = 1.0f;
    m2.params["Scale"] = 0.7f;
    m2.params["Stretch.x"] = 2.0f; m2.params["Stretch.y"] = 0.5f;
    m2.params["Pivot.x"] = 0.0f; m2.params["Pivot.y"] = 1.0f;
    m2.params["Rotation.z"] = 30.0f;
    m2.params["Center.x"] = 0.1f; m2.params["Center.y"] = -0.2f; m2.params["Center.z"] = 0.25f;
    g2.nodes.push_back(m2);
    pg.cook(g2, ctx, nullptr, 2);

    const MTL::Buffer* vb2 = nullptr;
    const MTL::Buffer* ib2 = nullptr;
    uint32_t vc2 = 0, fc2 = 0;
    bool got2 = pg.debugCookedMesh(2, vb2, vc2, ib2, fc2);
    bool ok2 = got2 && vc2 == 4 && fc2 == 2;
    if (ok2) {
      const SwVertex* v = (const SwVertex*)const_cast<MTL::Buffer*>(vb2)->contents();
      bool vOk2 = posEq(v[0], -0.5937178f, -0.3984456f, 0.25f) &&
                  posEq(v[1], -0.7687178f, -0.0953367f, 0.25f) &&
                  posEq(v[2], 0.6187178f, 0.3015544f, 0.25f) &&
                  posEq(v[3], 0.4437178f, 0.6046633f, 0.25f);
      bool tOk2 = tanEq(v[0], 0.8660254f, 0.5f, 0.0f);
      ok2 = vOk2 && tOk2;
      std::printf("[selftest-mesh-quad] case2 v0=(%.4f,%.4f,%.4f) v3=(%.4f,%.4f,%.4f) vOk=%d tOk=%d\n",
                  v[0].Position.x, v[0].Position.y, v[0].Position.z, v[3].Position.x,
                  v[3].Position.y, v[3].Position.z, vOk2, tOk2);
    } else {
      std::printf("[selftest-mesh-quad] case2 FAIL: got=%d verts=%u faces=%u\n", got2, vc2, fc2);
    }
    ok = ok && ok2;
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
