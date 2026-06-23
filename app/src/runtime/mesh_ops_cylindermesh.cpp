// CylinderMesh mesh op (cylinder / cone / tube generator with optional caps) — rides the built
// mesh-op seam. TiXL authority: external/tixl/Operators/Lib/mesh/generate/CylinderMesh.cs (+ .t3
// defaults Radius=0.5, RadiusOffset=0, Height=1, Rows=1, Columns=32, CapSegments=1, Spin=0, Twist=0,
// Fill=360, Center=0, BasePivot=0.5, Rotation=0). One .cpp owns the whole op.
//
// VERBATIM math (CylinderMesh.cs:23-248):
//   rotationMatrix = CreateFromYawPitchRoll(Rot.Y, Rot.X, Rot.Z) (deg2rad).                       [:24-26]
//   upperRadius = lowerRadius + RadiusOffset ; isFlipped = lowerRadius<0.                          [:28-46]
//   vertexHullColumns = columns+1 ; hullVertices=(rows+1)*vertexHullColumns ; hullTris=rows*columns*2. [:48-51]
//   capsVertexCount = 2*(capSegments*vertexHullColumns + 1) ; capsTriangleCount=2*((capSegments-1)*columns*2+columns). [:53-54]
//   addCaps = capSegments>0. totals add the cap counts only when addCaps.                          [:42,56-57]
//   radiusAngleFraction = fillRatio/(vertexHullColumns-1)*2PI ; squeezeAngle = atan2(upper-lower, height). [:67,70]
//   HULL per (rowIndex 0..rows, columnIndex 0..vertexHullColumns-1):                               [:73-137]
//     heightFraction=rowIndex/rows ; rowRadius=lerp(lower,upper,h) ; rowLevel=height*(h-basePivot).
//     columnAngle = columnIndex*radiusAngleFraction + spin + twist*h + PI.
//     p = (sin(a)*rowRadius, rowLevel, cos(a)*rowRadius) ; then TransformNormal(p, R) + center.
//     normal/binormal/tangent per CylinderMesh.cs:94-104, all TransformNormal'd; flip negates n & bn.
//     face wind depends on isFlipped (CylinderMesh.cs:123-134).
//   CAPS (CylinderMesh.cs:140-248): two fans (lower/upper), each capSegments rings + 1 center vert.
//     Ported 1:1 including the center-vertex + center-segment fan winding.
// FORK (faithful): Texcoord2 = Texcoord (CylinderMesh.cs:115 sets Texcoord2=uv0 on hull verts; cap
// verts leave Texcoord2 at default 0). Color=(1,1,1).
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <cmath>
#include <cstring>
#include <map>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"      // EvaluationContext
#include "runtime/mesh_op_registry.h"  // MeshOp self-registration + MeshCookCtx + cookMeshParam/VecN
#include "runtime/sw_mesh.h"           // SwVertex (80B) + SwTriIndex (12B)

namespace sw {
namespace {

constexpr double kPiD = 3.14159265358979323846;
constexpr float kDeg2Rad = (float)(kPiD / 180.0);

struct V3 { float x, y, z; };
V3 cross(V3 a, V3 b) { return V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// System.Numerics CreateFromYawPitchRoll = Ry(yaw)*Rx(pitch)*Rz(roll), TransformNormal = v * M
// (row-vector). Default rotation (0,0,0) -> identity. (Same composite as NGon/Quad transformNormal.)
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

int clampI(float v, int lo, int hi) {
  int s = (int)(v + (v >= 0 ? 0.5f : -0.5f));
  if (s < lo) s = lo;
  if (s > hi) s = hi;
  return s;
}

// Compute the same totals the cook uses (the count fn needs them before the buffers are sized).
void cylTotals(const std::map<std::string, float>* params, int& totalVtx, int& totalTri,
               int& rows, int& columns, int& vertexHullColumns, int& capSegments, bool& addCaps,
               int& hullVerticesCount, int& hullTriangleCount, int& capsVertexCount,
               int& capsTriangleCount) {
  rows = clampI(cookMeshParam(params, "Rows", 1.0f), 1, 10000);
  columns = clampI(cookMeshParam(params, "Columns", 32.0f), 1, 10000);
  capSegments = clampI(cookMeshParam(params, "CapSegments", 1.0f), 0, 1000);
  addCaps = capSegments > 0;
  vertexHullColumns = columns + 1;
  hullVerticesCount = (rows + 1) * vertexHullColumns;
  hullTriangleCount = rows * columns * 2;
  capsVertexCount = 2 * (capSegments * vertexHullColumns + 1);
  capsTriangleCount = 2 * ((capSegments - 1) * columns * 2 + columns);
  totalVtx = hullVerticesCount + (addCaps ? capsVertexCount : 0);
  totalTri = hullTriangleCount + (addCaps ? capsTriangleCount : 0);
}

void cylinderCount(const std::map<std::string, float>* params, const SwMeshView* /*inputs*/,
                   int /*inputCount*/, uint32_t& vtx, uint32_t& idx) {
  int tv, tt, rows, cols, vhc, caps, hv, ht, cv, ct;
  bool ac;
  cylTotals(params, tv, tt, rows, cols, vhc, caps, ac, hv, ht, cv, ct);
  vtx = (uint32_t)tv;
  idx = (uint32_t)tt;
}

void cylinderCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  float rotDef[3] = {0, 0, 0};
  float rot[3]; cookMeshVecN(c.params, "Rotation", rotDef, 3, rot);
  float yaw = rot[1] * kDeg2Rad, pitch = rot[0] * kDeg2Rad, roll = rot[2] * kDeg2Rad;

  float lowerRadius = cookMeshParam(c.params, "Radius", 0.5f);
  float upperRadius = lowerRadius + cookMeshParam(c.params, "RadiusOffset", 0.0f);
  float height = cookMeshParam(c.params, "Height", 1.0f);
  float centerDef[3] = {0, 0, 0};
  float center[3]; cookMeshVecN(c.params, "Center", centerDef, 3, center);
  V3 centerV{center[0], center[1], center[2]};
  float spinInRad = cookMeshParam(c.params, "Spin", 0.0f) * kDeg2Rad;
  float twistInRad = cookMeshParam(c.params, "Twist", 0.0f) * kDeg2Rad;
  float basePivot = cookMeshParam(c.params, "BasePivot", 0.5f);
  float fillRatio = cookMeshParam(c.params, "Fill", 360.0f) / 360.0f;
  bool isFlipped = lowerRadius < 0;

  int tv, tt, rows, columns, vertexHullColumns, capSegments, hullVerticesCount, hullTriangleCount,
      capsVertexCount, capsTriangleCount;
  bool addCaps;
  cylTotals(c.params, tv, tt, rows, columns, vertexHullColumns, capSegments, addCaps,
            hullVerticesCount, hullTriangleCount, capsVertexCount, capsTriangleCount);

  SwVertex* verts = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* idx = (SwTriIndex*)c.output_indices->contents();
  std::memset(verts, 0, (size_t)c.vertexCount * sizeof(SwVertex));
  std::memset(idx, 0, (size_t)c.indexCount * sizeof(SwTriIndex));

  double radiusAngleFraction = (double)fillRatio / (vertexHullColumns - 1) * 2.0 * kPiD;
  float squeezeAngle = std::atan2(upperRadius - lowerRadius, height);

  auto addV3 = [](V3 a, V3 b) { return V3{a.x + b.x, a.y + b.y, a.z + b.z}; };
  auto neg = [](V3 a) { return V3{-a.x, -a.y, -a.z}; };

  // HULL
  for (int rowIndex = 0; rowIndex < rows + 1; ++rowIndex) {
    float heightFraction = rowIndex / (float)rows;
    float rowRadius = lerpf(lowerRadius, upperRadius, heightFraction);
    float rowLevel = height * (heightFraction - basePivot);
    for (int columnIndex = 0; columnIndex < vertexHullColumns; ++columnIndex) {
      float columnAngle = (float)(columnIndex * radiusAngleFraction + spinInRad +
                                  twistInRad * heightFraction + kPiD);
      float u0 = columnIndex / (float)columns;
      float v1 = addCaps ? (rowIndex / (float)rows) / 2.0f : rowIndex / (float)rows;
      V3 p = {std::sin(columnAngle) * rowRadius, rowLevel, std::cos(columnAngle) * rowRadius};
      V3 normal0 = {std::sin(columnAngle) * std::cos(squeezeAngle),
                    std::cos(-squeezeAngle - (float)kPiD / 2.0f),
                    std::cos(columnAngle) * std::cos(squeezeAngle)};
      V3 binormal0 = {std::sin(squeezeAngle) * std::sin(columnAngle), std::cos(-squeezeAngle),
                      std::sin(squeezeAngle) * std::cos(columnAngle)};
      V3 tangent0 = cross(neg(normal0), binormal0);
      int vertexIndex = rowIndex * vertexHullColumns + columnIndex;
      p = transformNormal(p, yaw, pitch, roll);
      V3 pos = addV3(p, centerV);
      V3 n = transformNormal(isFlipped ? neg(normal0) : normal0, yaw, pitch, roll);
      V3 t = transformNormal(tangent0, yaw, pitch, roll);
      V3 bn = transformNormal(isFlipped ? neg(binormal0) : binormal0, yaw, pitch, roll);
      SwVertex& v = verts[vertexIndex];
      v.Position = {pos.x, pos.y, pos.z};
      v.Normal = {n.x, n.y, n.z};
      v.Tangent = {t.x, t.y, t.z};
      v.Bitangent = {bn.x, bn.y, bn.z};
      v.Texcoord = {u0, v1};
      v.Texcoord2 = {u0, v1};  // CylinderMesh.cs:115 sets Texcoord2 = uv0
      v.Selection = 1.0f;
      v.ColorRgb = {1, 1, 1};

      int faceIndex = 2 * (rowIndex * (vertexHullColumns - 1) + columnIndex);
      if (columnIndex < vertexHullColumns - 1 && rowIndex < rows) {
        if (isFlipped) {
          idx[faceIndex + 0] = {vertexIndex + vertexHullColumns, vertexIndex + 1, vertexIndex + 0};
          idx[faceIndex + 1] = {vertexIndex + vertexHullColumns + 1, vertexIndex + 1,
                                vertexIndex + vertexHullColumns};
        } else {
          idx[faceIndex + 0] = {vertexIndex + 0, vertexIndex + 1, vertexIndex + vertexHullColumns};
          idx[faceIndex + 1] = {vertexIndex + vertexHullColumns, vertexIndex + 1,
                                vertexIndex + vertexHullColumns + 1};
        }
      }
    }
  }

  // CAPS
  if (addCaps) {
    for (int capIndex = 0; capIndex <= 1; ++capIndex) {
      bool isLowerCap = capIndex == 0;
      float capLevel = ((isLowerCap ? 0.0f : 1.0f) - basePivot) * height;
      float capRadius = isLowerCap ? lowerRadius : upperRadius;
      bool isReverse = isFlipped ^ isLowerCap;
      int centerVertexIndex = hullVerticesCount + (capsVertexCount / 2) * (capIndex + 1) - 1;
      for (int capSegmentIndex = 0; capSegmentIndex < capSegments; ++capSegmentIndex) {
        bool isCenterSegment = capSegmentIndex == capSegments - 1;
        float capFraction = 1.0f - capSegmentIndex / (float)capSegments;
        float radius = capRadius * capFraction;
        for (int columnIndex = 0; columnIndex < vertexHullColumns; ++columnIndex) {
          float columnAngle = (float)(columnIndex * radiusAngleFraction + spinInRad +
                                      twistInRad * (isLowerCap ? 0.0f : 1.0f));
          float xx = std::sin(columnAngle), yy = std::cos(columnAngle);
          V3 p = {-xx * radius, capLevel, -yy * radius};
          V3 normal0 = isLowerCap ? V3{0, -1, 0} : V3{0, 1, 0};  // Down / Up
          V3 tangent0 = {-1, 0, 0};                              // VectorT3.Left
          V3 binormal0 = {0, 0, -1};                             // VectorT3.ForwardRH
          int vertexIndex = capSegmentIndex * vertexHullColumns + columnIndex + hullVerticesCount +
                            (capsVertexCount / 2) * capIndex;
          float capUvU = isLowerCap ? 0.25f : 0.75f, capUvV = 0.75f;
          p = transformNormal(p, yaw, pitch, roll);
          V3 pos = addV3(p, centerV);
          SwVertex& v = verts[vertexIndex];
          v.Position = {pos.x, pos.y, pos.z};
          V3 nC = isFlipped ? neg(normal0) : normal0;
          v.Normal = {nC.x, nC.y, nC.z};
          v.Tangent = {tangent0.x, tangent0.y, tangent0.z};
          V3 bnC = isFlipped ? neg(binormal0) : binormal0;
          v.Bitangent = {bnC.x, bnC.y, bnC.z};
          v.Texcoord = {-xx * (isLowerCap ? -1.0f : 1.0f) * capFraction / 4.0f + capUvU,
                        yy * capFraction / 4.0f + capUvV};
          v.Selection = 1.0f;
          v.ColorRgb = {1, 1, 1};

          if (isCenterSegment && columnIndex == 0) {
            V3 p2 = {0, capLevel, 0};
            p2 = transformNormal(p2, yaw, pitch, roll);
            V3 pos2 = addV3(p2, centerV);
            SwVertex& vc = verts[centerVertexIndex];
            vc.Position = {pos2.x, pos2.y, pos2.z};
            vc.Normal = {nC.x, nC.y, nC.z};
            vc.Tangent = {tangent0.x, tangent0.y, tangent0.z};
            V3 bnCtr = (isFlipped ^ isLowerCap) ? neg(binormal0) : binormal0;
            vc.Bitangent = {bnCtr.x, bnCtr.y, bnCtr.z};
            vc.Texcoord = {capUvU, capUvV};
            vc.Selection = 1.0f;
            vc.ColorRgb = {1, 1, 1};
          }

          if (columnIndex < vertexHullColumns - 1 && capSegmentIndex < capSegments) {
            if (isCenterSegment) {
              int faceIndex = (capSegmentIndex * (vertexHullColumns - 1) * 2 + columnIndex) +
                              hullTriangleCount + (capsTriangleCount / 2) * capIndex;
              idx[faceIndex] = isReverse
                                   ? SwTriIndex{vertexIndex + 1, vertexIndex, centerVertexIndex}
                                   : SwTriIndex{centerVertexIndex, vertexIndex, vertexIndex + 1};
            } else {
              int faceIndex = (capSegmentIndex * (vertexHullColumns - 1) * 2) + columnIndex * 2 +
                              hullTriangleCount + (capsTriangleCount / 2) * capIndex;
              idx[faceIndex] = isReverse
                                   ? SwTriIndex{vertexIndex + vertexHullColumns, vertexIndex + 1, vertexIndex}
                                   : SwTriIndex{vertexIndex, vertexIndex + 1, vertexIndex + vertexHullColumns};
              idx[faceIndex + 1] =
                  isReverse ? SwTriIndex{vertexIndex + vertexHullColumns,
                                         vertexIndex + vertexHullColumns + 1, vertexIndex + 1}
                            : SwTriIndex{vertexIndex + 1, vertexIndex + vertexHullColumns + 1,
                                         vertexIndex + vertexHullColumns};
            }
          }
        }
      }
    }
  }

  // Test injection (golden RED): corrupt the first hull vertex position in the REAL output.
  if (meshInjectBug() && c.vertexCount > 0) verts[0].Position = {-999.0f, -999.0f, -999.0f};
}

NodeSpec cylinderSpec() {
  NodeSpec s;
  s.type = "CylinderMesh";
  s.title = "Cylinder Mesh";
  PortSpec rad; rad.id = "Radius"; rad.name = "Radius"; rad.dataType = "Float"; rad.isInput = true;
  rad.def = 0.5f; rad.minV = -10.0f; rad.maxV = 10.0f;
  PortSpec rof; rof.id = "RadiusOffset"; rof.name = "RadiusOffset"; rof.dataType = "Float"; rof.isInput = true;
  rof.def = 0.0f; rof.minV = -10.0f; rof.maxV = 10.0f;
  PortSpec hgt; hgt.id = "Height"; hgt.name = "Height"; hgt.dataType = "Float"; hgt.isInput = true;
  hgt.def = 1.0f; hgt.minV = 0.0f; hgt.maxV = 10.0f;
  PortSpec row; row.id = "Rows"; row.name = "Rows"; row.dataType = "Float"; row.isInput = true;
  row.def = 1.0f; row.minV = 1.0f; row.maxV = 256.0f;
  PortSpec col; col.id = "Columns"; col.name = "Columns"; col.dataType = "Float"; col.isInput = true;
  col.def = 32.0f; col.minV = 1.0f; col.maxV = 256.0f;
  PortSpec cap; cap.id = "CapSegments"; cap.name = "CapSegments"; cap.dataType = "Float"; cap.isInput = true;
  cap.def = 1.0f; cap.minV = 0.0f; cap.maxV = 64.0f;
  PortSpec spn; spn.id = "Spin"; spn.name = "Spin"; spn.dataType = "Float"; spn.isInput = true;
  spn.def = 0.0f; spn.minV = -360.0f; spn.maxV = 360.0f;
  PortSpec twi; twi.id = "Twist"; twi.name = "Twist"; twi.dataType = "Float"; twi.isInput = true;
  twi.def = 0.0f; twi.minV = -360.0f; twi.maxV = 360.0f;
  PortSpec fil; fil.id = "Fill"; fil.name = "Fill"; fil.dataType = "Float"; fil.isInput = true;
  fil.def = 360.0f; fil.minV = 0.0f; fil.maxV = 360.0f;
  PortSpec ctx; ctx.id = "Center.x"; ctx.name = "Center"; ctx.dataType = "Float"; ctx.isInput = true;
  ctx.def = 0.0f; ctx.minV = -10.0f; ctx.maxV = 10.0f; ctx.widget = Widget::Vec; ctx.vecArity = 3;
  PortSpec cty; cty.id = "Center.y"; cty.name = "Center.y"; cty.dataType = "Float"; cty.isInput = true;
  cty.def = 0.0f; cty.minV = -10.0f; cty.maxV = 10.0f;
  PortSpec ctz; ctz.id = "Center.z"; ctz.name = "Center.z"; ctz.dataType = "Float"; ctz.isInput = true;
  ctz.def = 0.0f; ctz.minV = -10.0f; ctz.maxV = 10.0f;
  PortSpec bpv; bpv.id = "BasePivot"; bpv.name = "BasePivot"; bpv.dataType = "Float"; bpv.isInput = true;
  bpv.def = 0.5f; bpv.minV = 0.0f; bpv.maxV = 1.0f;
  PortSpec rtx; rtx.id = "Rotation.x"; rtx.name = "Rotation"; rtx.dataType = "Float"; rtx.isInput = true;
  rtx.def = 0.0f; rtx.minV = -360.0f; rtx.maxV = 360.0f; rtx.widget = Widget::Vec; rtx.vecArity = 3;
  PortSpec rty; rty.id = "Rotation.y"; rty.name = "Rotation.y"; rty.dataType = "Float"; rty.isInput = true;
  rty.def = 0.0f; rty.minV = -360.0f; rty.maxV = 360.0f;
  PortSpec rtz; rtz.id = "Rotation.z"; rtz.name = "Rotation.z"; rtz.dataType = "Float"; rtz.isInput = true;
  rtz.def = 0.0f; rtz.minV = -360.0f; rtz.maxV = 360.0f;
  PortSpec out; out.id = "Data"; out.name = "Data"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {rad, rof, hgt, row, col, cap, spn, twi, fil, ctx, cty, ctz, bpv, rtx, rty, rtz, out};
  return s;
}

const MeshOp g_cylinderMeshOp(cylinderSpec(), cylinderCount, cylinderCook);

}  // namespace
}  // namespace sw
