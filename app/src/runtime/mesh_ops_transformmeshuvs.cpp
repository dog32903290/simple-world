// TransformMeshUVs mesh op (a pure mesh→mesh CONSUMER, count = input vertex/face count, topology
// unchanged — it only rewrites a UV channel). TiXL authority:
//   external/tixl/Operators/Lib/mesh/modify/TransformMeshUVs.cs (one Mesh input `InputMesh` +
//   Translate/Rotate/Stretch/Uniformscale/UseVertexSelection/Pivot/TexCoord2) → its .t3 builds the
//   SAME render/_/TransformMatrix node as TransformMesh (Pivot/Translate/Rotate/Uniformscale/Stretch→
//   Scale) and dispatches mesh-TransformUVs.hlsl.
//
// VERBATIM math (mesh-TransformUVs.hlsl:25-34):
//   s   = UseVertexSelection > 0.5 ? Selected : 1
//   pos = float3(TexCoord, 0)              (the UV promoted to a 3-vector, z=0)
//   if (ToTexCoord2)  TexCoord2 = lerp(float3(TexCoord2,0), mul(float4(pos2,1), M).xyz, s).xy
//   else              TexCoord  = lerp(pos,                 mul(float4(pos ,1), M).xyz, s).xy
//   (everything else copied: ResultVerts = SourceVerts first).
//   ★ M = render/_/TransformMatrix (GraphicsMath.CreateTransformationMatrix, scalingCenter=
//   rotationCenter=Pivot, scaling=Stretch*UniformScale, rotation=YawPitchRoll(Rot.Y,Rot.X,Rot.Z),
//   translation=Translate). The shader's `mul(float4(p,1), M)` is the row-vector v·M convention, and
//   TransformMatrix's `.Transpose()` for the cbuffer + HLSL column-major load CANCEL → net is v·M on
//   the ORIGINAL M. So we build M directly (identical to mesh_ops_transformmesh.cpp) and apply v·M.
//
// FORKS (named): Shear/Invert/RotationMode=Quaternion deferred (TransformMatrix defaults: no shear, no
// invert, PitchYawRoll), same as TransformMesh. TransformMeshUVs has no Shear/Invert/RotationMode ports
// in its .cs anyway → those defaults are the only behavior. ★Default Pivot=0.5 (NOT 0 like TransformMesh
// — TransformMeshUVs.t3 DefaultValue Pivot=(0.5,0.5,0.5), the UV-space center). Stretch default=(1,1,1).
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <cmath>
#include <map>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"      // EvaluationContext
#include "runtime/field_camera.h"      // Mat4 / mat4Identity / mat4Mul / mat4TransformPointDivW (v·M)
#include "runtime/mesh_op_registry.h"  // MeshOp self-registration + MeshCookCtx + cookMeshParam/VecN
#include "runtime/sw_mesh.h"           // SwVertex (80B) + SwTriIndex (12B)

namespace sw {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;

// (Identical matrix builders to mesh_ops_transformmesh.cpp — row-vector v·M convention.)
Mat4 uvTranslate(float tx, float ty, float tz) {
  Mat4 m = mat4Identity();
  m.m[12] = tx; m.m[13] = ty; m.m[14] = tz;
  return m;
}
Mat4 uvScaleMat(float sx, float sy, float sz) {
  Mat4 m = mat4Identity();
  m.m[0] = sx; m.m[5] = sy; m.m[10] = sz;
  return m;
}
Mat4 uvRotateYawPitchRoll(float yaw, float pitch, float roll) {
  float cy = std::cos(yaw), sy = std::sin(yaw);
  float cx = std::cos(pitch), sx = std::sin(pitch);
  float cz = std::cos(roll), sz = std::sin(roll);
  Mat4 m = mat4Identity();
  m.m[0]  = cy * cz + sy * sx * sz;   m.m[1]  = cx * sz;   m.m[2]  = -sy * cz + cy * sx * sz;
  m.m[4]  = -cy * sz + sy * sx * cz;  m.m[5]  = cx * cz;   m.m[6]  = sy * sz + cy * sx * cz;
  m.m[8]  = sy * cx;                  m.m[9]  = -sx;       m.m[10] = cy * cx;
  return m;
}
Mat4 uvBuildTransformMatrix(const float pivot[3], const float scaling[3], float yaw, float pitch,
                            float roll, const float t[3]) {
  Mat4 m = uvTranslate(-pivot[0], -pivot[1], -pivot[2]);  // scalingCenterT
  m = mat4Mul(m, mat4Identity());                          // invScalingRot (Identity)
  m = mat4Mul(m, uvScaleMat(scaling[0], scaling[1], scaling[2]));
  m = mat4Mul(m, mat4Identity());                          // scalingRot (Identity)
  m = mat4Mul(m, uvTranslate(pivot[0], pivot[1], pivot[2]));      // invScalingCenterT
  m = mat4Mul(m, uvTranslate(-pivot[0], -pivot[1], -pivot[2]));   // rotationCenterT
  m = mat4Mul(m, uvRotateYawPitchRoll(yaw, pitch, roll));
  m = mat4Mul(m, uvTranslate(pivot[0], pivot[1], pivot[2]));      // invRotationCenterT
  m = mat4Mul(m, uvTranslate(t[0], t[1], t[2]));                  // finalT
  return m;
}

// count: pure consumer — output topology == input[0]. Unwired → 0/0 (empty mesh, faithful no-op).
void transformMeshUvsCount(const std::map<std::string, float>* /*params*/, const SwMeshView* inputs,
                           int inputCount, uint32_t& vtx, uint32_t& idx) {
  if (inputCount < 1 || !inputs[0].vtx) { vtx = 0; idx = 0; return; }
  vtx = inputs[0].vtxCount;
  idx = inputs[0].faceCount;
}

void transformMeshUvsCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  if (c.inputMeshCount < 1 || !c.inputMeshes[0].vtx || !c.inputMeshes[0].idx) return;  // unwired → empty
  const SwMeshView& in = c.inputMeshes[0];

  float transDef[3] = {0.0f, 0.0f, 0.0f};
  float trans[3];
  cookMeshVecN(c.params, "Translate", transDef, 3, trans);
  float rotDef[3] = {0.0f, 0.0f, 0.0f};
  float rot[3];  // (X=pitch, Y=yaw, Z=roll) DEGREES
  cookMeshVecN(c.params, "Rotate", rotDef, 3, rot);
  float stretchDef[3] = {1.0f, 1.0f, 1.0f};
  float stretch[3];
  cookMeshVecN(c.params, "Stretch", stretchDef, 3, stretch);
  float uniformScale = cookMeshParam(c.params, "Uniformscale", 1.0f);
  float pivotDef[3] = {0.5f, 0.5f, 0.5f};  // ★UV-space center default (TransformMeshUVs.t3)
  float pivot[3];
  cookMeshVecN(c.params, "Pivot", pivotDef, 3, pivot);
  bool useVertexSelection = cookMeshParam(c.params, "UseVertexSelection", 1.0f) > 0.5f;
  bool toTexCoord2 = cookMeshParam(c.params, "TexCoord2", 0.0f) > 0.5f;

  float scaling[3] = {stretch[0] * uniformScale, stretch[1] * uniformScale, stretch[2] * uniformScale};
  float yaw = rot[1] * kDeg2Rad, pitch = rot[0] * kDeg2Rad, roll = rot[2] * kDeg2Rad;
  Mat4 M = uvBuildTransformMatrix(pivot, scaling, yaw, pitch, roll, trans);

  const SwVertex* src = (const SwVertex*)const_cast<MTL::Buffer*>(in.vtx)->contents();
  const SwTriIndex* srcIdx = (const SwTriIndex*)const_cast<MTL::Buffer*>(in.idx)->contents();
  SwVertex* dst = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* dstIdx = (SwTriIndex*)c.output_indices->contents();

  uint32_t nv = c.vertexCount < in.vtxCount ? c.vertexCount : in.vtxCount;
  for (uint32_t i = 0; i < nv; ++i) {
    SwVertex v = src[i];  // copy all fields (ResultVerts = SourceVerts), then overwrite the UV channel
    float s = useVertexSelection ? v.Selection : 1.0f;
    if (toTexCoord2) {
      float p[3] = {v.Texcoord2.x, v.Texcoord2.y, 0.0f};
      float tp[3];
      mat4TransformPointDivW(M, p[0], p[1], p[2], tp);  // mul(float4(pos2,1), M).xyz
      v.Texcoord2 = {p[0] + (tp[0] - p[0]) * s, p[1] + (tp[1] - p[1]) * s};  // lerp(...).xy
    } else {
      float p[3] = {v.Texcoord.x, v.Texcoord.y, 0.0f};
      float tp[3];
      mat4TransformPointDivW(M, p[0], p[1], p[2], tp);  // mul(float4(pos,1), M).xyz
      v.Texcoord = {p[0] + (tp[0] - p[0]) * s, p[1] + (tp[1] - p[1]) * s};  // lerp(...).xy
    }
    dst[i] = v;
  }

  // Indices: topology unchanged → copy verbatim.
  uint32_t nf = c.indexCount < in.faceCount ? c.indexCount : in.faceCount;
  for (uint32_t f = 0; f < nf; ++f) dstIdx[f] = srcIdx[f];

  // Test injection (golden RED): corrupt the FIRST output vertex in the REAL cook. We corrupt BOTH the
  // op's primary output (TexCoord — the flat golden's load-bearing assertion) AND its Position (so the
  // production resident pixel golden bites too: DrawMeshUnlit is unlit and ignores UVs, so only a
  // position move shifts the on-screen quad). Teeth fire on the actual cook path.
  if (meshInjectBug() && c.vertexCount > 0) {
    dst[0].Texcoord = {-999.0f, -999.0f};
    dst[0].Position = {-999.0f, -999.0f, -999.0f};
  }
}

NodeSpec transformMeshUvsSpec() {
  NodeSpec s;
  s.type = "TransformMeshUVs";
  s.title = "Transform Mesh UVs";
  // TransformMeshUVs.cs ports: Mesh input (b9e7efdf) + Translate/Rotate/Stretch Vec3, Uniformscale
  // float, UseVertexSelection bool, Pivot Vec3 (default 0.5), TexCoord2 bool. (NodeSpec append order.)
  PortSpec meshIn; meshIn.id = "Mesh"; meshIn.name = "InputMesh"; meshIn.dataType = "Mesh"; meshIn.isInput = true;
  PortSpec tx; tx.id = "Translate.x"; tx.name = "Translate"; tx.dataType = "Float"; tx.isInput = true;
  tx.def = 0.0f; tx.minV = -10.0f; tx.maxV = 10.0f; tx.widget = Widget::Vec; tx.vecArity = 3;
  PortSpec ty; ty.id = "Translate.y"; ty.name = "Translate.y"; ty.dataType = "Float"; ty.isInput = true;
  ty.def = 0.0f; ty.minV = -10.0f; ty.maxV = 10.0f;
  PortSpec tz; tz.id = "Translate.z"; tz.name = "Translate.z"; tz.dataType = "Float"; tz.isInput = true;
  tz.def = 0.0f; tz.minV = -10.0f; tz.maxV = 10.0f;
  PortSpec rx; rx.id = "Rotate.x"; rx.name = "Rotate"; rx.dataType = "Float"; rx.isInput = true;
  rx.def = 0.0f; rx.minV = -360.0f; rx.maxV = 360.0f; rx.widget = Widget::Vec; rx.vecArity = 3;
  PortSpec ry; ry.id = "Rotate.y"; ry.name = "Rotate.y"; ry.dataType = "Float"; ry.isInput = true;
  ry.def = 0.0f; ry.minV = -360.0f; ry.maxV = 360.0f;
  PortSpec rz; rz.id = "Rotate.z"; rz.name = "Rotate.z"; rz.dataType = "Float"; rz.isInput = true;
  rz.def = 0.0f; rz.minV = -360.0f; rz.maxV = 360.0f;
  PortSpec sx; sx.id = "Stretch.x"; sx.name = "Stretch"; sx.dataType = "Float"; sx.isInput = true;
  sx.def = 1.0f; sx.minV = -10.0f; sx.maxV = 10.0f; sx.widget = Widget::Vec; sx.vecArity = 3;
  PortSpec sy; sy.id = "Stretch.y"; sy.name = "Stretch.y"; sy.dataType = "Float"; sy.isInput = true;
  sy.def = 1.0f; sy.minV = -10.0f; sy.maxV = 10.0f;
  PortSpec sz; sz.id = "Stretch.z"; sz.name = "Stretch.z"; sz.dataType = "Float"; sz.isInput = true;
  sz.def = 1.0f; sz.minV = -10.0f; sz.maxV = 10.0f;
  PortSpec us; us.id = "Uniformscale"; us.name = "Uniformscale"; us.dataType = "Float"; us.isInput = true;
  us.def = 1.0f; us.minV = -10.0f; us.maxV = 10.0f;
  PortSpec uvs; uvs.id = "UseVertexSelection"; uvs.name = "UseVertexSelection"; uvs.dataType = "Float";
  uvs.isInput = true; uvs.def = 1.0f; uvs.minV = 0.0f; uvs.maxV = 1.0f; uvs.widget = Widget::Bool;
  PortSpec px; px.id = "Pivot.x"; px.name = "Pivot"; px.dataType = "Float"; px.isInput = true;
  px.def = 0.5f; px.minV = -10.0f; px.maxV = 10.0f; px.widget = Widget::Vec; px.vecArity = 3;
  PortSpec py; py.id = "Pivot.y"; py.name = "Pivot.y"; py.dataType = "Float"; py.isInput = true;
  py.def = 0.5f; py.minV = -10.0f; py.maxV = 10.0f;
  PortSpec pz; pz.id = "Pivot.z"; pz.name = "Pivot.z"; pz.dataType = "Float"; pz.isInput = true;
  pz.def = 0.5f; pz.minV = -10.0f; pz.maxV = 10.0f;
  PortSpec tc2; tc2.id = "TexCoord2"; tc2.name = "TexCoord2"; tc2.dataType = "Float"; tc2.isInput = true;
  tc2.def = 0.0f; tc2.minV = 0.0f; tc2.maxV = 1.0f; tc2.widget = Widget::Bool;
  PortSpec out; out.id = "Result"; out.name = "Result"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {meshIn, tx, ty, tz, rx, ry, rz, sx, sy, sz, us, uvs, px, py, pz, tc2, out};
  return s;
}

const MeshOp g_transformMeshUvsOp(transformMeshUvsSpec(), transformMeshUvsCount, transformMeshUvsCook);

}  // namespace
}  // namespace sw
