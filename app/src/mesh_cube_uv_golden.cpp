// mesh_cube_uv_golden — --selftest-mesh-cube-uv. CPU-READBACK golden for CubeMesh's non-default UV
// mappers (TexCoord/TexCoord2 modes 1/2/3), proving they reproduce TiXL's atlas layouts — NOT the
// Standard fallback. CLOSED-FORM: TiXL UV math is a pure function of (u,v,sideIndex,padding); the
// expected values below are hand-derived from CubeMesh.cs:308-442, no observation needed.
//
// Defaults: Segments=(1,1,1) -> columnCount=rowCount=2 per side, so u0,v0 in {0,1}. Front face
// (sideIndex 0) verts are vertexIndex 0..3:  v0=(u0=0,v0=0) v1=(0,1) v2=(1,0) v3=(1,1).
// Top face (sideIndex 4) starts at vertexIndex 16 (4 verts/side * 4 prior sides).
//
//   Mode 1 UnwrappedCubeMapper (margin=0): usableW=1/3, usableH=1/2, faceSize=1/3, column=side%3,
//     row=side/3. Since usableH>usableW: yOffset += (1/2-1/3)/2 = 1/12. uv=(u/3 + col/3, v/3 + row/2 + 1/12).
//       Front(col0,row0): v0 uv=(0, 1/12=0.083333) ; v3 uv=(1/3, 1/3+1/12=0.416667).
//       Top  (col1,row1): v16 uv=(1/3, 7/12=0.583333).
//   Mode 2 CubeMap (margin=0): uv = region.XY + (u,v)*region.ZW. Front region=(0.25,1/3,0.25,1/3).
//       v0 uv=(0.25, 0.333333) ; v3 uv=(0.5, 0.666667).
//   Mode 3 CubeMapSquare (margin=0): Front region=(0.25, 0.375, 0.25, 0.25).
//       v0 uv=(0.25, 0.375) ; v3 uv=(0.5, 0.625).
//   Mode 0 Standard (margin=0): identity (u,v).  v0=(0,0) v3=(1,1).  (the .t3 default — unchanged.)
//
// Channel wiring: TexCoord drives Texcoord (margin Margin); TexCoord2 drives Texcoord2 (margin
// Margin2). Independent — checked by cooking TexCoord=0 + TexCoord2=2.
//
// injectBug -> meshInjectBug() -> corrupts verts[0].Position in the REAL cook -> RED (tooth bites).
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
bool nearf(float a, float b) { return std::fabs(a - b) < 1e-4f; }
bool uvEq(const SwVertex& v, float u, float w) {
  return nearf(v.Texcoord.x, u) && nearf(v.Texcoord.y, w);
}
bool uv2Eq(const SwVertex& v, float u, float w) {
  return nearf(v.Texcoord2.x, u) && nearf(v.Texcoord2.y, w);
}

// Cook a CubeMesh with the given UV-channel params and return the readback buffer (or fail).
const SwVertex* cookCube(PointGraph& pg, Graph& g, EvaluationContext& ctx, float texCoord,
                         float texCoord2, uint32_t& vc, float margin = 0.0f) {
  g.nodes.clear();
  Node m; m.id = 1; m.type = "CubeMesh";
  m.params["Segments.x"] = 1.0f; m.params["Segments.y"] = 1.0f; m.params["Segments.z"] = 1.0f;
  m.params["Scale"] = 1.0f;
  m.params["Stretch.x"] = 1.0f; m.params["Stretch.y"] = 1.0f; m.params["Stretch.z"] = 1.0f;
  m.params["TexCoord"] = texCoord; m.params["TexCoord2"] = texCoord2;
  m.params["Margin"] = margin;
  g.nodes.push_back(m);
  pg.cook(g, ctx, nullptr, 1);
  pg.cook(g, ctx, nullptr, 1);
  const MTL::Buffer* vb = nullptr; const MTL::Buffer* ib = nullptr;
  uint32_t fc = 0;
  if (!pg.debugCookedMesh(1, vb, vc, ib, fc) || vc != 24) return nullptr;
  return (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
}
}  // namespace

int runMeshCubeUvGoldenSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  meshInjectBug() = injectBug;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, nullptr, q, 64, 64);
  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  Graph g;
  uint32_t vc = 0;
  bool ok = true;

  // Mode 1 Unwrapped.
  const SwVertex* v = cookCube(pg, g, ctx, 1.0f, 0.0f, vc);
  bool m1 = v && uvEq(v[0], 0.0f, 1.0f / 12.0f) && uvEq(v[3], 1.0f / 3.0f, 1.0f / 3.0f + 1.0f / 12.0f) &&
            uvEq(v[16], 1.0f / 3.0f, 7.0f / 12.0f);
  // injectBug corrupts v[0].Position -> fold it into the verdict so the tooth bites.
  bool posBite = v && nearf(v[0].Position.x, -0.5f);
  ok = ok && m1 && posBite;
  if (v) std::printf("[selftest-mesh-cube-uv] mode1 v0=(%.4f,%.4f) v3=(%.4f,%.4f) v16=(%.4f,%.4f) m1=%d\n",
                     v[0].Texcoord.x, v[0].Texcoord.y, v[3].Texcoord.x, v[3].Texcoord.y,
                     v[16].Texcoord.x, v[16].Texcoord.y, m1);

  // Mode 2 CubeMap.
  v = cookCube(pg, g, ctx, 2.0f, 0.0f, vc);
  bool m2 = v && uvEq(v[0], 0.25f, 1.0f / 3.0f) && uvEq(v[3], 0.5f, 2.0f / 3.0f);
  ok = ok && m2;
  if (v) std::printf("[selftest-mesh-cube-uv] mode2 v0=(%.4f,%.4f) v3=(%.4f,%.4f) m2=%d\n",
                     v[0].Texcoord.x, v[0].Texcoord.y, v[3].Texcoord.x, v[3].Texcoord.y, m2);

  // Mode 3 CubeMapSquare.
  v = cookCube(pg, g, ctx, 3.0f, 0.0f, vc);
  bool m3 = v && uvEq(v[0], 0.25f, 0.375f) && uvEq(v[3], 0.5f, 0.625f);
  ok = ok && m3;
  if (v) std::printf("[selftest-mesh-cube-uv] mode3 v0=(%.4f,%.4f) v3=(%.4f,%.4f) m3=%d\n",
                     v[0].Texcoord.x, v[0].Texcoord.y, v[3].Texcoord.x, v[3].Texcoord.y, m3);

  // Margin lock: CubeMap (mode 2) with Margin=0.05 exercises the padding terms (regionUv double
  // reassignment, CubeMesh.cs:375-398). Front region (0.25,1/3,0.25,1/3), p=0.05 -> hand-derived:
  //   width=1/3-0.1=0.23333, height likewise; x=0.375-0.075+0.05=0.35, y=0.5-0.116665+0.05=0.43333.
  //   v0(u0,v0)=(0.35, 0.43333) ; v3(u1,v1)=(0.35+0.05, 0.43333+0.13333)=(0.4, 0.56667).
  v = cookCube(pg, g, ctx, 2.0f, 0.0f, vc, 0.05f);
  bool mg = v && uvEq(v[0], 0.35f, 0.43333f) && uvEq(v[3], 0.4f, 0.56667f);
  ok = ok && mg;
  if (v) std::printf("[selftest-mesh-cube-uv] margin v0=(%.4f,%.4f) v3=(%.4f,%.4f) mg=%d\n",
                     v[0].Texcoord.x, v[0].Texcoord.y, v[3].Texcoord.x, v[3].Texcoord.y, mg);

  // Face-selection INVARIANT (CubeMap, margin=0): each of the 6 faces owns 4 verts (sideIndex=i/4)
  // and must map into ITS OWN cell — not just Front. Centroid of a face's 4 verts = its region center
  // (u0,v0 in {0,1} average to 0.5). Catches a broken sideIndex→region index even when Front is right.
  v = cookCube(pg, g, ctx, 2.0f, 0.0f, vc);
  const float kFaceCenter[6][2] = {{0.375f, 0.5f}, {0.625f, 0.5f}, {0.875f, 0.5f},
                                   {0.125f, 0.5f}, {0.375f, 1.0f / 6.0f}, {0.375f, 5.0f / 6.0f}};
  bool faceSel = (v != nullptr);
  for (int s = 0; s < 6 && faceSel; ++s) {
    float cu = 0, cv = 0;
    for (int k = 0; k < 4; ++k) { cu += v[s * 4 + k].Texcoord.x; cv += v[s * 4 + k].Texcoord.y; }
    cu *= 0.25f; cv *= 0.25f;
    if (!nearf(cu, kFaceCenter[s][0]) || !nearf(cv, kFaceCenter[s][1])) faceSel = false;
  }
  ok = ok && faceSel;
  std::printf("[selftest-mesh-cube-uv] faceSel=%d (6 faces each centered on own CubeMap cell)\n", faceSel);

  // Channel independence: TexCoord=0 (Standard) on Texcoord, TexCoord2=2 (CubeMap) on Texcoord2.
  v = cookCube(pg, g, ctx, 0.0f, 2.0f, vc);
  bool ch = v && uvEq(v[0], 0.0f, 0.0f) && uv2Eq(v[0], 0.25f, 1.0f / 3.0f);
  ok = ok && ch;
  if (v) std::printf("[selftest-mesh-cube-uv] chan v0.tc=(%.4f,%.4f) v0.tc2=(%.4f,%.4f) ch=%d\n",
                     v[0].Texcoord.x, v[0].Texcoord.y, v[0].Texcoord2.x, v[0].Texcoord2.y, ch);

  meshInjectBug() = false;
  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-mesh-cube-uv] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
