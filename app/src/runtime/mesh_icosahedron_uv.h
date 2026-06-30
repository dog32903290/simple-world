// mesh_icosahedron_uv.h — IcosahedronMesh UV mappers (split out of mesh_ops_icosahedronmesh.cpp to
// keep the cook ≤400 lines; the UV layout is its own responsibility). TiXL authority:
// external/tixl/Operators/Lib/mesh/generate/IcosahedronMesh.cs:355-740.
//
// Five mappers selected by TexCoord / TexCoord2: 0=Faces, 1=Unwrapped, 2=Atlas, 3=FacesSub,
// 4=GridFacesSub. All closed-form (pure functions of vertexIndex=i%3, triangleIndex=i/3, the
// subdivision level, and — for Unwrapped — the vertex position). Header is included by exactly one
// .cpp; functions are `inline` in a detail namespace.
#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

#include "runtime/mesh_op_registry.h"  // meshInjectBug() — RED hook for the seam-fix gate

namespace sw {
namespace ico_uv {

struct UvV2 { float x, y; };

// _baseUvs (IcosahedronMesh.cs:735-740) — the canonical per-face triangle UVs (Faces / FacesSub).
inline const UvV2 kFaceUv[3] = {{0.5f, 1.0f}, {0.067f, 0.250f}, {0.933f, 0.250f}};

inline UvV2 midUv(UvV2 a, UvV2 b) { return UvV2{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}; }
inline int ipow4(int n) { int r = 1; for (int i = 0; i < n; ++i) r *= 4; return r; }

// TessellateUV (IcosahedronMesh.cs:668-719). C# selects the output vertex by `baseUV == base[k]`
// float-equality; baseUV is always base[uvIndex] (no two base entries collide in any face), so we
// select by uvIndex directly — faithful, avoiding fragile float compares.
inline UvV2 tessellateUv(const UvV2 base[3], int uvIndex, int subTriangleIndex, int level) {
  if (level == 0) return base[uvIndex];
  UvV2 v0 = base[0], v1 = base[1], v2 = base[2];
  int currentIndex = subTriangleIndex;
  for (int lvl = level; lvl > 0; --lvl) {
    int trianglesAtThisLevel = ipow4(lvl - 1);
    int quadrant = currentIndex / trianglesAtThisLevel;
    currentIndex = currentIndex % trianglesAtThisLevel;
    UvV2 mid01 = midUv(v0, v1), mid12 = midUv(v1, v2), mid20 = midUv(v2, v0);
    switch (quadrant) {
      case 0: v1 = mid01; v2 = mid20; break;
      case 1: v0 = v1; v1 = mid12; v2 = mid01; break;
      case 2: v0 = v2; v1 = mid20; v2 = mid12; break;
      case 3: v0 = mid01; v1 = mid12; v2 = mid20; break;
    }
  }
  UvV2 cur[3] = {v0, v1, v2};
  return cur[uvIndex];
}

// Atlas.GetBaseTriangleUvs (IcosahedronMesh.cs:619-665).
inline void atlasBaseUvs(int faceIndex, UvV2 out[3]) {
  const float cellWidth = 0.909091f / 5.0f;
  const float yShift = 0.157461f;
  const float maxY = 0.472382f;
  int faceInGroup = faceIndex % 5;
  float xOffset = faceInGroup * cellWidth;
  if (faceIndex < 5) {
    out[0] = {0.09091f + xOffset, 1.0f};
    out[1] = {0.0f + xOffset, 0.314921f / maxY};
    out[2] = {0.181819f + xOffset, 0.314921f / maxY};
  } else if (faceIndex < 10) {
    out[0] = {0.181819f + xOffset, 0.314921f / maxY};
    out[1] = {0.0f + xOffset, 0.314921f / maxY};
    out[2] = {0.090911f + xOffset, 0.157461f / maxY};
  } else if (faceIndex < 15) {
    out[0] = {0.090911f + xOffset + cellWidth * 0.5f, (0.157461f - yShift) / maxY};
    out[1] = {0.181819f + xOffset + cellWidth * 0.5f, (0.314921f - yShift) / maxY};
    out[2] = {0.0f + xOffset + cellWidth * 0.5f, (0.314921f - yShift) / maxY};
  } else {
    out[0] = {0.0f + xOffset + cellWidth * 0.5f, (0.314921f - yShift) / maxY};
    out[1] = {0.181819f + xOffset + cellWidth * 0.5f, (0.314921f - yShift) / maxY};
    out[2] = {0.09091f + xOffset + cellWidth * 0.5f, (0.472382f - yShift) / maxY};
  }
}

// GridFacesSub.GetBaseTriangleUvs (IcosahedronMesh.cs:459-509).
inline void gridBaseUvs(int faceIndex, UvV2 out[3]) {
  const float cellH = 1.0f / 5.0f;
  const float cellV = 1.0f / 2.0f;
  int faceInGroup = faceIndex % 5;
  float xOffset = faceInGroup * cellH + (0.2f - 0.181819f) * 0.5f;  // center horizontally
  if (faceIndex < 5) {
    out[0] = {0.09091f + xOffset, 0.907461f};
    out[1] = {0.0f + xOffset, 0.75f};
    out[2] = {0.181819f + xOffset, 0.75f};
  } else if (faceIndex < 10) {
    out[0] = {0.181819f + xOffset, 0.75f};
    out[1] = {0.0f + xOffset, 0.75f};
    out[2] = {0.090911f + xOffset, 0.59254f};
  } else if (faceIndex < 15) {
    out[0] = {0.09091f + xOffset, 0.907461f - cellV};
    out[1] = {0.0f + xOffset, 0.75f - cellV};
    out[2] = {0.181819f + xOffset, 0.75f - cellV};
  } else {
    out[0] = {0.181819f + xOffset, 0.75f - cellV};
    out[1] = {0.0f + xOffset, 0.75f - cellV};
    out[2] = {0.090911f + xOffset, 0.59254f - cellV};
  }
}

// Unwrapped.Prepare (IcosahedronMesh.cs:518-579) → one UV per output vertex (3*tri + vIdx).
// The C# winding-flip is a verified NO-OP: it swaps uvs[1]/uvs[2] then the fixedIndex remap swaps
// them back, and _flippedTriangles is never read in CalculateUV. So we keep original vertex order;
// only the seam-fix shifts u. CalculateUV's final transform is uv*(-1,1)+(1,0) = (1-u, v).
//   vertsXyz: tightly-packed {x,y,z} per vertex; triIdx: tightly-packed {i0,i1,i2} per triangle.
//   tilt: the icosahedron tilt angle (radians); pi: π.
inline void prepareUnwrapped(const float* vertsXyz, std::size_t vertCount, const int* triIdx,
                             std::size_t triCount, float tilt, float pi, std::vector<UvV2>& uvOut) {
  uvOut.assign(vertCount, UvV2{0.5f, 0.5f});
  float cz = std::cos(-tilt), sz = std::sin(-tilt);  // Rz(-tilt), row-vector v*M
  for (std::size_t t = 0; t < triCount; ++t) {
    const int idx[3] = {triIdx[3 * t + 0], triIdx[3 * t + 1], triIdx[3 * t + 2]};
    UvV2 uvs[3];
    for (int i = 0; i < 3; ++i) {
      const float* p = &vertsXyz[3 * (std::size_t)idx[i]];
      float rx = p[0] * cz - p[1] * sz;
      float ry = p[0] * sz + p[1] * cz;
      float rz = p[2];
      if (ry > 1.0f) ry = 1.0f; else if (ry < -1.0f) ry = -1.0f;  // guard asin domain (|v|=1 ± fp)
      float u = 0.5f + std::atan2(rz, rx) / (2.0f * pi);
      float vc = 0.5f + std::asin(ry) / pi;
      if (u < 0.0f) u += 1.0f;
      if (u >= 1.0f) u -= 1.0f;
      uvs[i] = {u, vc};
    }
    float minU = std::fmin(uvs[0].x, std::fmin(uvs[1].x, uvs[2].x));
    float maxU = std::fmax(uvs[0].x, std::fmax(uvs[1].x, uvs[2].x));
    bool wraps = (maxU - minU) > 0.5f;
    bool applySeam = !meshInjectBug();  // RED hook: --selftest-…-bug drops seam-fix → u-span blows up
    for (int i = 0; i < 3; ++i)
      if (applySeam && wraps && uvs[i].x < 0.5f) uvs[i].x += 1.0f;
    for (int i = 0; i < 3; ++i)
      uvOut[3 * t + i] = {1.0f - uvs[i].x, uvs[i].y};  // CalculateUV flip uv*(-1,1)+(1,0)
  }
}

// GetUvMapper dispatch per vertex (IcosahedronMesh.cs:355-366). vertexIndex=i%3, triangleIndex=i/3.
// mode 1 (Unwrapped) reads the precomputed table; the rest are table + tessellation.
inline UvV2 uvForVertex(int mode, int i, int level, const std::vector<UvV2>& unwrapped) {
  int vIdx = i % 3;
  int triIdx = i / 3;
  switch (mode) {
    case 1:  // Unwrapped
      return ((std::size_t)i < unwrapped.size()) ? unwrapped[i] : UvV2{0.5f, 0.5f};
    case 2: {  // Atlas
      int subPerFace = ipow4(level);
      int face = (triIdx / subPerFace) % 20;
      UvV2 base[3]; atlasBaseUvs(face, base);
      return tessellateUv(base, vIdx, triIdx % subPerFace, level);
    }
    case 3: {  // FacesSub
      int subPerFace = ipow4(level);
      UvV2 base[3] = {kFaceUv[0], kFaceUv[1], kFaceUv[2]};
      return tessellateUv(base, vIdx, triIdx % subPerFace, level);
    }
    case 4: {  // GridFacesSub
      int subPerFace = ipow4(level);
      int face = (triIdx / subPerFace) % 20;
      UvV2 base[3]; gridBaseUvs(face, base);
      return tessellateUv(base, vIdx, triIdx % subPerFace, level);
    }
    default:  // 0 Faces
      return kFaceUv[vIdx];
  }
}

}  // namespace ico_uv
}  // namespace sw
