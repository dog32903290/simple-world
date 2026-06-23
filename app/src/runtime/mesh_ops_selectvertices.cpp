// SelectVertices mesh op (a mesh→mesh CONSUMER that writes per-vertex Selection from a VOLUME field).
// TiXL authority: external/tixl/Operators/Lib/mesh/modify/SelectVertices.cs (ITransformable: a Mesh
// input + a gizmo TRS = Center/Stretch/Scale/Rotate + VolumeShape/FallOff/Mode/Strength/Phase/Threshold/
// ClampResult) + its compute shader mesh-SelectVertices.hlsl (the verbatim per-vertex math). The gizmo
// TRS is composed by render/_/TransformMatrix.cs → GraphicsMath.CreateTransformationMatrix; the shader's
// `TransformVolume` cbuffer is that matrix's ResultInverted (object→volume), so a vertex inside the unit
// gizmo volume maps to |posInVolume|<1.
//
// VERBATIM math (mesh-SelectVertices.hlsl):
//   posInVolume = mul(float4(pos,1), TransformVolume)        // TransformVolume = inverse(gizmo TRS)
//   Sphere: s = smoothstep(1+FallOff, 1, length(posInVolume))
//   Box:    t=abs(posInVolume); d=max(t.x,t.y,t.z)+Phase; s = smoothstep(1+FallOff, 1, d)
//   Plane:  s = smoothstep(FallOff, 0, posInVolume.y)
//   Zebra:  d = 1-abs(mod(posInVolume.y+Phase,2)-1); s = smoothstep(Th+0.5+FallOff, Th+0.5, d)
//   Noise:  s = smoothstep(Th+FallOff, Th, snoise(posInVolume*0.91+Phase))
//   then by Mode: Override s*=Strength; Add s+=Sel*Strength; Sub s=Sel-s*Strength;
//                 Multiply s=lerp(Sel,Sel*s,Strength); Invert s=s*(1-Sel)
//   Result.Selected = ClampResult<0.5 ? s : saturate(s).  All other fields copied verbatim.
//
// FORKS (named): Noise mode uses TiXL snoise (shared/noise-functions.hlsl) — NOT ported here (no host
// simplex-noise yet); a Noise-mode cook falls through with s=1 (the shader's pre-branch default) so it is
// a NAMED DEFER, not silent wrong math. Sphere/Box/Plane/Zebra are ported 1:1. The gizmo matrix is built
// directly in row-vector v·M form and inverted (the .Transpose() + HLSL column-load cancel, per
// mesh_ops_transformmesh.cpp's matrix note). RotationMode=PitchYawRoll, Shear=0, Invert=false (defaults).
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <cmath>
#include <map>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/field_camera.h"      // Mat4 / mat4Identity / mat4Mul / mat4Inverse / mat4TransformPointDivW
#include "runtime/mesh_op_registry.h"  // MeshOp self-registration + MeshCookCtx + SwMeshView
#include "runtime/sw_mesh.h"           // SwVertex (80B)

namespace sw {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;

Mat4 translate(float tx, float ty, float tz) {
  Mat4 m = mat4Identity(); m.m[12]=tx; m.m[13]=ty; m.m[14]=tz; return m;
}
Mat4 scaleMat(float sx, float sy, float sz) {
  Mat4 m = mat4Identity(); m.m[0]=sx; m.m[5]=sy; m.m[10]=sz; return m;
}
Mat4 rotateYawPitchRoll(float yaw, float pitch, float roll) {
  float cy=std::cos(yaw), sy=std::sin(yaw), cx=std::cos(pitch), sx=std::sin(pitch),
        cz=std::cos(roll), sz=std::sin(roll);
  Mat4 m = mat4Identity();
  m.m[0]=cy*cz+sy*sx*sz;  m.m[1]=cx*sz;  m.m[2]=-sy*cz+cy*sx*sz;
  m.m[4]=-cy*sz+sy*sx*cz; m.m[5]=cx*cz;  m.m[6]=sy*sz+cy*sx*cz;
  m.m[8]=sy*cx;           m.m[9]=-sx;    m.m[10]=cy*cx;
  return m;
}
// GraphicsMath.CreateTransformationMatrix(scalingCenter=pivot, scaling, rotationCenter=pivot, rotation,
// translation) — verbatim composite (identical to mesh_ops_transformmesh.cpp::buildTransformMatrix).
// Pivot is 0 here (SelectVertices' gizmo has no pivot input), kept general for clarity.
Mat4 buildTransformMatrix(const float pivot[3], const float scaling[3], float yaw, float pitch,
                          float roll, const float t[3]) {
  Mat4 m = translate(-pivot[0], -pivot[1], -pivot[2]);          // scalingCenterT
  m = mat4Mul(m, mat4Identity());                               // invScalingRot (Conjugate(I)=I)
  m = mat4Mul(m, scaleMat(scaling[0], scaling[1], scaling[2])); // scalingM
  m = mat4Mul(m, mat4Identity());                               // scalingRot (I)
  m = mat4Mul(m, translate(pivot[0], pivot[1], pivot[2]));      // invScalingCenterT
  m = mat4Mul(m, translate(-pivot[0], -pivot[1], -pivot[2]));   // rotationCenterT
  m = mat4Mul(m, rotateYawPitchRoll(yaw, pitch, roll));         // rotationM
  m = mat4Mul(m, translate(pivot[0], pivot[1], pivot[2]));      // invRotationCenterT
  m = mat4Mul(m, translate(t[0], t[1], t[2]));                  // finalT
  return m;
}

float smoothstepf(float a, float b, float x) {
  // HLSL smoothstep(edge0,edge1,x): note edge0 may be > edge1 (the shader passes 1+FallOff, 1).
  float t = (x - a) / (b - a);
  if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
  return t * t * (3.0f - 2.0f * t);
}
float modf_hlsl(float x, float y) { return x - y * std::floor(x / y); }  // HLSL mod = fmod-with-floor

// count: pure consumer, topology unchanged. Unwired → 0/0.
void selectVerticesCount(const std::map<std::string, float>* /*params*/, const SwMeshView* inputs,
                         int inputCount, uint32_t& vtx, uint32_t& idx) {
  if (inputCount < 1 || !inputs[0].vtx) { vtx = 0; idx = 0; return; }
  vtx = inputs[0].vtxCount; idx = inputs[0].faceCount;
}

void selectVerticesCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  if (c.inputMeshCount < 1 || !c.inputMeshes[0].vtx || !c.inputMeshes[0].idx) return;  // unwired → empty
  const SwMeshView& in = c.inputMeshes[0];

  float centerDef[3] = {0,0,0}, center[3];   cookMeshVecN(c.params, "Center", centerDef, 3, center);
  float stretchDef[3] = {1,1,1}, stretch[3]; cookMeshVecN(c.params, "Stretch", stretchDef, 3, stretch);
  float rotDef[3] = {0,0,0}, rot[3];         cookMeshVecN(c.params, "Rotate", rotDef, 3, rot);
  float scale = cookMeshParam(c.params, "Scale", 1.0f);
  float fallOff = cookMeshParam(c.params, "FallOff", 0.5f);
  float volumeShape = cookMeshParam(c.params, "VolumeShape", 0.0f);  // 0=Sphere 1=Box 2=Plane 3=Zebra 4=Noise
  float mode = cookMeshParam(c.params, "Mode", 0.0f);                // 0=Override 1=Add 2=Sub 3=Multiply 4=Invert
  float clampResult = cookMeshParam(c.params, "ClampResult", 0.0f);
  float strength = cookMeshParam(c.params, "Strength", 1.0f);
  float phase = cookMeshParam(c.params, "Phase", 0.0f);
  float threshold = cookMeshParam(c.params, "Threshold", 0.0f);

  float scaling[3] = {stretch[0]*scale, stretch[1]*scale, stretch[2]*scale};
  float yaw = rot[1]*kDeg2Rad, pitch = rot[0]*kDeg2Rad, roll = rot[2]*kDeg2Rad;
  float pivot[3] = {0,0,0};
  Mat4 gizmo = buildTransformMatrix(pivot, scaling, yaw, pitch, roll, center);
  Mat4 transformVolume; if (!mat4Inverse(gizmo, transformVolume)) transformVolume = mat4Identity();

  const SwVertex* src = (const SwVertex*)const_cast<MTL::Buffer*>(in.vtx)->contents();
  const SwTriIndex* srcIdx = (const SwTriIndex*)const_cast<MTL::Buffer*>(in.idx)->contents();
  SwVertex* dst = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* dstIdx = (SwTriIndex*)c.output_indices->contents();

  uint32_t nv = c.vertexCount < in.vtxCount ? c.vertexCount : in.vtxCount;
  for (uint32_t i = 0; i < nv; ++i) {
    SwVertex v = src[i];  // copy all fields; only Selected is overwritten
    float pv[3];
    mat4TransformPointDivW(transformVolume, v.Position.x, v.Position.y, v.Position.z, pv);

    float s = 1.0f;
    if (volumeShape < 0.5f) {  // Sphere
      float dist = std::sqrt(pv[0]*pv[0] + pv[1]*pv[1] + pv[2]*pv[2]);
      s = smoothstepf(1.0f + fallOff, 1.0f, dist);
    } else if (volumeShape < 1.5f) {  // Box
      float tx=std::fabs(pv[0]), ty=std::fabs(pv[1]), tz=std::fabs(pv[2]);
      float dist = std::fmax(std::fmax(tx, ty), tz) + phase;
      s = smoothstepf(1.0f + fallOff, 1.0f, dist);
    } else if (volumeShape < 2.5f) {  // Plane
      s = smoothstepf(fallOff, 0.0f, pv[1]);
    } else if (volumeShape < 3.5f) {  // Zebra
      float dist = 1.0f - std::fabs(modf_hlsl(pv[1] * 1.0f + phase, 2.0f) - 1.0f);
      s = smoothstepf(threshold + 0.5f + fallOff, threshold + 0.5f, dist);
    } else {  // Noise — NAMED DEFER (no host snoise): leave s = 1 (the shader's pre-branch default).
      s = 1.0f;
    }

    float sel = v.Selection;
    if (mode < 0.5f)      s = s * strength;                       // Override
    else if (mode < 1.5f) s = s + sel * strength;                // Add
    else if (mode < 2.5f) s = sel - s * strength;                // Sub
    else if (mode < 3.5f) s = sel + (sel * s - sel) * strength;  // Multiply = lerp(sel, sel*s, strength)
    else                  s = s * (1.0f - sel);                  // Invert

    if (clampResult >= 0.5f) { if (s < 0.0f) s = 0.0f; else if (s > 1.0f) s = 1.0f; }
    v.Selection = s;
    dst[i] = v;
  }

  uint32_t nf = c.indexCount < in.faceCount ? c.indexCount : in.faceCount;
  for (uint32_t f = 0; f < nf; ++f) dstIdx[f] = srcIdx[f];  // topology unchanged

  // Test injection (golden RED): corrupt the FIRST output vertex's Selection (the op's primary output)
  // AND Position (so a production unlit pixel golden could bite). Bites the REAL cook path.
  if (meshInjectBug() && c.vertexCount > 0) {
    dst[0].Selection = -999.0f;
    dst[0].Position = {-999.0f, -999.0f, -999.0f};
  }
}

NodeSpec selectVerticesSpec() {
  NodeSpec s;
  s.type = "SelectVertices";
  s.title = "Select Vertices";
  PortSpec meshIn; meshIn.id="Mesh"; meshIn.name="Mesh"; meshIn.dataType="Mesh"; meshIn.isInput=true;
  PortSpec vs; vs.id="VolumeShape"; vs.name="VolumeShape"; vs.dataType="Float"; vs.isInput=true;
  vs.def=0.0f; vs.minV=0.0f; vs.maxV=4.0f;  // Sphere/Box/Plane/Zebra/Noise
  PortSpec cx; cx.id="Center.x"; cx.name="Center"; cx.dataType="Float"; cx.isInput=true;
  cx.def=0.0f; cx.minV=-10.0f; cx.maxV=10.0f; cx.widget=Widget::Vec; cx.vecArity=3;
  PortSpec cy; cy.id="Center.y"; cy.name="Center.y"; cy.dataType="Float"; cy.isInput=true; cy.def=0.0f; cy.minV=-10.0f; cy.maxV=10.0f;
  PortSpec cz; cz.id="Center.z"; cz.name="Center.z"; cz.dataType="Float"; cz.isInput=true; cz.def=0.0f; cz.minV=-10.0f; cz.maxV=10.0f;
  PortSpec stx; stx.id="Stretch.x"; stx.name="Stretch"; stx.dataType="Float"; stx.isInput=true;
  stx.def=1.0f; stx.minV=-10.0f; stx.maxV=10.0f; stx.widget=Widget::Vec; stx.vecArity=3;
  PortSpec sty; sty.id="Stretch.y"; sty.name="Stretch.y"; sty.dataType="Float"; sty.isInput=true; sty.def=1.0f; sty.minV=-10.0f; sty.maxV=10.0f;
  PortSpec stz; stz.id="Stretch.z"; stz.name="Stretch.z"; stz.dataType="Float"; stz.isInput=true; stz.def=1.0f; stz.minV=-10.0f; stz.maxV=10.0f;
  PortSpec sc; sc.id="Scale"; sc.name="Scale"; sc.dataType="Float"; sc.isInput=true; sc.def=1.0f; sc.minV=-10.0f; sc.maxV=10.0f;
  PortSpec rx; rx.id="Rotate.x"; rx.name="Rotate"; rx.dataType="Float"; rx.isInput=true;
  rx.def=0.0f; rx.minV=-360.0f; rx.maxV=360.0f; rx.widget=Widget::Vec; rx.vecArity=3;
  PortSpec ry; ry.id="Rotate.y"; ry.name="Rotate.y"; ry.dataType="Float"; ry.isInput=true; ry.def=0.0f; ry.minV=-360.0f; ry.maxV=360.0f;
  PortSpec rz; rz.id="Rotate.z"; rz.name="Rotate.z"; rz.dataType="Float"; rz.isInput=true; rz.def=0.0f; rz.minV=-360.0f; rz.maxV=360.0f;
  PortSpec fo; fo.id="FallOff"; fo.name="FallOff"; fo.dataType="Float"; fo.isInput=true; fo.def=0.5f; fo.minV=0.0f; fo.maxV=2.0f;
  PortSpec md; md.id="Mode"; md.name="Mode"; md.dataType="Float"; md.isInput=true; md.def=0.0f; md.minV=0.0f; md.maxV=4.0f;
  PortSpec cl; cl.id="ClampResult"; cl.name="ClampResult"; cl.dataType="Float"; cl.isInput=true; cl.def=0.0f; cl.minV=0.0f; cl.maxV=1.0f; cl.widget=Widget::Bool;
  PortSpec st; st.id="Strength"; st.name="Strength"; st.dataType="Float"; st.isInput=true; st.def=1.0f; st.minV=-2.0f; st.maxV=2.0f;
  PortSpec ph; ph.id="Phase"; ph.name="Phase"; ph.dataType="Float"; ph.isInput=true; ph.def=0.0f; ph.minV=-10.0f; ph.maxV=10.0f;
  PortSpec th; th.id="Threshold"; th.name="Threshold"; th.dataType="Float"; th.isInput=true; th.def=0.0f; th.minV=-1.0f; th.maxV=1.0f;
  PortSpec out; out.id="Result"; out.name="Result"; out.dataType="Mesh"; out.isInput=false;
  s.ports = {meshIn, vs, cx, cy, cz, stx, sty, stz, sc, rx, ry, rz, fo, md, cl, st, ph, th, out};
  return s;
}

const MeshOp g_selectVerticesOp(selectVerticesSpec(), selectVerticesCount, selectVerticesCook);

}  // namespace
}  // namespace sw
