// MeshProjectUV mesh op (a mesh→mesh CONSUMER: PLANAR-projects new UVs from each vertex's POSITION
// through a gizmo transform). TiXL authority: external/tixl/Operators/Lib/mesh/modify/MeshProjectUV.cs
// (ITransformable gizmo Translate/Rotate/Stretch/Scale + ToTexCoord2 bool, one Mesh in/out) + compute
// shader mesh-ProjectUV.hlsl.
//
// VERBATIM math (mesh-ProjectUV.hlsl):
//   uv = mul(float4(pos,1), Transform).xy + float2(1,1)
//   ToTexCoord2 ? v.TexCoord2 = uv : v.TexCoord = uv ;   all other fields copied verbatim.
// `Transform` (cbuffer b1) is the gizmo's TransformMatrix.Result (forward TRS, fed un-inverted in this op
// — the .t3 wires `Result`, NOT `ResultInverted`). The .Transpose() applied for the HLSL cbuffer cancels
// with HLSL's column-major load of `mul(rowvec, M)`, so the net per-vertex op is the row-vector `pos · M`
// on the ORIGINAL forward M — exactly the field_camera mat4TransformPointDivW convention, so we build M
// directly and apply it (identical reasoning to mesh_ops_transformmesh.cpp's matrix note).
//
// FORKS (named): RotationMode=PitchYawRoll, Shear=0, Invert=false, Pivot=0 (TransformMatrix defaults).
// inputTexture/sampler (declared in the shader t1/s0) are UNUSED by this op's math (planar projection only)
// → not wired. Translate/Rotate/Stretch/Scale ride the standard gizmo TRS.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <cmath>
#include <map>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/field_camera.h"      // Mat4 / mat4Identity / mat4Mul / mat4TransformPointDivW
#include "runtime/mesh_op_registry.h"  // MeshOp self-registration + MeshCookCtx + SwMeshView
#include "runtime/sw_mesh.h"           // SwVertex (80B)

namespace sw {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;

Mat4 translate(float tx, float ty, float tz) { Mat4 m=mat4Identity(); m.m[12]=tx; m.m[13]=ty; m.m[14]=tz; return m; }
Mat4 scaleMat(float sx, float sy, float sz) { Mat4 m=mat4Identity(); m.m[0]=sx; m.m[5]=sy; m.m[10]=sz; return m; }
Mat4 rotateYawPitchRoll(float yaw, float pitch, float roll) {
  float cy=std::cos(yaw), sy=std::sin(yaw), cx=std::cos(pitch), sx=std::sin(pitch), cz=std::cos(roll), sz=std::sin(roll);
  Mat4 m = mat4Identity();
  m.m[0]=cy*cz+sy*sx*sz;  m.m[1]=cx*sz;  m.m[2]=-sy*cz+cy*sx*sz;
  m.m[4]=-cy*sz+sy*sx*cz; m.m[5]=cx*cz;  m.m[6]=sy*sz+cy*sx*cz;
  m.m[8]=sy*cx;           m.m[9]=-sx;    m.m[10]=cy*cx;
  return m;
}
// GraphicsMath.CreateTransformationMatrix with pivot 0 (no pivot input): S·R·T.
Mat4 buildTransformMatrix(const float scaling[3], float yaw, float pitch, float roll, const float t[3]) {
  Mat4 m = scaleMat(scaling[0], scaling[1], scaling[2]);
  m = mat4Mul(m, rotateYawPitchRoll(yaw, pitch, roll));
  m = mat4Mul(m, translate(t[0], t[1], t[2]));
  return m;
}

void meshProjectUvCount(const std::map<std::string, float>* /*params*/, const SwMeshView* inputs,
                        int inputCount, uint32_t& vtx, uint32_t& idx) {
  if (inputCount < 1 || !inputs[0].vtx) { vtx = 0; idx = 0; return; }
  vtx = inputs[0].vtxCount; idx = inputs[0].faceCount;
}

void meshProjectUvCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  if (c.inputMeshCount < 1 || !c.inputMeshes[0].vtx || !c.inputMeshes[0].idx) return;  // unwired → empty
  const SwMeshView& in = c.inputMeshes[0];

  float transDef[3] = {0,0,0}, trans[3];    cookMeshVecN(c.params, "Translate", transDef, 3, trans);
  float rotDef[3] = {0,0,0}, rot[3];        cookMeshVecN(c.params, "Rotate", rotDef, 3, rot);
  float stretchDef[3] = {1,1,1}, stretch[3];cookMeshVecN(c.params, "Stretch", stretchDef, 3, stretch);
  float scale = cookMeshParam(c.params, "Scale", 1.0f);
  bool toTexCoord2 = cookMeshParam(c.params, "ToTexCoord2", 0.0f) > 0.5f;

  float scaling[3] = {stretch[0]*scale, stretch[1]*scale, stretch[2]*scale};
  Mat4 M = buildTransformMatrix(scaling, rot[1]*kDeg2Rad, rot[0]*kDeg2Rad, rot[2]*kDeg2Rad, trans);

  const SwVertex* src = (const SwVertex*)const_cast<MTL::Buffer*>(in.vtx)->contents();
  const SwTriIndex* srcIdx = (const SwTriIndex*)const_cast<MTL::Buffer*>(in.idx)->contents();
  SwVertex* dst = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* dstIdx = (SwTriIndex*)c.output_indices->contents();

  uint32_t nv = c.vertexCount < in.vtxCount ? c.vertexCount : in.vtxCount;
  for (uint32_t i = 0; i < nv; ++i) {
    SwVertex v = src[i];  // carry all attributes; one UV channel overwritten
    float p[3]; mat4TransformPointDivW(M, v.Position.x, v.Position.y, v.Position.z, p);
    SW_MESH_FLOAT2 uv = {p[0] + 1.0f, p[1] + 1.0f};
    if (toTexCoord2) v.Texcoord2 = uv; else v.Texcoord = uv;
    dst[i] = v;
  }

  uint32_t nf = c.indexCount < in.faceCount ? c.indexCount : in.faceCount;
  for (uint32_t f = 0; f < nf; ++f) dstIdx[f] = srcIdx[f];  // topology unchanged

  // Test injection (golden RED): corrupt the FIRST output vertex's TexCoord (the op's primary output)
  // AND Position (so a production unlit pixel golden could bite). Bites the REAL cook path.
  if (meshInjectBug() && c.vertexCount > 0) {
    dst[0].Texcoord = {-999.0f, -999.0f};
    dst[0].Position = {-999.0f, -999.0f, -999.0f};
  }
}

NodeSpec meshProjectUvSpec() {
  NodeSpec s;
  s.type = "MeshProjectUV";
  s.title = "Mesh Project UV";
  PortSpec meshIn; meshIn.id="Mesh"; meshIn.name="Mesh"; meshIn.dataType="Mesh"; meshIn.isInput=true;
  PortSpec tx; tx.id="Translate.x"; tx.name="Translate"; tx.dataType="Float"; tx.isInput=true;
  tx.def=0.0f; tx.minV=-10.0f; tx.maxV=10.0f; tx.widget=Widget::Vec; tx.vecArity=3;
  PortSpec ty; ty.id="Translate.y"; ty.name="Translate.y"; ty.dataType="Float"; ty.isInput=true; ty.def=0.0f; ty.minV=-10.0f; ty.maxV=10.0f;
  PortSpec tz; tz.id="Translate.z"; tz.name="Translate.z"; tz.dataType="Float"; tz.isInput=true; tz.def=0.0f; tz.minV=-10.0f; tz.maxV=10.0f;
  PortSpec rx; rx.id="Rotate.x"; rx.name="Rotate"; rx.dataType="Float"; rx.isInput=true;
  rx.def=0.0f; rx.minV=-360.0f; rx.maxV=360.0f; rx.widget=Widget::Vec; rx.vecArity=3;
  PortSpec ry; ry.id="Rotate.y"; ry.name="Rotate.y"; ry.dataType="Float"; ry.isInput=true; ry.def=0.0f; ry.minV=-360.0f; ry.maxV=360.0f;
  PortSpec rz; rz.id="Rotate.z"; rz.name="Rotate.z"; rz.dataType="Float"; rz.isInput=true; rz.def=0.0f; rz.minV=-360.0f; rz.maxV=360.0f;
  PortSpec stx; stx.id="Stretch.x"; stx.name="Stretch"; stx.dataType="Float"; stx.isInput=true;
  stx.def=1.0f; stx.minV=-10.0f; stx.maxV=10.0f; stx.widget=Widget::Vec; stx.vecArity=3;
  PortSpec sty; sty.id="Stretch.y"; sty.name="Stretch.y"; sty.dataType="Float"; sty.isInput=true; sty.def=1.0f; sty.minV=-10.0f; sty.maxV=10.0f;
  PortSpec stz; stz.id="Stretch.z"; stz.name="Stretch.z"; stz.dataType="Float"; stz.isInput=true; stz.def=1.0f; stz.minV=-10.0f; stz.maxV=10.0f;
  PortSpec sc; sc.id="Scale"; sc.name="Scale"; sc.dataType="Float"; sc.isInput=true; sc.def=1.0f; sc.minV=-10.0f; sc.maxV=10.0f;
  PortSpec t2; t2.id="ToTexCoord2"; t2.name="ToTexCoord2"; t2.dataType="Float"; t2.isInput=true; t2.def=0.0f; t2.minV=0.0f; t2.maxV=1.0f; t2.widget=Widget::Bool;
  PortSpec out; out.id="Result"; out.name="Result"; out.dataType="Mesh"; out.isInput=false;
  s.ports = {meshIn, tx, ty, tz, rx, ry, rz, stx, sty, stz, sc, t2, out};
  return s;
}

const MeshOp g_meshProjectUvOp(meshProjectUvSpec(), meshProjectUvCount, meshProjectUvCook);

}  // namespace
}  // namespace sw
