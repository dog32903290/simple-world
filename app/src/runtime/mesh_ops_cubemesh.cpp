// CubeMesh mesh op (6-sided box generator) — rides the built mesh-op seam. TiXL authority:
// external/tixl/Operators/Lib/mesh/generate/CubeMesh.cs (+ .t3 defaults Scale=1, Stretch=(1,1,1),
// Pivot=(0,0,0), Center=0, Segments=(1,1,1), TexCoord=0/Standard, Margin=0). One .cpp owns the op.
//
// VERBATIM math (CubeMesh.cs:19-163): six sides, each a QuadMesh-like grid in its own rotated frame.
//   cubeRotationMatrix = CreateFromYawPitchRoll(Rot.Y, Rot.X, Rot.Z) (deg2rad).                   [:28-30]
//   offset = -Vector3.One*0.5 (constant) ; segments per axis = clamp(seg,1,1e4)+1.                [:44-51]
//   faceCount = ((y-1)(x-1) + (y-1)(z-1) + (x-1)(z-1)) * 2*2 ; verticesCount = (y*x + y*z + x*z)*2. [:53-57]
//   Per side: sideRotationMatrix = CreateFromYawPitchRoll(side.SideRotation.Y,.X,.Z) ;            [:73-75]
//     columnCount/rowCount/columnStretch/rowStretch from the side's Column/Row/Depth axis ;        [:79-83]
//     columnStep = 1/(columnCount-1) ; rowStep = 1/(rowCount-1) ; depthScale = 1.                  [:85-87]
//     normal/tangent/binormal = TransformNormal(ForwardLH/Right/Up, sideRot*cubeRot).              [:89-91]
//     p = (columnFragment, rowFragment, depthScale) ;                                               [:106-108]
//     position = (TransformNormal(p+offset, sideRot) + pivot) * stretch * scale ;                  [:115]
//     position = TransformNormal(position, cubeRot) ; final = position + center.                    [:116,120]
//     face wind = (vi, vi+rowCount, vi+1) , (vi+rowCount, vi+rowCount+1, vi+1).                     [:133-134]
//
// FORK (named): only the StandardUvMapper (TexCoord mode 0, the .t3 default) is ported. CubeMesh's
// other UV modes (1=UnwrappedCube, 2=CubeMap, 3=CubeMapSquare) are texture-atlas layouts; they are
// DEFERRED and fall back to Standard. At default margin=0 Standard is identity uv (u,v). Texcoord2
// likewise uses Standard. This is a parity-narrowing on UV only — positions/topology are 1:1.
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

// System.Numerics CreateFromYawPitchRoll = Ry(yaw)*Rx(pitch)*Rz(roll); TransformNormal = v*M.
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

enum Axis { AX = 0, AY = 1, AZ = 2 };
struct Side { float rotX, rotY, rotZ; Axis columnAxis, rowAxis, depthAxis; };
// CubeMesh.cs:209-289 _sides table. SideRotation given as (X=pitch, Y=yaw, Z=roll) RADIANS.
const Side kSides[6] = {
    {0, 0, 0, AX, AY, AZ},                            // Front
    {0, (float)(kPiD * 0.5), 0, AZ, AY, AX},          // Right
    {0, (float)kPiD, 0, AX, AY, AZ},                  // Back
    {0, (float)(kPiD * 1.5), 0, AZ, AY, AX},          // Left
    {(float)(kPiD * 0.5), 0, 0, AX, AZ, AY},          // Top
    {(float)(kPiD * 1.5), 0, 0, AX, AZ, AY},          // Bottom
};

int clampSeg1(float v) {
  int s = (int)(v + 0.5f);
  if (s < 1) s = 1;
  if (s > 10000) s = 10000;
  return s;
}

void cubeCounts(const std::map<std::string, float>* params, int& xs, int& ys, int& zs, int& vtx,
                int& tri) {
  xs = clampSeg1(cookMeshParam(params, "Segments.x", 1.0f)) + 1;
  ys = clampSeg1(cookMeshParam(params, "Segments.y", 1.0f)) + 1;
  zs = clampSeg1(cookMeshParam(params, "Segments.z", 1.0f)) + 1;
  tri = ((ys - 1) * (xs - 1) + (ys - 1) * (zs - 1) + (xs - 1) * (zs - 1)) * 2 * 2;
  vtx = (ys * xs + ys * zs + xs * zs) * 2;
}

void cubeCount(const std::map<std::string, float>* params, const SwMeshView* /*inputs*/,
               int /*inputCount*/, uint32_t& vtx, uint32_t& idx) {
  int xs, ys, zs, v, t;
  cubeCounts(params, xs, ys, zs, v, t);
  vtx = (uint32_t)v;
  idx = (uint32_t)t;
}

void cubeCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  float scale = cookMeshParam(c.params, "Scale", 1.0f);
  float stretchDef[3] = {1, 1, 1};
  float stretch[3]; cookMeshVecN(c.params, "Stretch", stretchDef, 3, stretch);
  float pivotDef[3] = {0, 0, 0};
  float pivot[3]; cookMeshVecN(c.params, "Pivot", pivotDef, 3, pivot);
  float rotDef[3] = {0, 0, 0};
  float rot[3]; cookMeshVecN(c.params, "Rotation", rotDef, 3, rot);
  float centerDef[3] = {0, 0, 0};
  float center[3]; cookMeshVecN(c.params, "Center", centerDef, 3, center);
  float margin = cookMeshParam(c.params, "Margin", 0.0f);
  float cyaw = rot[1] * kDeg2Rad, cpitch = rot[0] * kDeg2Rad, croll = rot[2] * kDeg2Rad;

  int xSeg, ySeg, zSeg, totalV, totalT;
  cubeCounts(c.params, xSeg, ySeg, zSeg, totalV, totalT);
  auto segForAxis = [&](Axis a) { return a == AX ? xSeg : a == AY ? ySeg : zSeg; };
  auto compForAxis = [&](Axis a) { return a == AX ? stretch[0] : a == AY ? stretch[1] : stretch[2]; };
  (void)compForAxis;

  // StandardUvMapper (CubeMesh.cs:308-318): u,v scaled toward (0.5,0.5) by (1-2*margin).
  auto stdUv = [&](float u, float v, float* out) {
    out[0] = 0.5f + (u - 0.5f) * (1.0f - 2.0f * margin);
    out[1] = 0.5f + (v - 0.5f) * (1.0f - 2.0f * margin);
  };

  SwVertex* verts = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* idx = (SwTriIndex*)c.output_indices->contents();
  std::memset(verts, 0, (size_t)c.vertexCount * sizeof(SwVertex));
  std::memset(idx, 0, (size_t)c.indexCount * sizeof(SwTriIndex));

  int sideVertexIndex = 0, sideFaceIndex = 0;
  for (int sideIndex = 0; sideIndex < 6; ++sideIndex) {
    const Side& side = kSides[sideIndex];
    // sideRotationMatrix = CreateFromYawPitchRoll(SideRotation.Y, .X, .Z). The combined rotation
    // used for normals = sideRot * cubeRot; we apply normals via two TransformNormal passes.
    int columnCount = segForAxis(side.columnAxis);
    int rowCount = segForAxis(side.rowAxis);
    double columnStep = 1.0 / (columnCount - 1);
    double rowStep = 1.0 / (rowCount - 1);
    float depthScale = 1.0f;

    V3 normal = transformNormal(transformNormal(V3{0, 0, 1}, side.rotY, side.rotX, side.rotZ),
                                cyaw, cpitch, croll);
    V3 tangent = transformNormal(transformNormal(V3{1, 0, 0}, side.rotY, side.rotX, side.rotZ),
                                 cyaw, cpitch, croll);
    V3 binormal = transformNormal(transformNormal(V3{0, 1, 0}, side.rotY, side.rotX, side.rotZ),
                                  cyaw, cpitch, croll);

    for (int columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
      float columnFragment = (float)(columnIndex * columnStep);
      float u0 = columnIndex / ((float)columnCount - 1.0f);
      for (int rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
        float rowFragment = (float)(rowIndex * rowStep);
        int vertexIndex = rowIndex + columnIndex * rowCount + sideVertexIndex;
        int faceIndex = 2 * (rowIndex + columnIndex * (rowCount - 1)) + sideFaceIndex;
        V3 p = {columnFragment, rowFragment, depthScale};
        float v0 = rowIndex / ((float)rowCount - 1.0f);
        float uv0[2]; stdUv(u0, v0, uv0);

        // position = (TransformNormal(p+offset, sideRot) + pivot) * stretch * scale  (offset=-0.5).
        V3 pOff = {p.x - 0.5f, p.y - 0.5f, p.z - 0.5f};
        V3 ps = transformNormal(pOff, side.rotY, side.rotX, side.rotZ);
        V3 position = {(ps.x + pivot[0]) * stretch[0] * scale, (ps.y + pivot[1]) * stretch[1] * scale,
                       (ps.z + pivot[2]) * stretch[2] * scale};
        position = transformNormal(position, cyaw, cpitch, croll);

        SwVertex& v = verts[vertexIndex];
        v.Position = {position.x + center[0], position.y + center[1], position.z + center[2]};
        v.Normal = {normal.x, normal.y, normal.z};
        v.Tangent = {tangent.x, tangent.y, tangent.z};
        v.Bitangent = {binormal.x, binormal.y, binormal.z};
        v.Texcoord = {uv0[0], uv0[1]};
        v.Texcoord2 = {uv0[0], uv0[1]};  // FORK: Standard mapper for both (see header).
        v.Selection = 1.0f;
        v.ColorRgb = {1, 1, 1};

        if (columnIndex >= columnCount - 1 || rowIndex >= rowCount - 1) continue;
        idx[faceIndex + 0] = {vertexIndex, vertexIndex + rowCount, vertexIndex + 1};
        idx[faceIndex + 1] = {vertexIndex + rowCount, vertexIndex + rowCount + 1, vertexIndex + 1};
      }
    }
    sideVertexIndex += columnCount * rowCount;
    sideFaceIndex += (columnCount - 1) * (rowCount - 1) * 2;
  }

  // Test injection (golden RED): corrupt the first vertex position in the REAL output.
  if (meshInjectBug() && c.vertexCount > 0) verts[0].Position = {-999.0f, -999.0f, -999.0f};
}

NodeSpec cubeSpec() {
  NodeSpec s;
  s.type = "CubeMesh";
  s.title = "Cube Mesh";
  PortSpec sgx; sgx.id = "Segments.x"; sgx.name = "Segments"; sgx.dataType = "Float"; sgx.isInput = true;
  sgx.def = 1.0f; sgx.minV = 1.0f; sgx.maxV = 64.0f; sgx.widget = Widget::Vec; sgx.vecArity = 3;
  PortSpec sgy; sgy.id = "Segments.y"; sgy.name = "Segments.y"; sgy.dataType = "Float"; sgy.isInput = true;
  sgy.def = 1.0f; sgy.minV = 1.0f; sgy.maxV = 64.0f;
  PortSpec sgz; sgz.id = "Segments.z"; sgz.name = "Segments.z"; sgz.dataType = "Float"; sgz.isInput = true;
  sgz.def = 1.0f; sgz.minV = 1.0f; sgz.maxV = 64.0f;
  PortSpec stx; stx.id = "Stretch.x"; stx.name = "Stretch"; stx.dataType = "Float"; stx.isInput = true;
  stx.def = 1.0f; stx.minV = 0.0f; stx.maxV = 10.0f; stx.widget = Widget::Vec; stx.vecArity = 3;
  PortSpec sty; sty.id = "Stretch.y"; sty.name = "Stretch.y"; sty.dataType = "Float"; sty.isInput = true;
  sty.def = 1.0f; sty.minV = 0.0f; sty.maxV = 10.0f;
  PortSpec stz; stz.id = "Stretch.z"; stz.name = "Stretch.z"; stz.dataType = "Float"; stz.isInput = true;
  stz.def = 1.0f; stz.minV = 0.0f; stz.maxV = 10.0f;
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
  PortSpec mgn; mgn.id = "Margin"; mgn.name = "Margin"; mgn.dataType = "Float"; mgn.isInput = true;
  mgn.def = 0.0f; mgn.minV = 0.0f; mgn.maxV = 0.5f;
  // TexCoord / TexCoord2 / Margin2: real TiXL [Input]s (CubeMesh.cs:481-491).
  // UV modes (0=Standard/Default, 1=UnwrappedCube, 2=CubeMap, 3=CubeMapSquare) — default=0 is the
  // Standard mapper already implemented. Non-zero modes are DEFERRED (fork); NodeSpec exposes the
  // knob so the inspector shows it; cook ignores non-zero and falls back to Standard (neutral at def).
  PortSpec tco; tco.id = "TexCoord"; tco.name = "TexCoord"; tco.dataType = "Float"; tco.isInput = true;
  tco.def = 0.0f; tco.minV = 0.0f; tco.maxV = 3.0f;
  tco.widget = Widget::Enum; tco.labels = {"Standard", "Unwrapped", "CubeMap", "CubeMapSquare"};
  PortSpec tc2; tc2.id = "TexCoord2"; tc2.name = "TexCoord2"; tc2.dataType = "Float"; tc2.isInput = true;
  tc2.def = 0.0f; tc2.minV = 0.0f; tc2.maxV = 3.0f;
  tc2.widget = Widget::Enum; tc2.labels = {"Standard", "Unwrapped", "CubeMap", "CubeMapSquare"};
  PortSpec mg2; mg2.id = "Margin2"; mg2.name = "Margin2"; mg2.dataType = "Float"; mg2.isInput = true;
  mg2.def = 0.0f; mg2.minV = 0.0f; mg2.maxV = 0.5f;
  PortSpec out; out.id = "Data"; out.name = "Data"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {sgx, sgy, sgz, stx, sty, stz, scl, pvx, pvy, pvz, ctx, cty, ctz, rtx, rty, rtz, mgn, tco, tc2, mg2, out};
  return s;
}

const MeshOp g_cubeMeshOp(cubeSpec(), cubeCount, cubeCook);

}  // namespace
}  // namespace sw
