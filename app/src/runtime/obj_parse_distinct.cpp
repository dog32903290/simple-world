// runtime/obj_parse_distinct — distinct-vertex dedup + TBN (tangent/binormal). Split out of obj_parse.cpp
// per ARCHITECTURE rule 4 (keep each file < ~400 lines, one responsibility). For the LATER LoadObj
// consumer (Tooll's mesh format combines pos/normal/uv into one vertex index); LoadObjAsPoints does NOT
// use this (it reads RAW positions). TiXL authority: ObjMesh.cs:235-368 (InitializeVertices /
// SortInMergedVertex / GetUvFromPositionAndNormal) + MeshUtils.cs:5 (CalcTBNSpace). Mirrored 1:1 INCLUDING
// the checked-out position-index re-sort (ObjMesh.cs:292-326, fork-objmesh-checked-out-resort).
#include "runtime/obj_parse.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <vector>

namespace sw {

namespace {

ObjVec3 sub3(const ObjVec3& a, const ObjVec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
ObjVec3 cross3(const ObjVec3& a, const ObjVec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
ObjVec3 normalize3(const ObjVec3& v) {
  float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  if (len <= 0.0f) return {0.0f, 0.0f, 0.0f};
  return {v.x / len, v.y / len, v.z / len};
}

// MeshUtils.CalcTBNSpace (MeshUtils.cs:5) — verbatim.
void calcTBN(const ObjVec3& p0, const ObjVec2& uv0, const ObjVec3& p1, const ObjVec2& uv1,
             const ObjVec3& p2, const ObjVec2& uv2, const ObjVec3& normal, ObjVec3& tangent,
             ObjVec3& bitangent) {
  ObjVec3 q1 = sub3(p1, p0);
  ObjVec3 q2 = sub3(p2, p0);
  float s1 = uv1.u - uv0.u, t1 = uv1.v - uv0.v;
  float s2 = uv2.u - uv0.u, t2 = uv2.v - uv0.v;
  float det = (s1 * t2 - s2 * t1);
  float inv = det != 0.0f ? 1.0f / det : 0.0f;  // .NET would yield ±Inf/NaN on det==0; sw guards (rare,
                                                // only on degenerate UVs — TiXL's fallback-UV path avoids it)
  ObjVec3 tRaw = {(q1.x * t2 - q2.x * t1) * inv, (q1.y * t2 - q2.y * t1) * inv,
                  (q1.z * t2 - q2.z * t1) * inv};
  bitangent = normalize3(cross3(normal, tRaw));
  tangent = normalize3(cross3(bitangent, normal));
}

// GetUvFromPositionAndNormal (ObjMesh.cs:370) — verbatim projection of position onto a normal-major plane.
ObjVec2 uvFromPosNormal(const ObjVec3& pos, const ObjVec3& normal) {
  float ax = std::fabs(normal.x), ay = std::fabs(normal.y), az = std::fabs(normal.z);
  if (ax > ay) return ax > az ? ObjVec2{pos.y, pos.z} : ObjVec2{pos.x, pos.y};
  return ay > az ? ObjVec2{pos.x, pos.z} : ObjVec2{pos.x, pos.y};
}

// Vertex.GetHashForIndices (ObjMesh.cs:223): (pos << 42) | (normal << 21) | texCoords.
int64_t hashIndices(int pos, int normal, int tex) {
  return ((int64_t)pos << 42) | ((int64_t)normal << 21) | (int64_t)tex;
}

}  // namespace

size_t objBuildDistinctVertices(ObjMesh& mesh, std::vector<ObjVertex>& vertices,
                                std::vector<ObjVec3>& tangents, std::vector<ObjVec3>& binormals) {
  vertices.clear();
  tangents.clear();
  binormals.clear();

  // InitializeVertices (ObjMesh.cs:235): ensure at least one TexCoord (so fallback-UV indices are valid).
  if (mesh.texCoords.empty()) mesh.texCoords.push_back({0.0f, 0.0f});

  // Compute fallback UVs for faces whose three UVs are all zero (ObjMesh.cs:243-274). This MUTATES
  // mesh.texCoords (appends) and the face's V*t indices — exactly as TiXL does.
  auto isZeroUv = [](const ObjVec2& uv) { return uv.u == 0.0f && uv.v == 0.0f; };
  for (size_t i = 0; i < mesh.faces.size(); ++i) {
    ObjFace& face = mesh.faces[i];
    const ObjVec2& uv0 = mesh.texCoords[(size_t)face.v0t];
    const ObjVec2& uv1 = mesh.texCoords[(size_t)face.v1t];
    const ObjVec2& uv2 = mesh.texCoords[(size_t)face.v2t];
    if (!(isZeroUv(uv0) && isZeroUv(uv1) && isZeroUv(uv2))) continue;
    const ObjVec3& n0 = mesh.normals[(size_t)face.v0n];
    const ObjVec3& n1 = mesh.normals[(size_t)face.v1n];
    const ObjVec3& n2 = mesh.normals[(size_t)face.v2n];
    const ObjVec3& p0 = mesh.positions[(size_t)face.v0];
    const ObjVec3& p1 = mesh.positions[(size_t)face.v1];
    const ObjVec3& p2 = mesh.positions[(size_t)face.v2];
    face.v0t = (int)mesh.texCoords.size(); mesh.texCoords.push_back(uvFromPosNormal(p0, n0));
    face.v1t = (int)mesh.texCoords.size(); mesh.texCoords.push_back(uvFromPosNormal(p1, n1));
    face.v2t = (int)mesh.texCoords.size(); mesh.texCoords.push_back(uvFromPosNormal(p2, n2));
  }

  // Build distinct vertices by hashing (pos,normal,uv); reuse on a hash hit (ObjMesh.cs:276-290 +
  // SortInMergedVertex :331). Per new vertex, compute its face's TBN (MeshUtils.CalcTBNSpace).
  std::map<int64_t, int> indexByHash;
  auto sortIn = [&](int posIdx, int normalIdx, int texIdx, const ObjFace& face) {
    int64_t h = hashIndices(posIdx, normalIdx, texIdx);
    auto it = indexByHash.find(h);
    if (it != indexByHash.end()) return;  // already merged
    const ObjVec3& p0 = mesh.positions[(size_t)face.v0];
    const ObjVec3& p1 = mesh.positions[(size_t)face.v1];
    const ObjVec3& p2 = mesh.positions[(size_t)face.v2];
    const ObjVec2& uv0 = mesh.texCoords[(size_t)face.v0t];
    const ObjVec2& uv1 = mesh.texCoords[(size_t)face.v1t];
    const ObjVec2& uv2 = mesh.texCoords[(size_t)face.v2t];
    ObjVec3 tan, bitan;
    calcTBN(p0, uv0, p1, uv1, p2, uv2, mesh.normals[(size_t)normalIdx], tan, bitan);
    int newIndex = (int)vertices.size();
    indexByHash[h] = newIndex;
    binormals.push_back(bitan);
    tangents.push_back(tan);
    vertices.push_back({posIdx, normalIdx, texIdx});
  };
  for (const ObjFace& face : mesh.faces) {
    sortIn(face.v0, face.v0n, face.v0t, face);
    sortIn(face.v1, face.v1n, face.v1t, face);
    sortIn(face.v2, face.v2n, face.v2t, face);
  }

  // Checked-out re-sort by position index (ObjMesh.cs:292-326, fork-objmesh-checked-out-resort): stable
  // sort the distinct vertices by their position index, carrying tangents/binormals along.
  size_t count = vertices.size();
  std::vector<int> order(count);
  for (size_t i = 0; i < count; ++i) order[i] = (int)i;
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
    return vertices[(size_t)a].positionIndex < vertices[(size_t)b].positionIndex;
  });
  std::vector<ObjVertex> rv;
  std::vector<ObjVec3> rt, rb;
  rv.reserve(count); rt.reserve(count); rb.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    int old = order[i];
    rv.push_back(vertices[(size_t)old]);
    rt.push_back(tangents[(size_t)old]);
    rb.push_back(binormals[(size_t)old]);
  }
  vertices.swap(rv);
  tangents.swap(rt);
  binormals.swap(rb);
  return count;
}

}  // namespace sw
