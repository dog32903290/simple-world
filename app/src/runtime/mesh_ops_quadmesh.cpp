// QuadMesh mesh op (subdivided plane / grid generator) — the 4th cook flow's second proving leaf.
// TiXL authority: external/tixl/Operators/Lib/mesh/generate/QuadMesh.cs (+ defaults). One .cpp owns
// the whole op (NodeSpec + MeshCountFn + MeshCookFn) via the MeshOp self-registration seam; the base
// machinery is FROZEN.
//
// VERBATIM math (QuadMesh.cs:38-107):
//   columns = SegW+1, rows = SegH+1;  verticesCount = columns*rows, faceCount = (columns-1)(rows-1)*2.
//   offset      = (stretch.X*scale*(pivot.X-0.5), stretch.Y*scale*(pivot.Y-0.5), 0).            [:38-40]
//   columnStep  = scale*stretch.X / (columns-1);  rowStep = scale*stretch.Y / (rows-1).         [:58-59]
//   vertex[row + col*rows].Position = TransformNormal((col*colStep, row*rowStep, 0)+offset, R)+center. [:82-94]
//   per cell (skip last col/row): faceIdx = 2*(row + col*(rows-1));                              [:83]
//     index[faceIdx+0] = Int3(v, v+rows, v+1);  index[faceIdx+1] = Int3(v+rows, v+rows+1, v+1). [:106-107]
// Vertex fields set by TiXL (QuadMesh.cs:92-101): Position, Normal=ForwardLH, Tangent=Right,
// Bitangent=Up (rotation-transformed), Texcoord (u0,v0), Selection=1, ColorRgb=(1,1,1).
// Texcoord2 NOT set -> stays default 0 (named fork, same as NGonMesh).
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <cmath>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"      // EvaluationContext
#include "runtime/mesh_op_registry.h"  // MeshOp self-registration + MeshCookCtx + cookMeshParam/VecN
#include "runtime/sw_mesh.h"           // SwVertex (80B) + SwTriIndex (12B)

namespace sw {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;

struct V3 { float x, y, z; };
// Identical composite to NGonMesh's transformNormal (System.Numerics CreateFromYawPitchRoll, row-vec).
V3 transformNormal(V3 v, float yaw, float pitch, float roll) {
  float cy = std::cos(yaw), sy = std::sin(yaw);
  float cx = std::cos(pitch), sx = std::sin(pitch);
  float cz = std::cos(roll), sz = std::sin(roll);
  float m11 = cy * cz + sy * sx * sz;
  float m12 = cx * sz;
  float m13 = -sy * cz + cy * sx * sz;
  float m21 = -cy * sz + sy * sx * cz;
  float m22 = cx * cz;
  float m23 = sy * sz + cy * sx * cz;
  float m31 = sy * cx;
  float m32 = -sx;
  float m33 = cy * cx;
  return V3{v.x * m11 + v.y * m21 + v.z * m31, v.x * m12 + v.y * m22 + v.z * m32,
            v.x * m13 + v.y * m23 + v.z * m33};
}

int clampSeg(float v) {
  int s = (int)(v + 0.5f);
  if (s < 1) s = 1;
  if (s > 10000) s = 10000;
  return s;
}

// QuadMesh.cs:44-49 — columns = SegW+1, rows = SegH+1; verts = cols*rows, faces = (cols-1)(rows-1)*2.
// A generator OWNS no Mesh input, so the gathered-inputs args (mesh-input seam, fork-mesh-1) are
// IGNORED — the count stays a pure function of params, byte-identical to before the signature widen.
void quadCount(const std::map<std::string, float>* params, const SwMeshView* /*inputs*/,
               int /*inputCount*/, uint32_t& vtx, uint32_t& idx) {
  int columns = clampSeg(cookMeshParam(params, "Segments.x", 1.0f)) + 1;
  int rows = clampSeg(cookMeshParam(params, "Segments.y", 1.0f)) + 1;
  vtx = (uint32_t)(columns * rows);
  idx = (uint32_t)((columns - 1) * (rows - 1) * 2);
}

void quadCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  int columns = clampSeg(cookMeshParam(c.params, "Segments.x", 1.0f)) + 1;
  int rows = clampSeg(cookMeshParam(c.params, "Segments.y", 1.0f)) + 1;
  float scale = cookMeshParam(c.params, "Scale", 1.0f);
  float stretchDef[2] = {1.0f, 1.0f};
  float stretch[2];
  cookMeshVecN(c.params, "Stretch", stretchDef, 2, stretch);
  float pivotDef[2] = {0.5f, 0.5f};
  float pivot[2];
  cookMeshVecN(c.params, "Pivot", pivotDef, 2, pivot);
  float rotDef[3] = {0.0f, 0.0f, 0.0f};
  float rot[3];  // (X=pitch, Y=yaw, Z=roll) DEGREES (QuadMesh.cs:28-30)
  cookMeshVecN(c.params, "Rotation", rotDef, 3, rot);
  float centerDef[3] = {0.0f, 0.0f, 0.0f};
  float center[3];
  cookMeshVecN(c.params, "Center", centerDef, 3, center);

  float yaw = rot[1] * kDeg2Rad, pitch = rot[0] * kDeg2Rad, roll = rot[2] * kDeg2Rad;
  V3 normal = transformNormal(V3{0, 0, 1}, yaw, pitch, roll);   // ForwardLH
  V3 tangent = transformNormal(V3{1, 0, 0}, yaw, pitch, roll);  // Right
  V3 binormal = transformNormal(V3{0, 1, 0}, yaw, pitch, roll); // Up

  // offset (QuadMesh.cs:38-40) + steps (QuadMesh.cs:58-59). columns/rows >= 2 here (SegW/H >= 1).
  V3 offset = {stretch[0] * scale * (pivot[0] - 0.5f), stretch[1] * scale * (pivot[1] - 0.5f), 0.0f};
  double columnStep = (double)(scale * stretch[0]) / (double)(columns - 1);
  double rowStep = (double)(scale * stretch[1]) / (double)(rows - 1);

  SwVertex* verts = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* idx = (SwTriIndex*)c.output_indices->contents();

  for (int columnIndex = 0; columnIndex < columns; ++columnIndex) {
    float columnFragment = (float)(columnIndex * columnStep);
    float u0 = columnIndex / ((float)columns - 1.0f);
    for (int rowIndex = 0; rowIndex < rows; ++rowIndex) {
      float rowFragment = (float)(rowIndex * rowStep);
      int vertexIndex = rowIndex + columnIndex * rows;
      int faceIndex = 2 * (rowIndex + columnIndex * (rows - 1));
      V3 p = {columnFragment, rowFragment, 0.0f};
      float v0 = rowIndex / ((float)rows - 1.0f);
      V3 pr = transformNormal(V3{p.x + offset.x, p.y + offset.y, p.z + offset.z}, yaw, pitch, roll);
      SwVertex& v = verts[vertexIndex];
      v.Position = {pr.x + center[0], pr.y + center[1], pr.z + center[2]};
      v.Normal = {normal.x, normal.y, normal.z};
      v.Tangent = {tangent.x, tangent.y, tangent.z};
      v.Bitangent = {binormal.x, binormal.y, binormal.z};
      v.Texcoord = {u0, v0};
      v.Texcoord2 = {0.0f, 0.0f};  // FORK: QuadMesh leaves Texcoord2 at PbrVertex default 0.
      v.Selection = 1.0f;
      v.ColorRgb = {1.0f, 1.0f, 1.0f};  // Vector3.One

      // skip the last column/row (no cell to the +col/+row) — QuadMesh.cs:103-104.
      if (columnIndex >= columns - 1 || rowIndex >= rows - 1) continue;
      // two tris per cell (QuadMesh.cs:106-107).
      idx[faceIndex + 0].X = vertexIndex;        idx[faceIndex + 0].Y = vertexIndex + rows;     idx[faceIndex + 0].Z = vertexIndex + 1;
      idx[faceIndex + 1].X = vertexIndex + rows; idx[faceIndex + 1].Y = vertexIndex + rows + 1; idx[faceIndex + 1].Z = vertexIndex + 1;
    }
  }

  // Test injection (golden RED): corrupt the FIRST face's index triple in the REAL output so the
  // golden's exact-winding assertion fires on the actual cook path.
  if (meshInjectBug() && c.indexCount > 0) { idx[0].X = 99; idx[0].Y = 99; idx[0].Z = 99; }
}

NodeSpec quadSpec() {
  NodeSpec s;
  s.type = "QuadMesh";
  s.title = "Quad Mesh";
  // Segments = Int2 (Width, Height) -> Vec2 of two Float ports (default (1,1) = a single quad).
  // Stretch = Vec2 (default (1,1)). Scale = float (default 1). Pivot = Vec2 (default (0.5,0.5)).
  // Center = Vec3 (default 0). Rotation = Vec3 (default 0).
  PortSpec sgx; sgx.id = "Segments.x"; sgx.name = "Segments"; sgx.dataType = "Float"; sgx.isInput = true;
  sgx.def = 1.0f; sgx.minV = 1.0f; sgx.maxV = 64.0f; sgx.widget = Widget::Vec; sgx.vecArity = 2;
  PortSpec sgy; sgy.id = "Segments.y"; sgy.name = "Segments.y"; sgy.dataType = "Float"; sgy.isInput = true;
  sgy.def = 1.0f; sgy.minV = 1.0f; sgy.maxV = 64.0f;
  PortSpec stx; stx.id = "Stretch.x"; stx.name = "Stretch"; stx.dataType = "Float"; stx.isInput = true;
  stx.def = 1.0f; stx.minV = 0.0f; stx.maxV = 10.0f; stx.widget = Widget::Vec; stx.vecArity = 2;
  PortSpec sty; sty.id = "Stretch.y"; sty.name = "Stretch.y"; sty.dataType = "Float"; sty.isInput = true;
  sty.def = 1.0f; sty.minV = 0.0f; sty.maxV = 10.0f;
  PortSpec scl; scl.id = "Scale"; scl.name = "Scale"; scl.dataType = "Float"; scl.isInput = true;
  scl.def = 1.0f; scl.minV = 0.0f; scl.maxV = 10.0f;
  PortSpec pvx; pvx.id = "Pivot.x"; pvx.name = "Pivot"; pvx.dataType = "Float"; pvx.isInput = true;
  pvx.def = 0.5f; pvx.minV = 0.0f; pvx.maxV = 1.0f; pvx.widget = Widget::Vec; pvx.vecArity = 2;
  PortSpec pvy; pvy.id = "Pivot.y"; pvy.name = "Pivot.y"; pvy.dataType = "Float"; pvy.isInput = true;
  pvy.def = 0.5f; pvy.minV = 0.0f; pvy.maxV = 1.0f;
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
  PortSpec out; out.id = "Data"; out.name = "Data"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {sgx, sgy, stx, sty, scl, pvx, pvy, ctx, cty, ctz, rtx, rty, rtz, out};
  return s;
}

const MeshOp g_quadMeshOp(quadSpec(), quadCount, quadCook);

}  // namespace
}  // namespace sw
