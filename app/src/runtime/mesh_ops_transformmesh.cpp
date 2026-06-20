// TransformMesh mesh op (the FIRST mesh CONSUMER = the mesh-input seam's first proving leaf). Mesh in →
// Mesh out: applies a TRS-with-pivot matrix to every vertex (position + the three direction frames).
// TiXL authority: external/tixl/Operators/Lib/mesh/modify/TransformMesh.cs (a TransformCallbackSlot
// ITransformable whose math lives in the .t3 dispatch of mesh-TransformVertices.hlsl, the matrix built
// by render/_/TransformMatrix.cs → GraphicsMath.CreateTransformationMatrix).
//
// VERBATIM math:
//   matrix (TransformMatrix.cs Update + GraphicsMath.cs:56-97, scalingRotation=Identity,
//           scalingCenter=rotationCenter=Pivot, scaling=Scale*UniformScale, rotation=
//           Quaternion.CreateFromYawPitchRoll(yaw=Rot.Y, pitch=Rot.X, roll=Rot.Z), translation=Translation):
//     M = T(-pivot)·invScalingRot·S·scalingRot·T(pivot)·T(-pivot)·R·T(pivot)·T(translation)
//   (Shear=0 → the post-multiply shear matrix is Identity → omitted. Invert=false. The .Transpose()
//    for the HLSL cbuffer + HLSL's default column-major load CANCEL, so the net per-vertex transform
//    is the System.Numerics row-vector `Vector4.Transform((pos,1), M)` on the ORIGINAL M — which is
//    exactly the field_camera Mat4 / mat4Mul convention `v·M`, so we build M directly and apply it.)
//   per vertex (mesh-TransformVertices.hlsl):
//     s = UseVertexSelection>0.5 ? Selected : 1   (NGon/Quad set Selection=1 → s=1 → full transform)
//     Position  = lerp(pos,  (float4(pos,1)·M).xyz, s)
//     Normal    = lerp(n,    normalize((float4(n,0)·M).xyz), s)   (same for Tangent, Bitangent)
//     TexCoord/TexCoord2/Selected/ColorRGB copied verbatim.
//
// FORKS (named): Shear/Invert/RotationMode=Quaternion deferred (defaults: no shear, no invert,
// PitchYawRoll); the .Transpose() round-trip folded into the row-vector `v·M` (math-identical, no GPU
// cbuffer here). UseVertexSelection default true is honored (reads SwVertex.Selection).
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <cmath>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"      // EvaluationContext
#include "runtime/field_camera.h"      // Mat4 / mat4Identity / mat4Mul (row-vector v·M, == System.Numerics)
#include "runtime/mesh_op_registry.h"  // MeshOp self-registration + MeshCookCtx + SwMeshView + cookMeshParam/VecN
#include "runtime/sw_mesh.h"           // SwVertex (80B) + SwTriIndex (12B)

namespace sw {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;

// Row-vector translation matrix T(tx,ty,tz): translation lives in row 3 (m[12..14]) so v·T adds t.
Mat4 translate(float tx, float ty, float tz) {
  Mat4 m = mat4Identity();
  m.m[12] = tx; m.m[13] = ty; m.m[14] = tz;
  return m;
}
// Row-vector scale matrix.
Mat4 scaleMat(float sx, float sy, float sz) {
  Mat4 m = mat4Identity();
  m.m[0] = sx; m.m[5] = sy; m.m[10] = sz;
  return m;
}
// Row-vector rotation matrix = Matrix4x4.CreateFromQuaternion(CreateFromYawPitchRoll(yaw,pitch,roll)).
// Identical 3×3 to NGonMesh/QuadMesh transformNormal (System.Numerics CreateFromYawPitchRoll expansion,
// R = Ry(yaw)·Rx(pitch)·Rz(roll), row-vector v·M). Embedded in a 4×4 with no translation.
Mat4 rotateYawPitchRoll(float yaw, float pitch, float roll) {
  float cy = std::cos(yaw), sy = std::sin(yaw);
  float cx = std::cos(pitch), sx = std::sin(pitch);
  float cz = std::cos(roll), sz = std::sin(roll);
  Mat4 m = mat4Identity();
  m.m[0]  = cy * cz + sy * sx * sz;   m.m[1]  = cx * sz;   m.m[2]  = -sy * cz + cy * sx * sz;
  m.m[4]  = -cy * sz + sy * sx * cz;  m.m[5]  = cx * cz;   m.m[6]  = sy * sz + cy * sx * cz;
  m.m[8]  = sy * cx;                  m.m[9]  = -sx;       m.m[10] = cy * cx;
  return m;
}

// Transform a direction (w=0, no translation) by the row-vector convention v·M → out[3]. (Position uses
// the field_camera mat4TransformPointDivW; directions need the w=0 variant the shader does, then normalize.)
void transformDir(const Mat4& m, float x, float y, float z, float out[3]) {
  out[0] = x * m.m[0] + y * m.m[4] + z * m.m[8];
  out[1] = x * m.m[1] + y * m.m[5] + z * m.m[9];
  out[2] = x * m.m[2] + y * m.m[6] + z * m.m[10];
}
void normalize3(float v[3]) {
  float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (len > 1e-12f) { v[0] /= len; v[1] /= len; v[2] /= len; }
}

// Build M = GraphicsMath.CreateTransformationMatrix(scalingCenter=pivot, scalingRotation=Identity,
// scaling, rotationCenter=pivot, rotation, translation), verbatim composite order (GraphicsMath.cs:85-94).
Mat4 buildTransformMatrix(const float pivot[3], const float scaling[3], float yaw, float pitch,
                          float roll, const float t[3]) {
  Mat4 scalingCenterT = translate(-pivot[0], -pivot[1], -pivot[2]);
  Mat4 invScalingRot = mat4Identity();   // Conjugate(Identity) = Identity
  Mat4 scalingM = scaleMat(scaling[0], scaling[1], scaling[2]);
  Mat4 scalingRot = mat4Identity();      // CreateFromQuaternion(Identity) = Identity
  Mat4 invScalingCenterT = translate(pivot[0], pivot[1], pivot[2]);
  Mat4 rotationCenterT = translate(-pivot[0], -pivot[1], -pivot[2]);
  Mat4 rotationM = rotateYawPitchRoll(yaw, pitch, roll);
  Mat4 invRotationCenterT = translate(pivot[0], pivot[1], pivot[2]);
  Mat4 finalT = translate(t[0], t[1], t[2]);
  // M = scalingCenterT · invScalingRot · scalingM · scalingRot · invScalingCenterT ·
  //     rotationCenterT · rotationM · invRotationCenterT · finalT  (left-to-right v·M, mat4Mul order).
  Mat4 m = scalingCenterT;
  m = mat4Mul(m, invScalingRot);
  m = mat4Mul(m, scalingM);
  m = mat4Mul(m, scalingRot);
  m = mat4Mul(m, invScalingCenterT);
  m = mat4Mul(m, rotationCenterT);
  m = mat4Mul(m, rotationM);
  m = mat4Mul(m, invRotationCenterT);
  m = mat4Mul(m, finalT);
  return m;
}

// count: a CONSUMER op — its output is the SAME topology as input[0] (vertex-for-vertex transform,
// no add/remove). TransformMesh.cs has ONE Mesh input. Unwired → 0/0 (empty mesh, faithful no-op).
void transformMeshCount(const std::map<std::string, float>* /*params*/, const SwMeshView* inputs,
                        int inputCount, uint32_t& vtx, uint32_t& idx) {
  if (inputCount < 1 || !inputs[0].vtx) { vtx = 0; idx = 0; return; }
  vtx = inputs[0].vtxCount;
  idx = inputs[0].faceCount;
}

void transformMeshCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  if (c.inputMeshCount < 1 || !c.inputMeshes[0].vtx || !c.inputMeshes[0].idx) return;  // unwired → empty
  const SwMeshView& in = c.inputMeshes[0];

  float transDef[3] = {0.0f, 0.0f, 0.0f};
  float trans[3];
  cookMeshVecN(c.params, "Translation", transDef, 3, trans);
  float rotDef[3] = {0.0f, 0.0f, 0.0f};
  float rot[3];  // (X=pitch, Y=yaw, Z=roll) DEGREES (TransformMatrix.cs Rotation_PitchYawRoll)
  cookMeshVecN(c.params, "Rotation", rotDef, 3, rot);
  float scaleDef[3] = {1.0f, 1.0f, 1.0f};
  float scl[3];
  cookMeshVecN(c.params, "Scale", scaleDef, 3, scl);
  float uniformScale = cookMeshParam(c.params, "UniformScale", 1.0f);
  float pivotDef[3] = {0.0f, 0.0f, 0.0f};
  float pivot[3];
  cookMeshVecN(c.params, "Pivot", pivotDef, 3, pivot);
  // UseVertexSelection default true (TransformMesh.t3): s = Selected when on, else 1.
  bool useVertexSelection = cookMeshParam(c.params, "UseVertexSelection", 1.0f) > 0.5f;

  float scaling[3] = {scl[0] * uniformScale, scl[1] * uniformScale, scl[2] * uniformScale};
  float yaw = rot[1] * kDeg2Rad, pitch = rot[0] * kDeg2Rad, roll = rot[2] * kDeg2Rad;
  Mat4 M = buildTransformMatrix(pivot, scaling, yaw, pitch, roll, trans);

  const SwVertex* src = (const SwVertex*)const_cast<MTL::Buffer*>(in.vtx)->contents();
  const SwTriIndex* srcIdx = (const SwTriIndex*)const_cast<MTL::Buffer*>(in.idx)->contents();
  SwVertex* dst = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* dstIdx = (SwTriIndex*)c.output_indices->contents();

  uint32_t nv = c.vertexCount < in.vtxCount ? c.vertexCount : in.vtxCount;
  for (uint32_t i = 0; i < nv; ++i) {
    const SwVertex& sv = src[i];
    SwVertex& dv = dst[i];
    float s = useVertexSelection ? sv.Selection : 1.0f;

    // Position: lerp(pos, (float4(pos,1)·M).xyz, s).
    float tp[3];
    mat4TransformPointDivW(M, sv.Position.x, sv.Position.y, sv.Position.z, tp);  // affine → w=1
    dv.Position = {sv.Position.x + (tp[0] - sv.Position.x) * s,
                   sv.Position.y + (tp[1] - sv.Position.y) * s,
                   sv.Position.z + (tp[2] - sv.Position.z) * s};

    // Normal/Tangent/Bitangent: lerp(d, normalize((float4(d,0)·M).xyz), s).
    auto xfDir = [&](const SW_MESH_PACKED3& d) -> SW_MESH_PACKED3 {
      float td[3];
      transformDir(M, d.x, d.y, d.z, td);
      normalize3(td);
      return {d.x + (td[0] - d.x) * s, d.y + (td[1] - d.y) * s, d.z + (td[2] - d.z) * s};
    };
    dv.Normal = xfDir(sv.Normal);
    dv.Tangent = xfDir(sv.Tangent);
    dv.Bitangent = xfDir(sv.Bitangent);

    // Pass-through (mesh-TransformVertices.hlsl copies these verbatim).
    dv.Texcoord = sv.Texcoord;
    dv.Texcoord2 = sv.Texcoord2;
    dv.Selection = sv.Selection;
    dv.ColorRgb = sv.ColorRgb;
  }

  // Indices: topology unchanged → copy verbatim (transform is per-vertex; faces keep their indices).
  uint32_t nf = c.indexCount < in.faceCount ? c.indexCount : in.faceCount;
  for (uint32_t f = 0; f < nf; ++f) dstIdx[f] = srcIdx[f];

  // Test injection (golden RED): corrupt the FIRST output vertex's position in the REAL cook so the
  // golden's exact-position assertion fires on the actual transform path.
  if (meshInjectBug() && c.vertexCount > 0) dst[0].Position = {-999.0f, -999.0f, -999.0f};
}

NodeSpec transformMeshSpec() {
  NodeSpec s;
  s.type = "TransformMesh";
  s.title = "Transform Mesh";
  // Mesh (in). Translation Vec3 (0). Rotation Vec3 (0) DEGREES. Scale Vec3 (1). UniformScale float (1).
  // Pivot Vec3 (0). UseVertexSelection bool→Float (1 = true, TransformMesh.t3 default).
  PortSpec meshIn; meshIn.id = "Mesh"; meshIn.name = "Mesh"; meshIn.dataType = "Mesh"; meshIn.isInput = true;
  PortSpec tx; tx.id = "Translation.x"; tx.name = "Translation"; tx.dataType = "Float"; tx.isInput = true;
  tx.def = 0.0f; tx.minV = -100.0f; tx.maxV = 100.0f; tx.widget = Widget::Vec; tx.vecArity = 3;
  PortSpec ty; ty.id = "Translation.y"; ty.name = "Translation.y"; ty.dataType = "Float"; ty.isInput = true;
  ty.def = 0.0f; ty.minV = -100.0f; ty.maxV = 100.0f;
  PortSpec tz; tz.id = "Translation.z"; tz.name = "Translation.z"; tz.dataType = "Float"; tz.isInput = true;
  tz.def = 0.0f; tz.minV = -100.0f; tz.maxV = 100.0f;
  PortSpec rx; rx.id = "Rotation.x"; rx.name = "Rotation"; rx.dataType = "Float"; rx.isInput = true;
  rx.def = 0.0f; rx.minV = -360.0f; rx.maxV = 360.0f; rx.widget = Widget::Vec; rx.vecArity = 3;
  PortSpec ry; ry.id = "Rotation.y"; ry.name = "Rotation.y"; ry.dataType = "Float"; ry.isInput = true;
  ry.def = 0.0f; ry.minV = -360.0f; ry.maxV = 360.0f;
  PortSpec rz; rz.id = "Rotation.z"; rz.name = "Rotation.z"; rz.dataType = "Float"; rz.isInput = true;
  rz.def = 0.0f; rz.minV = -360.0f; rz.maxV = 360.0f;
  PortSpec sx; sx.id = "Scale.x"; sx.name = "Scale"; sx.dataType = "Float"; sx.isInput = true;
  sx.def = 1.0f; sx.minV = -10.0f; sx.maxV = 10.0f; sx.widget = Widget::Vec; sx.vecArity = 3;
  PortSpec sy; sy.id = "Scale.y"; sy.name = "Scale.y"; sy.dataType = "Float"; sy.isInput = true;
  sy.def = 1.0f; sy.minV = -10.0f; sy.maxV = 10.0f;
  PortSpec sz; sz.id = "Scale.z"; sz.name = "Scale.z"; sz.dataType = "Float"; sz.isInput = true;
  sz.def = 1.0f; sz.minV = -10.0f; sz.maxV = 10.0f;
  PortSpec us; us.id = "UniformScale"; us.name = "UniformScale"; us.dataType = "Float"; us.isInput = true;
  us.def = 1.0f; us.minV = -10.0f; us.maxV = 10.0f;
  PortSpec px; px.id = "Pivot.x"; px.name = "Pivot"; px.dataType = "Float"; px.isInput = true;
  px.def = 0.0f; px.minV = -10.0f; px.maxV = 10.0f; px.widget = Widget::Vec; px.vecArity = 3;
  PortSpec py; py.id = "Pivot.y"; py.name = "Pivot.y"; py.dataType = "Float"; py.isInput = true;
  py.def = 0.0f; py.minV = -10.0f; py.maxV = 10.0f;
  PortSpec pz; pz.id = "Pivot.z"; pz.name = "Pivot.z"; pz.dataType = "Float"; pz.isInput = true;
  pz.def = 0.0f; pz.minV = -10.0f; pz.maxV = 10.0f;
  PortSpec uvs; uvs.id = "UseVertexSelection"; uvs.name = "UseVertexSelection"; uvs.dataType = "Float";
  uvs.isInput = true; uvs.def = 1.0f; uvs.minV = 0.0f; uvs.maxV = 1.0f; uvs.widget = Widget::Bool;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {meshIn, tx, ty, tz, rx, ry, rz, sx, sy, sz, us, px, py, pz, uvs, out};
  return s;
}

const MeshOp g_transformMeshOp(transformMeshSpec(), transformMeshCount, transformMeshCook);

}  // namespace
}  // namespace sw
