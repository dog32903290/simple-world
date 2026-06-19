// NGonMesh mesh op (triangle-fan polygon generator) — the 4th cook flow's first proving leaf.
// TiXL authority: external/tixl/Operators/Lib/mesh/generate/NGonMesh.cs (+ defaults). Like a field
// leaf, this one .cpp owns BOTH halves of the op: the NodeSpec (Add menu / findSpec) and the
// MeshCountFn+MeshCookFn (the cook), registered via the MeshOp self-registration seam. The base
// machinery (MeshCookCtx, ensureMesh, cookMeshNode driver branch) is FROZEN — a new mesh op = this
// one .cpp + one CMakeLists line.
//
// VERBATIM math (NGonMesh.cs:84-122):
//   vertex[0]   = center (the fan hub).
//   vertex[1+i] : phi = 2*PI*i/segments; p = (R*sin(phi)*stretchX, R*cos(phi)*stretchY, 0).  [:87-89]
//   index[i]    = Int3(0, (i+2)>segments ? 1 : i+2, i+1).                                      [:120-122]
//   counts      : verticesCount = segments+1, faceCount = segments.                            [:45,51]
// Vertex fields set by TiXL (NGonMesh.cs:71-80, 110-119): Position, Normal=ForwardLH, Tangent=Right,
// Bitangent=Up (all rotation-transformed), Texcoord (TextureMode), Selection=1, ColorRgb=(1,1,1).
// Texcoord2 is NOT set by NGonMesh -> stays default 0 (named fork: TiXL's PbrVertex default = 0).
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

// TextureModes enum (NGonMesh.cs:13-18): Planar=0, Circular=1, CircularScaled=2.
enum TextureModes { Planar = 0, Circular = 1, CircularScaled = 2 };

// Rotate a vector by yaw(Y)/pitch(X)/roll(Z) — TiXL's Matrix4x4.CreateFromYawPitchRoll +
// Vector3.TransformNormal. System.Numerics builds R = Rz(roll) * Rx(pitch) * Ry(yaw) and
// TransformNormal applies it row-vector style (v * M). Default rotation (0,0,0) -> identity -> v.
struct V3 { float x, y, z; };
V3 transformNormal(V3 v, float yaw, float pitch, float roll) {
  float cy = std::cos(yaw), sy = std::sin(yaw);
  float cx = std::cos(pitch), sx = std::sin(pitch);
  float cz = std::cos(roll), sz = std::sin(roll);
  // System.Numerics Matrix4x4.CreateFromYawPitchRoll = CreateRotationY(yaw) * CreateRotationX(pitch)
  // * CreateRotationZ(roll). Expanded (row-major, row-vector v*M). This composite is only exercised
  // for non-default rotation; the proving golden uses (0,0,0) so it reduces to identity.
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

int clampSegments(float v) {
  int s = (int)(v + 0.5f);
  if (s < 1) s = 1;
  if (s > 10000) s = 10000;
  return s;
}

// NGonMesh.cs:44-52 — verticesCount = segments+1, faceCount = segments.
void ngonCount(const std::map<std::string, float>* params, uint32_t& vtx, uint32_t& idx) {
  int segments = clampSegments(cookMeshParam(params, "Segments", 4.0f));
  vtx = (uint32_t)(segments + 1);
  idx = (uint32_t)segments;
}

void ngonCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  int segments = clampSegments(cookMeshParam(c.params, "Segments", 4.0f));
  float radius = cookMeshParam(c.params, "Radius", 1.0f);
  float stretchDef[2] = {1.0f, 1.0f};
  float stretch[2];
  cookMeshVecN(c.params, "Stretch", stretchDef, 2, stretch);
  float rotDef[3] = {0.0f, 0.0f, 0.0f};
  float rot[3];  // (X=pitch, Y=yaw, Z=roll) in DEGREES (NGonMesh.cs:33-35)
  cookMeshVecN(c.params, "Rotation", rotDef, 3, rot);
  float centerDef[3] = {0.0f, 0.0f, 0.0f};
  float center[3];
  cookMeshVecN(c.params, "Center", centerDef, 3, center);
  int textureMode = (int)(cookMeshParam(c.params, "TextureMode", 0.0f) + 0.5f);

  float yaw = rot[1] * kDeg2Rad, pitch = rot[0] * kDeg2Rad, roll = rot[2] * kDeg2Rad;
  // Normal=ForwardLH(0,0,1), Tangent=Right(1,0,0), Bitangent=Up(0,1,0) — rotation-transformed.
  V3 normal = transformNormal(V3{0, 0, 1}, yaw, pitch, roll);
  V3 tangent = transformNormal(V3{1, 0, 0}, yaw, pitch, roll);
  V3 binormal = transformNormal(V3{0, 1, 0}, yaw, pitch, roll);

  SwVertex* verts = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* idx = (SwTriIndex*)c.output_indices->contents();

  auto setCommon = [&](SwVertex& v) {
    v.Normal = {normal.x, normal.y, normal.z};
    v.Tangent = {tangent.x, tangent.y, tangent.z};
    v.Bitangent = {binormal.x, binormal.y, binormal.z};
    v.Texcoord2 = {0.0f, 0.0f};  // FORK: NGonMesh leaves Texcoord2 at PbrVertex default 0.
    v.Selection = 1.0f;
    v.ColorRgb = {1.0f, 1.0f, 1.0f};  // Vector3.One
  };

  // center vertex (NGonMesh.cs:58-81). Planar -> uv (0.5,0.5); other modes -> (0,0).
  {
    SwVertex& v = verts[0];
    v.Position = {center[0], center[1], center[2]};
    float uCenter = 0, vCenter = 0;
    if (textureMode == Planar) { uCenter = 0.5f; vCenter = 0.5f; }
    v.Texcoord = {uCenter, vCenter};
    setCommon(v);
  }

  // ring vertices + fan indices (NGonMesh.cs:84-123).
  for (int i = 0; i < segments; ++i) {
    float phi = 2.0f * kPi * (float)i / (float)segments;
    V3 p = {radius * std::sin(phi) * stretch[0],  // starts at top
            radius * std::cos(phi) * stretch[1], 0.0f};
    float u0 = 0.0f, v0 = 0.0f;
    switch (textureMode) {
      case Planar:         u0 = std::sin(phi) / 2.0f + 0.5f; v0 = std::cos(phi) / 2.0f + 0.5f; break;
      case Circular:       u0 = phi / (2.0f * kPi);          v0 = 1.0f;                        break;
      case CircularScaled: u0 = phi / (2.0f * kPi);          v0 = radius;                      break;
    }
    V3 pr = transformNormal(p, yaw, pitch, roll);
    SwVertex& v = verts[i + 1];
    v.Position = {pr.x + center[0], pr.y + center[1], pr.z + center[2]};
    v.Texcoord = {u0, v0};
    setCommon(v);
    // index[i] = Int3(0, (i+2)>segments ? 1 : i+2, i+1)  (NGonMesh.cs:120-122).
    idx[i].X = 0;
    idx[i].Y = (i + 2) > segments ? 1 : (i + 2);
    idx[i].Z = i + 1;
  }

  // Test injection (golden RED): corrupt the FIRST ring vertex's position in the REAL output so the
  // golden's exact-position assertion fires on the actual cook path.
  if (meshInjectBug() && c.vertexCount > 1) verts[1].Position = {-999.0f, -999.0f, -999.0f};
}

NodeSpec ngonSpec() {
  NodeSpec s;
  s.type = "NGonMesh";
  s.title = "NGon Mesh";
  // Segments = int (NGonMesh.cs Segments InputSlot<int>), default 4 (the proving golden's exact-trig
  // case). Radius = float, default 1. Stretch = Vec2 (default (1,1)). Center = Vec3 (default 0).
  // Rotation = Vec3 (default 0). TextureMode = enum (Planar/Circular/CircularScaled), default Planar.
  PortSpec seg; seg.id = "Segments"; seg.name = "Segments"; seg.dataType = "Float"; seg.isInput = true;
  seg.def = 4.0f; seg.minV = 1.0f; seg.maxV = 64.0f;
  PortSpec rad; rad.id = "Radius"; rad.name = "Radius"; rad.dataType = "Float"; rad.isInput = true;
  rad.def = 1.0f; rad.minV = 0.0f; rad.maxV = 10.0f;
  PortSpec stx; stx.id = "Stretch.x"; stx.name = "Stretch"; stx.dataType = "Float"; stx.isInput = true;
  stx.def = 1.0f; stx.minV = 0.0f; stx.maxV = 10.0f; stx.widget = Widget::Vec; stx.vecArity = 2;
  PortSpec sty; sty.id = "Stretch.y"; sty.name = "Stretch.y"; sty.dataType = "Float"; sty.isInput = true;
  sty.def = 1.0f; sty.minV = 0.0f; sty.maxV = 10.0f;
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
  PortSpec tm; tm.id = "TextureMode"; tm.name = "TextureMode"; tm.dataType = "Float"; tm.isInput = true;
  tm.def = 0.0f; tm.minV = 0.0f; tm.maxV = 2.0f; tm.widget = Widget::Enum;
  tm.labels = {"Planar", "Circular", "CircularScaled"};
  // Output: a Mesh (TiXL Slot<MeshBuffers>). dataType "Mesh" keeps it off Float/Points/Texture2D ports.
  PortSpec out; out.id = "Data"; out.name = "Data"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {seg, rad, stx, sty, ctx, cty, ctz, rtx, rty, rtz, tm, out};
  return s;
}

const MeshOp g_ngonMeshOp(ngonSpec(), ngonCount, ngonCook);

}  // namespace
}  // namespace sw
