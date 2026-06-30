// IcosahedronMesh mesh op (icosahedron / geodesic-sphere generator) — rides the built mesh-op seam.
// TiXL authority: external/tixl/Operators/Lib/mesh/generate/IcosahedronMesh.cs (+ .t3 defaults
// Subdivisions=0, Spherical=false, Strength=1, Shading=0/Flat, TexCoord=0/Faces, Scale=1,
// Stretch=(1,1), Pivot=0, Center=0, Rotation=0). One .cpp owns the op.
//
// VERBATIM math (IcosahedronMesh.cs:19-351):
//   phi = (1+sqrt(5))/2 ; tiltAngle = atan(2/(2*phi)) = atan(1/phi).                               [:730-732]
//   rollOffset = roll - tiltAngle ; rotationMatrix = CreateFromYawPitchRoll(yaw, pitch, rollOffset). [:40-42]
//     ★ Even at default Rotation=0, the mesh is rotated by Rz(-tiltAngle) — the canonical tilt that
//       puts a vertex straight up. (This is why vertex[0] lands at (0,1,0) for defaults.)
//   GenerateIcosahedron: 12 normalized golden-ratio verts + 20 base faces, then SPLIT for flat
//     shading (each face gets its own 3 verts) -> 60 verts / 20 tris, tri i = (3i, 3i+1, 3i+2).   [:145-198]
//   SubdivideMeshFlat (Subdivisions>0): 1->4 split per level, optional spherical Lerp(v, normalize(v), strength). [:305-351]
//   Normals: Flat = per-face cross; Smoothed = average per unique position (Vector3EqualityComparer eps 1e-4). [:200-276]
//   Transform per vertex (IcosahedronMesh.cs:79-110):
//     offset = (stretchX*scale*pivotX, stretchY*scale*pivotY, stretchX*scale*pivotZ) ;
//     pos = (vX*scale*stretchX, vY*scale*stretchY, vZ*scale*stretchX) ;
//     pos = Transform(pos+offset, R) + center.
//     Normal/Tangent/Bitangent = TransformNormal(n / cross(n,UnitY) / cross(n,UnitX), R).
//   UV (all five mappers ported, IcosahedronMesh.cs:355-719): 0=Faces (_baseUvs[vertexIndex%3]),
//     1=Unwrapped (spherical projection + seam-fix, prepareUnwrapped), 2=Atlas, 3=FacesSub,
//     4=GridFacesSub (atlas/grid base-UV table + recursive TessellateUV per subdivision level).
//     TexCoord selects the mapper for Texcoord, TexCoord2 for Texcoord2. Positions/topology/normals
//     are 1:1 across subdivision + both shading modes. --selftest-mesh-icosahedron-uv pins each
//     non-default mode to its TiXL layout (FacesSub probed at sub=1, where it diverges from Faces).
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <cmath>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"        // EvaluationContext
#include "runtime/mesh_icosahedron_uv.h"  // ico_uv:: UV mappers (Faces/Unwrapped/Atlas/FacesSub/Grid)
#include "runtime/mesh_op_registry.h"    // MeshOp self-registration + MeshCookCtx + cookMeshParam/VecN
#include "runtime/sw_mesh.h"             // SwVertex (80B) + SwTriIndex (12B)

namespace sw {
namespace {

constexpr double kPiD = 3.14159265358979323846;
constexpr float kDeg2Rad = (float)(kPiD / 180.0);

struct V3 { float x, y, z; };
struct Tri { int x, y, z; };
V3 add(V3 a, V3 b) { return V3{a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 sub(V3 a, V3 b) { return V3{a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 mul(V3 a, float s) { return V3{a.x * s, a.y * s, a.z * s}; }
V3 cross(V3 a, V3 b) { return V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 normalize(V3 v) {
  float len = std::sqrt(dot(v, v));
  if (len < 1e-20f) return V3{0, 0, 0};
  return V3{v.x / len, v.y / len, v.z / len};
}
V3 lerp(V3 a, V3 b, float t) { return V3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }

V3 transformNormal(V3 v, float yaw, float pitch, float roll) {
  float cy = std::cos(yaw), sy = std::sin(yaw);
  float cx = std::cos(pitch), sx = std::sin(pitch);
  float cz = std::cos(roll), sz = std::sin(roll);
  float m11 = cy * cz + sy * sx * sz, m12 = cx * sz, m13 = -sy * cz + cy * sx * sz;
  float m21 = -cy * sz + sy * sx * cz, m22 = cx * cz, m23 = sy * sz + cy * sx * cz;
  float m31 = sy * cx, m32 = -sx, m33 = cy * cx;
  return V3{v.x * m11 + v.y * m21 + v.z * m31, v.x * m12 + v.y * m22 + v.z * m32,
            v.x * m13 + v.y * m23 + v.z * m33};
}

const float kPhi = (1.0f + std::sqrt(5.0f)) / 2.0f;
float tiltAngle() { return std::atan(2.0f / (2.0f * kPhi)); }

void generateIcosahedron(std::vector<V3>& verts, std::vector<Tri>& tris) {
  float p = kPhi;
  V3 bv[12] = {
      normalize({-1, p, 0}), normalize({1, p, 0}),  normalize({-1, -p, 0}), normalize({1, -p, 0}),
      normalize({0, -1, p}), normalize({0, 1, p}),  normalize({0, -1, -p}), normalize({0, 1, -p}),
      normalize({p, 0, -1}), normalize({p, 0, 1}),  normalize({-p, 0, -1}), normalize({-p, 0, 1})};
  Tri bt[20] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
                {5, 11, 4}, {1, 5, 9}, {7, 1, 8}, {10, 7, 6}, {11, 10, 2},
                {3, 9, 4},  {3, 8, 9}, {3, 6, 8}, {3, 2, 6},  {3, 4, 2},
                {4, 9, 5},  {9, 8, 1}, {8, 6, 7}, {6, 2, 10}, {2, 4, 11}};
  verts.clear(); tris.clear();
  for (auto& t : bt) {
    int v0 = (int)verts.size();
    verts.push_back(bv[t.x]); verts.push_back(bv[t.y]); verts.push_back(bv[t.z]);
    tris.push_back({v0, v0 + 1, v0 + 2});
  }
}

void subdivideFlat(std::vector<V3>& verts, std::vector<Tri>& tris, int levels, float strength,
                   bool spherical) {
  for (int i = 0; i < levels; ++i) {
    std::vector<V3> nv; std::vector<Tri> nt;
    nv.reserve(tris.size() * 12);
    for (auto& t : tris) {
      V3 v1 = verts[t.x], v2 = verts[t.y], v3 = verts[t.z];
      V3 a = mul(add(v1, v2), 0.5f), b = mul(add(v2, v3), 0.5f), c = mul(add(v3, v1), 0.5f);
      int base = (int)nv.size();
      auto sph = [&](V3 q) { return spherical ? lerp(q, normalize(q), strength) : q; };
      nv.push_back(sph(v1)); nv.push_back(sph(a)); nv.push_back(sph(c));
      nv.push_back(sph(v2)); nv.push_back(sph(b)); nv.push_back(sph(a));
      nv.push_back(sph(v3)); nv.push_back(sph(c)); nv.push_back(sph(b));
      nv.push_back(sph(a));  nv.push_back(sph(b)); nv.push_back(sph(c));
      nt.push_back({base + 0, base + 1, base + 2});
      nt.push_back({base + 3, base + 4, base + 5});
      nt.push_back({base + 6, base + 7, base + 8});
      nt.push_back({base + 9, base + 10, base + 11});
    }
    verts.swap(nv); tris.swap(nt);
  }
}

void flatNormals(const std::vector<V3>& verts, const std::vector<Tri>& tris, std::vector<V3>& n) {
  n.assign(verts.size(), V3{0, 0, 0});
  for (auto& t : tris) {
    V3 nrm = normalize(cross(sub(verts[t.y], verts[t.x]), sub(verts[t.z], verts[t.x])));
    n[t.x] = nrm; n[t.y] = nrm; n[t.z] = nrm;
  }
}

void smoothNormals(const std::vector<V3>& verts, const std::vector<Tri>& tris, std::vector<V3>& n) {
  // Vector3EqualityComparer eps 1e-4 (IcosahedronMesh.cs:281). Quantize to 1e-4 buckets for grouping.
  auto key = [](V3 v) {
    auto q = [](float f) { return (long long)std::llround(f / 1e-4f); };
    return ((q(v.x) * 73856093LL) ^ (q(v.y) * 19349663LL) ^ (q(v.z) * 83492791LL));
  };
  std::unordered_map<long long, V3> accum;
  std::unordered_map<long long, int> cnt;
  for (auto& t : tris) {
    V3 nrm = normalize(cross(sub(verts[t.y], verts[t.x]), sub(verts[t.z], verts[t.x])));
    for (int idx : {t.x, t.y, t.z}) {
      long long k = key(verts[idx]);
      accum[k] = add(accum.count(k) ? accum[k] : V3{0, 0, 0}, nrm);
      cnt[k] += 1;
    }
  }
  n.assign(verts.size(), V3{0, 0, 0});
  for (size_t i = 0; i < verts.size(); ++i) {
    long long k = key(verts[i]);
    int c = cnt[k];
    n[i] = c > 0 ? normalize(mul(accum[k], 1.0f / c)) : V3{0, 1, 0};
  }
}

int clampI(float v, int lo, int hi) {
  int s = (int)(v + 0.5f);
  if (s < lo) s = lo;
  if (s > hi) s = hi;
  return s;
}

void icoCounts(const std::map<std::string, float>* params, int& vtx, int& tri) {
  int sub = clampI(cookMeshParam(params, "Subdivisions", 0.0f), 0, 5);
  long long faces = 20;
  for (int i = 0; i < sub; ++i) faces *= 4;
  tri = (int)faces;
  vtx = (int)(faces * 3);  // flat-split: 3 verts per face
}

void icoCount(const std::map<std::string, float>* params, const SwMeshView* /*inputs*/,
              int /*inputCount*/, uint32_t& vtx, uint32_t& idx) {
  int v, t; icoCounts(params, v, t);
  vtx = (uint32_t)v; idx = (uint32_t)t;
}

void icoCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  float scale = cookMeshParam(c.params, "Scale", 1.0f);
  float stretchDef[2] = {1, 1};
  float stretch[2]; cookMeshVecN(c.params, "Stretch", stretchDef, 2, stretch);
  float pivotDef[3] = {0, 0, 0};
  float pivot[3]; cookMeshVecN(c.params, "Pivot", pivotDef, 3, pivot);
  float rotDef[3] = {0, 0, 0};
  float rot[3]; cookMeshVecN(c.params, "Rotation", rotDef, 3, rot);
  float centerDef[3] = {0, 0, 0};
  float center[3]; cookMeshVecN(c.params, "Center", centerDef, 3, center);
  int subdivisions = clampI(cookMeshParam(c.params, "Subdivisions", 0.0f), 0, 5);
  bool spherical = cookMeshParam(c.params, "Spherical", 0.0f) > 0.5f;
  float strength = cookMeshParam(c.params, "Strength", 1.0f);
  int shadingMode = clampI(cookMeshParam(c.params, "Shading", 0.0f), 0, 1);
  int uvMode = (int)(cookMeshParam(c.params, "TexCoord", 0.0f) + 0.5f);
  int uvMode2 = (int)(cookMeshParam(c.params, "TexCoord2", 0.0f) + 0.5f);

  float yaw = rot[1] * kDeg2Rad, pitch = rot[0] * kDeg2Rad, roll = rot[2] * kDeg2Rad;
  float rollOffset = roll - tiltAngle();

  std::vector<V3> verts; std::vector<Tri> tris;
  generateIcosahedron(verts, tris);
  if (subdivisions > 0) subdivideFlat(verts, tris, subdivisions, strength, spherical);
  std::vector<V3> normals;
  if (shadingMode == 1) smoothNormals(verts, tris, normals);
  else flatNormals(verts, tris, normals);

  // Per-channel UV: Unwrapped (mode 1) needs a whole-mesh Prepare pass; the rest are per-vertex.
  // verts/tris are tightly-packed {x,y,z}/{i0,i1,i2} so reinterpret to the raw views the header wants.
  std::vector<ico_uv::UvV2> unwrapped0, unwrapped1;
  if (uvMode == 1)
    ico_uv::prepareUnwrapped((const float*)verts.data(), verts.size(), (const int*)tris.data(),
                             tris.size(), tiltAngle(), (float)kPiD, unwrapped0);
  if (uvMode2 == 1)
    ico_uv::prepareUnwrapped((const float*)verts.data(), verts.size(), (const int*)tris.data(),
                             tris.size(), tiltAngle(), (float)kPiD, unwrapped1);

  SwVertex* outV = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* outI = (SwTriIndex*)c.output_indices->contents();
  std::memset(outV, 0, (size_t)c.vertexCount * sizeof(SwVertex));
  std::memset(outI, 0, (size_t)c.indexCount * sizeof(SwTriIndex));

  V3 offset = {stretch[0] * scale * pivot[0], stretch[1] * scale * pivot[1],
               stretch[0] * scale * pivot[2]};
  for (size_t i = 0; i < verts.size() && i < c.vertexCount; ++i) {
    V3 pos = {verts[i].x * scale * stretch[0], verts[i].y * scale * stretch[1],
              verts[i].z * scale * stretch[0]};
    pos = add(transformNormal(add(pos, offset), yaw, pitch, rollOffset), V3{center[0], center[1], center[2]});
    V3 n = transformNormal(normals[i], yaw, pitch, rollOffset);
    V3 t = transformNormal(cross(normals[i], V3{0, 1, 0}), yaw, pitch, rollOffset);
    V3 bn = transformNormal(cross(normals[i], V3{1, 0, 0}), yaw, pitch, rollOffset);
    ico_uv::UvV2 uv0 = ico_uv::uvForVertex(uvMode, (int)i, subdivisions, unwrapped0);
    ico_uv::UvV2 uv1 = ico_uv::uvForVertex(uvMode2, (int)i, subdivisions, unwrapped1);
    SwVertex& v = outV[i];
    v.Position = {pos.x, pos.y, pos.z};
    v.Normal = {n.x, n.y, n.z};
    v.Tangent = {t.x, t.y, t.z};
    v.Bitangent = {bn.x, bn.y, bn.z};
    v.Texcoord = {uv0.x, uv0.y};
    v.Texcoord2 = {uv1.x, uv1.y};
    v.Selection = 1.0f;
    v.ColorRgb = {1, 1, 1};
  }
  for (size_t i = 0; i < tris.size() && i < c.indexCount; ++i)
    outI[i] = {tris[i].x, tris[i].y, tris[i].z};

  // Test injection (golden RED): corrupt the first vertex position in the REAL output.
  if (meshInjectBug() && c.vertexCount > 0) outV[0].Position = {-999.0f, -999.0f, -999.0f};
}

NodeSpec icoSpec() {
  NodeSpec s;
  s.type = "IcosahedronMesh";
  s.title = "Icosahedron Mesh";
  PortSpec sub; sub.id = "Subdivisions"; sub.name = "Subdivisions"; sub.dataType = "Float"; sub.isInput = true;
  sub.def = 0.0f; sub.minV = 0.0f; sub.maxV = 5.0f;
  PortSpec sph; sph.id = "Spherical"; sph.name = "Spherical"; sph.dataType = "Float"; sph.isInput = true;
  sph.def = 0.0f; sph.minV = 0.0f; sph.maxV = 1.0f; sph.widget = Widget::Enum; sph.labels = {"Off", "On"};
  PortSpec str; str.id = "Strength"; str.name = "Strength"; str.dataType = "Float"; str.isInput = true;
  str.def = 1.0f; str.minV = 0.0f; str.maxV = 1.0f;
  PortSpec stx; stx.id = "Stretch.x"; stx.name = "Stretch"; stx.dataType = "Float"; stx.isInput = true;
  stx.def = 1.0f; stx.minV = 0.0f; stx.maxV = 10.0f; stx.widget = Widget::Vec; stx.vecArity = 2;
  PortSpec sty; sty.id = "Stretch.y"; sty.name = "Stretch.y"; sty.dataType = "Float"; sty.isInput = true;
  sty.def = 1.0f; sty.minV = 0.0f; sty.maxV = 10.0f;
  PortSpec scl; scl.id = "Scale"; scl.name = "Scale"; scl.dataType = "Float"; scl.isInput = true;
  scl.def = 1.0f; scl.minV = 0.0f; scl.maxV = 10.0f;
  PortSpec pvx; pvx.id = "Pivot.x"; pvx.name = "Pivot"; pvx.dataType = "Float"; pvx.isInput = true;
  pvx.def = 0.0f; pvx.minV = -1.0f; pvx.maxV = 1.0f; pvx.widget = Widget::Vec; pvx.vecArity = 3;
  PortSpec pvy; pvy.id = "Pivot.y"; pvy.name = "Pivot.y"; pvy.dataType = "Float"; pvy.isInput = true;
  pvy.def = 0.0f; pvy.minV = -1.0f; pvy.maxV = 1.0f;
  PortSpec pvz; pvz.id = "Pivot.z"; pvz.name = "Pivot.z"; pvz.dataType = "Float"; pvz.isInput = true;
  pvz.def = 0.0f; pvz.minV = -1.0f; pvz.maxV = 1.0f;
  PortSpec ctx; ctx.id = "Center.x"; ctx.name = "Center"; ctx.dataType = "Float"; ctx.isInput = true;
  ctx.def = 0.0f; ctx.minV = -10.0f; ctx.maxV = 10.0f; ctx.widget = Widget::Vec; ctx.vecArity = 3;
  PortSpec cty; cty.id = "Center.y"; cty.name = "Center.y"; cty.dataType = "Float"; cty.isInput = true;
  cty.def = 0.0f; cty.minV = -10.0f; cty.maxV = 10.0f;
  PortSpec ctz; ctz.id = "Center.z"; ctz.name = "Center.z"; ctz.dataType = "Float"; ctz.isInput = true;
  ctz.def = 0.0f; ctz.minV = -10.0f; ctz.maxV = 10.0f;
  PortSpec rtx; rtx.id = "Rotation.x"; rtx.name = "Rotation"; rtx.dataType = "Float"; rtx.isInput = true;
  rtx.def = 0.0f; rtx.minV = -360.0f; rtx.maxV = 360.0f; rtx.widget = Widget::Vec; rtx.vecArity = 3;
  PortSpec rty; rty.id = "Rotation.y"; rty.name = "Rotation.y"; rty.dataType = "Float"; rty.isInput = true;
  rty.def = 0.0f; rty.minV = -360.0f; rty.maxV = 360.0f;
  PortSpec rtz; rtz.id = "Rotation.z"; rtz.name = "Rotation.z"; rtz.dataType = "Float"; rtz.isInput = true;
  rtz.def = 0.0f; rtz.minV = -360.0f; rtz.maxV = 360.0f;
  PortSpec shd; shd.id = "Shading"; shd.name = "Shading"; shd.dataType = "Float"; shd.isInput = true;
  shd.def = 0.0f; shd.minV = 0.0f; shd.maxV = 1.0f; shd.widget = Widget::Enum; shd.labels = {"Flat", "Smoothed"};
  // TexCoord / TexCoord2: real TiXL [Input]s (IcosahedronMesh.cs:781-785).
  // UV modes (0=Faces, 1=Unwrapped, 2=Atlas, 3=FacesSub, 4=GridFacesSub) — all five ported in
  // mesh_icosahedron_uv.h (ico_uv::uvForVertex). TexCoord/TexCoord2 each select a mapper per channel.
  PortSpec tco; tco.id = "TexCoord"; tco.name = "TexCoord"; tco.dataType = "Float"; tco.isInput = true;
  tco.def = 0.0f; tco.minV = 0.0f; tco.maxV = 4.0f;
  tco.widget = Widget::Enum; tco.labels = {"Faces", "Unwrapped", "Atlas", "FacesSub", "GridFacesSub"};
  PortSpec tc2; tc2.id = "TexCoord2"; tc2.name = "TexCoord2"; tc2.dataType = "Float"; tc2.isInput = true;
  tc2.def = 0.0f; tc2.minV = 0.0f; tc2.maxV = 4.0f;
  tc2.widget = Widget::Enum; tc2.labels = {"Faces", "Unwrapped", "Atlas", "FacesSub", "GridFacesSub"};
  PortSpec out; out.id = "Data"; out.name = "Data"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {sub, sph, str, stx, sty, scl, pvx, pvy, pvz, ctx, cty, ctz, rtx, rty, rtz, shd, tco, tc2, out};
  return s;
}

const MeshOp g_icoMeshOp(icoSpec(), icoCount, icoCook);

}  // namespace
}  // namespace sw
