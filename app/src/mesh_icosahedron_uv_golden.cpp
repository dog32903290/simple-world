// mesh_icosahedron_uv_golden — --selftest-mesh-icosahedron-uv. CPU-READBACK golden for
// IcosahedronMesh's non-default UV mappers (TexCoord/TexCoord2 modes 1=Unwrapped, 2=Atlas,
// 3=FacesSub, 4=GridFacesSub), proving each reproduces TiXL's layout — NOT the Faces fallback.
// CLOSED-FORM: every mapper is a pure function of (vertex, vertexIndex=i%3, triangleIndex=i/3,
// subdivisionLevel); expected values are hand-derived from IcosahedronMesh.cs:355-719.
//
// vertex i -> triangle i/3, local vertexIndex i%3 (flat-split keeps 3 verts/face).
//
//   Mode 1 Unwrapped (sub=0, tri 0 = baseVertices[0,11,5]): spherical UV after the Rz(-tilt) tilt,
//     then the final (1-u, v) flip. The C# winding-flip is a verified NO-OP (it swaps uvs[1]/uvs[2]
//     then the fixedIndex remap swaps them back; _flippedTriangles is never read), so only the
//     seam-fix shifts u. Hand-derived (atan2/asin):
//       i0=(0.5, 1.0)  i1=(0.10008, 0.64758)  i2=(0.30007, 0.64758).
//   Mode 2 Atlas (sub=0, face 0): GetBaseTriangleUvs(0), currentMaxY=0.472382.
//       i0=(0.09091, 1.0)  i1=(0.0, 0.66666)  i2=(0.181819, 0.66666).
//   Mode 3 FacesSub (sub=1, tri 0, subTriangle 0): TessellateUV(_baseUvs, level 1, quadrant 0)
//     -> V0 keeps, V1=mid01, V2=mid20.   i0=(0.5, 1.0)  i1=(0.2835, 0.625)  i2=(0.7165, 0.625).
//     (At sub=0 FacesSub == Faces, so it MUST be probed at sub>=1 to differ from the default.)
//   Mode 4 GridFacesSub (sub=0, face 0): GetBaseTriangleUvs grid table, xOffset adds the centering.
//       i0=(0.1, 0.907461)  i1=(0.00909, 0.75)  i2=(0.190909, 0.75).
//   Mode 0 Faces (default): _baseUvs[i%3] = {(0.5,1.0),(0.067,0.25),(0.933,0.25)} — unchanged.
//
// injectBug -> meshInjectBug() -> corrupts verts[0].Position in the REAL cook -> RED (tooth bites).
#include <cmath>
#include <cstdint>
#include <cstdio>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/mesh_icosahedron_uv.h"
#include "runtime/mesh_op_registry.h"
#include "runtime/point_graph.h"
#include "runtime/sw_mesh.h"

namespace sw {
namespace {
bool nearf(float a, float b, float eps) { return std::fabs(a - b) < eps; }
bool uvEq(const SwVertex& v, float u, float w, float eps = 1.5e-3f) {
  return nearf(v.Texcoord.x, u, eps) && nearf(v.Texcoord.y, w, eps);
}
bool uv2Eq(const SwVertex& v, float u, float w, float eps = 1.5e-3f) {
  return nearf(v.Texcoord2.x, u, eps) && nearf(v.Texcoord2.y, w, eps);
}

const SwVertex* cookIco(PointGraph& pg, Graph& g, EvaluationContext& ctx, float subdivisions,
                        float texCoord, float texCoord2, uint32_t& vc) {
  g.nodes.clear();
  Node m; m.id = 1; m.type = "IcosahedronMesh";
  m.params["Subdivisions"] = subdivisions; m.params["Spherical"] = 0.0f; m.params["Strength"] = 1.0f;
  m.params["Scale"] = 1.0f; m.params["Stretch.x"] = 1.0f; m.params["Stretch.y"] = 1.0f;
  m.params["Shading"] = 0.0f;
  m.params["TexCoord"] = texCoord; m.params["TexCoord2"] = texCoord2;
  g.nodes.push_back(m);
  pg.cook(g, ctx, nullptr, 1);
  pg.cook(g, ctx, nullptr, 1);
  const MTL::Buffer* vb = nullptr; const MTL::Buffer* ib = nullptr;
  uint32_t fc = 0;
  if (!pg.debugCookedMesh(1, vb, vc, ib, fc) || vc < 3) return nullptr;
  return (const SwVertex*)const_cast<MTL::Buffer*>(vb)->contents();
}
}  // namespace

int runMeshIcosahedronUvGoldenSelfTest(bool injectBug) {
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

  // Mode 1 Unwrapped (sub=0).
  const SwVertex* v = cookIco(pg, g, ctx, 0.0f, 1.0f, 0.0f, vc);
  bool m1 = v && uvEq(v[0], 0.5f, 1.0f) && uvEq(v[1], 0.10008f, 0.64758f) &&
            uvEq(v[2], 0.30007f, 0.64758f);
  bool posBite = v && nearf(v[0].Position.y, 1.0f, 1e-3f);  // tilt puts v0 at (0,1,0); injectBug breaks it
  ok = ok && m1 && posBite;
  if (v) std::printf("[selftest-mesh-icosahedron-uv] mode1 v0=(%.4f,%.4f) v1=(%.4f,%.4f) v2=(%.4f,%.4f) m1=%d\n",
                     v[0].Texcoord.x, v[0].Texcoord.y, v[1].Texcoord.x, v[1].Texcoord.y,
                     v[2].Texcoord.x, v[2].Texcoord.y, m1);

  // Mode 2 Atlas (sub=0).
  v = cookIco(pg, g, ctx, 0.0f, 2.0f, 0.0f, vc);
  bool m2 = v && uvEq(v[0], 0.09091f, 1.0f) && uvEq(v[1], 0.0f, 0.66666f) &&
            uvEq(v[2], 0.181819f, 0.66666f);
  ok = ok && m2;
  if (v) std::printf("[selftest-mesh-icosahedron-uv] mode2 v0=(%.4f,%.4f) v1=(%.4f,%.4f) v2=(%.4f,%.4f) m2=%d\n",
                     v[0].Texcoord.x, v[0].Texcoord.y, v[1].Texcoord.x, v[1].Texcoord.y,
                     v[2].Texcoord.x, v[2].Texcoord.y, m2);

  // Mode 3 FacesSub (sub=1 — distinguishes from Faces).
  v = cookIco(pg, g, ctx, 1.0f, 3.0f, 0.0f, vc);
  bool m3 = v && vc == 240 && uvEq(v[0], 0.5f, 1.0f) && uvEq(v[1], 0.2835f, 0.625f) &&
            uvEq(v[2], 0.7165f, 0.625f);
  ok = ok && m3;
  if (v) std::printf("[selftest-mesh-icosahedron-uv] mode3 vc=%u v0=(%.4f,%.4f) v1=(%.4f,%.4f) v2=(%.4f,%.4f) m3=%d\n",
                     vc, v[0].Texcoord.x, v[0].Texcoord.y, v[1].Texcoord.x, v[1].Texcoord.y,
                     v[2].Texcoord.x, v[2].Texcoord.y, m3);

  // Mode 4 GridFacesSub (sub=0).
  v = cookIco(pg, g, ctx, 0.0f, 4.0f, 0.0f, vc);
  bool m4 = v && uvEq(v[0], 0.1f, 0.907461f) && uvEq(v[1], 0.00909f, 0.75f) &&
            uvEq(v[2], 0.190909f, 0.75f);
  ok = ok && m4;
  if (v) std::printf("[selftest-mesh-icosahedron-uv] mode4 v0=(%.4f,%.4f) v1=(%.4f,%.4f) v2=(%.4f,%.4f) m4=%d\n",
                     v[0].Texcoord.x, v[0].Texcoord.y, v[1].Texcoord.x, v[1].Texcoord.y,
                     v[2].Texcoord.x, v[2].Texcoord.y, m4);

  // Unwrapped seam-fix INVARIANT (no cook, no hand-computed exact UV). Cross-seam triangles are the
  // NORM for spherical unwrap, not an edge case; the seam-fix branch is what stops a wrapping tri from
  // smearing its texture across the whole atlas. Feed a triangle whose 3 verts project to u≈{0.95,
  // 0.05, 0.02} (two near 0, one near 1 — straddling the u=0/1 seam) straight into prepareUnwrapped.
  //   verts (pre-tilt, y=0 so the Rz(-tilt) tilt only scales x): x=-1, z=+0.3/-0.3/-0.1.
  //   Invariant: after seam-fix the edge's u-span (max-min of returned .x) collapses to < 0.5.
  //   injectBug drops the seam-fix branch (header hook) -> span springs back to ~0.93 -> RED.
  {
    const float phi = (1.0f + std::sqrt(5.0f)) / 2.0f;
    const float tilt = std::atan(1.0f / phi);
    const float pi = 3.14159265358979323846f;
    float triVerts[9] = {-1.0f, 0.0f, 0.3f, -1.0f, 0.0f, -0.3f, -1.0f, 0.0f, -0.1f};
    int triIdx[3] = {0, 1, 2};
    std::vector<ico_uv::UvV2> seamUv;
    ico_uv::prepareUnwrapped(triVerts, 3, triIdx, 1, tilt, pi, seamUv);
    float mn = seamUv[0].x, mx = seamUv[0].x;
    for (int i = 1; i < 3; ++i) { mn = std::fmin(mn, seamUv[i].x); mx = std::fmax(mx, seamUv[i].x); }
    float span = mx - mn;
    bool seamOk = (span < 0.5f);
    ok = ok && seamOk;
    std::printf("[selftest-mesh-icosahedron-uv] seam u-span=%.4f (<0.5 expected; injectBug ->~0.93) seamOk=%d\n",
                span, seamOk);
  }

  // Channel independence: TexCoord=0 (Faces) on Texcoord, TexCoord2=2 (Atlas) on Texcoord2.
  v = cookIco(pg, g, ctx, 0.0f, 0.0f, 2.0f, vc);
  bool ch = v && uvEq(v[0], 0.5f, 1.0f) && uv2Eq(v[0], 0.09091f, 1.0f);
  ok = ok && ch;
  if (v) std::printf("[selftest-mesh-icosahedron-uv] chan v0.tc=(%.4f,%.4f) v0.tc2=(%.4f,%.4f) ch=%d\n",
                     v[0].Texcoord.x, v[0].Texcoord.y, v[0].Texcoord2.x, v[0].Texcoord2.y, ch);

  meshInjectBug() = false;
  q->release();
  dev->release();
  pool->release();

  std::printf("[selftest-mesh-icosahedron-uv] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
