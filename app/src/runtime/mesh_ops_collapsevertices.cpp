// CollapseVertices mesh op (a mesh→mesh CONSUMER: snaps each vertex POSITION to a power-of-two grid, the
// grid level chosen by a volume FIELD value — a low-poly / voxelize look). TiXL authority:
// external/tixl/Operators/Lib/mesh/modify/CollapseVertices.cs (ITransformable gizmo + Amount/StepCount/
// SmoothSteps/GridOffset/VolumeShape/FallOff/Phase + the volume TRS) + compute shader mesh-CollapseVertices.hlsl.
//
// The .hlsl is the MATH AUTHORITY (the .cs only wires inputs to its two cbuffers). VERBATIM (b0:
// TransformVolume,Amount,FallOff,Strength,Phase,GridOffset(3),NoiseThreshold,BlendStep ; b1: Count,
// VolumeShape,StepCount). Per vertex:
//   s = GetFieldAtPosition(pos)   // posInVolume = mul(float4(pos,1),TransformVolume); linearstep field
//       Sphere: linearstep(1+FallOff,1,length); Box: linearstep(1+FallOff,1, max|t|+Phase);
//       Plane:  linearstep(FallOff,0, pv.y);   Zebra: 1-abs(mod(pv.y+Phase,2)-1) → linearstep around Th+0.5
//   xx = s*StepCount; step = uint(xx); ff = xx - step
//   if (s > 0.1/StepCount):
//     maxS = 1<<StepCount; ss1 = (1<<step)/maxS*Strength; snap1 = floor((pos-GridOffset)/ss1+0.5)*ss1
//     ss2 = (1<<(step+1))/maxS*Strength; snap2 = floor((pos-GridOffset)/ss2+0.5)*ss2
//     v.Position = lerp(pos, lerp(snap1,snap2, smoothstep(0,1,ff*BlendStep)) + GridOffset, Amount)
//   else: position unchanged. All other fields copied verbatim.
//
// FORKS (named): Noise volume mode leaves the field at s=GetFieldAtPosition's pre-switch default 1
// (snoise not ported); a Noise selection therefore snaps at full level — a NAMED DEFER. SmoothSteps (.cs)
// maps to the shader's BlendStep; GridSize/Extend/Radius/InnerRadius (.cs gizmo-drawing knobs) do NOT
// reach the compute cbuffer (they drive the gizmo .t3 only) → not exposed. Strength default 1, BlendStep
// default 1, StepCount default 4 (TiXL .t3 defaults). The gizmo TRS → ResultInverted = TransformVolume.
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
Mat4 buildGizmoMatrix(const float scaling[3], float yaw, float pitch, float roll, const float t[3]) {
  // pivot 0 (CollapseVertices gizmo has no pivot input).
  Mat4 m = scaleMat(scaling[0], scaling[1], scaling[2]);
  m = mat4Mul(m, rotateYawPitchRoll(yaw, pitch, roll));
  m = mat4Mul(m, translate(t[0], t[1], t[2]));
  return m;
}

float linearstep(float a, float b, float f) {  // saturate((f-a)/(b-a))
  float t = (f - a) / (b - a);
  return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}
float smoothstep01(float x) {  // smoothstep(0,1,x)
  float t = x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
  return t * t * (3.0f - 2.0f * t);
}
float modf_hlsl(float x, float y) { return x - y * std::floor(x / y); }

void collapseVerticesCount(const std::map<std::string, float>* /*params*/, const SwMeshView* inputs,
                           int inputCount, uint32_t& vtx, uint32_t& idx) {
  if (inputCount < 1 || !inputs[0].vtx) { vtx = 0; idx = 0; return; }
  vtx = inputs[0].vtxCount; idx = inputs[0].faceCount;
}

void collapseVerticesCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  if (c.inputMeshCount < 1 || !c.inputMeshes[0].vtx || !c.inputMeshes[0].idx) return;  // unwired → empty
  const SwMeshView& in = c.inputMeshes[0];

  float amount = cookMeshParam(c.params, "Amount", 1.0f);
  int stepCount = (int)cookMeshParam(c.params, "StepCount", 4.0f);
  if (stepCount < 1) stepCount = 1;
  float blendStep = cookMeshParam(c.params, "SmoothSteps", 1.0f);  // .cs SmoothSteps → shader BlendStep
  float strength = cookMeshParam(c.params, "Strength", 1.0f);
  float goDef[3] = {0,0,0}, gridOffset[3]; cookMeshVecN(c.params, "GridOffset", goDef, 3, gridOffset);
  float volumeShape = cookMeshParam(c.params, "VolumeShape", 0.0f);
  float centerDef[3] = {0,0,0}, center[3];   cookMeshVecN(c.params, "Center", centerDef, 3, center);
  float stretchDef[3] = {1,1,1}, stretch[3]; cookMeshVecN(c.params, "Stretch", stretchDef, 3, stretch);
  float rotDef[3] = {0,0,0}, rot[3];         cookMeshVecN(c.params, "Rotate", rotDef, 3, rot);
  float scale = cookMeshParam(c.params, "Scale", 1.0f);
  float fallOff = cookMeshParam(c.params, "FallOff", 0.5f);
  float phase = cookMeshParam(c.params, "Phase", 0.0f);
  float noiseThreshold = cookMeshParam(c.params, "Threshold", 0.0f);

  float scaling[3] = {stretch[0]*scale, stretch[1]*scale, stretch[2]*scale};
  Mat4 gizmo = buildGizmoMatrix(scaling, rot[1]*kDeg2Rad, rot[0]*kDeg2Rad, rot[2]*kDeg2Rad, center);
  Mat4 transformVolume; if (!mat4Inverse(gizmo, transformVolume)) transformVolume = mat4Identity();

  auto field = [&](const float pos[3]) -> float {
    float pv[3]; mat4TransformPointDivW(transformVolume, pos[0], pos[1], pos[2], pv);
    float s = 1.0f;
    if (volumeShape < 0.5f) {  // Sphere
      float d = std::sqrt(pv[0]*pv[0]+pv[1]*pv[1]+pv[2]*pv[2]);
      s = linearstep(1.0f+fallOff, 1.0f, d);
    } else if (volumeShape < 1.5f) {  // Box
      float t = std::fmax(std::fmax(std::fabs(pv[0]), std::fabs(pv[1])), std::fabs(pv[2])) + phase;
      s = linearstep(1.0f+fallOff, 1.0f, t);
    } else if (volumeShape < 2.5f) {  // Plane
      s = linearstep(fallOff, 0.0f, pv[1]);
    } else if (volumeShape < 3.5f) {  // Zebra
      float d = 1.0f - std::fabs(modf_hlsl(pv[1] + phase, 2.0f) - 1.0f);
      s = linearstep(noiseThreshold + 0.5f + fallOff, noiseThreshold + 0.5f, d);
    } else {  // Noise — NAMED DEFER: keep s = 1 (pre-switch default; snoise not ported).
      s = 1.0f;
    }
    return s;
  };

  const SwVertex* src = (const SwVertex*)const_cast<MTL::Buffer*>(in.vtx)->contents();
  const SwTriIndex* srcIdx = (const SwTriIndex*)const_cast<MTL::Buffer*>(in.idx)->contents();
  SwVertex* dst = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* dstIdx = (SwTriIndex*)c.output_indices->contents();

  float maxS = (float)(1 << stepCount);

  uint32_t nv = c.vertexCount < in.vtxCount ? c.vertexCount : in.vtxCount;
  for (uint32_t i = 0; i < nv; ++i) {
    SwVertex v = src[i];
    float pos[3] = {v.Position.x, v.Position.y, v.Position.z};
    float s = field(pos);
    float xx = s * (float)stepCount;
    int step = (int)xx;
    float ff = xx - (float)step;

    if (s > 0.1f / (float)stepCount) {
      float ss1 = (float)(1 << step) / maxS * strength;
      float ss2 = (float)(1 << (step + 1)) / maxS * strength;
      auto snap = [&](float ss, float out[3]) {
        if (ss == 0.0f) { out[0]=pos[0]; out[1]=pos[1]; out[2]=pos[2]; return; }
        out[0] = std::floor((pos[0]-gridOffset[0])/ss + 0.5f) * ss;
        out[1] = std::floor((pos[1]-gridOffset[1])/ss + 0.5f) * ss;
        out[2] = std::floor((pos[2]-gridOffset[2])/ss + 0.5f) * ss;
      };
      float snap1[3], snap2[3]; snap(ss1, snap1); snap(ss2, snap2);
      float bl = smoothstep01(ff * blendStep);
      float mid[3] = {snap1[0] + (snap2[0]-snap1[0])*bl + gridOffset[0],
                      snap1[1] + (snap2[1]-snap1[1])*bl + gridOffset[1],
                      snap1[2] + (snap2[2]-snap1[2])*bl + gridOffset[2]};
      v.Position = {pos[0] + (mid[0]-pos[0])*amount, pos[1] + (mid[1]-pos[1])*amount,
                    pos[2] + (mid[2]-pos[2])*amount};
    }
    dst[i] = v;
  }

  uint32_t nf = c.indexCount < in.faceCount ? c.indexCount : in.faceCount;
  for (uint32_t f = 0; f < nf; ++f) dstIdx[f] = srcIdx[f];  // topology unchanged

  if (meshInjectBug() && c.vertexCount > 0) dst[0].Position = {-999.0f, -999.0f, -999.0f};
}

NodeSpec collapseVerticesSpec() {
  NodeSpec s;
  s.type = "CollapseVertices";
  s.title = "Collapse Vertices";
  PortSpec meshIn; meshIn.id="Mesh"; meshIn.name="Mesh"; meshIn.dataType="Mesh"; meshIn.isInput=true;
  PortSpec am; am.id="Amount"; am.name="Amount"; am.dataType="Float"; am.isInput=true; am.def=1.0f; am.minV=0.0f; am.maxV=1.0f;
  PortSpec stc; stc.id="StepCount"; stc.name="StepCount"; stc.dataType="Float"; stc.isInput=true; stc.def=4.0f; stc.minV=1.0f; stc.maxV=16.0f;
  PortSpec ss; ss.id="SmoothSteps"; ss.name="SmoothSteps"; ss.dataType="Float"; ss.isInput=true; ss.def=1.0f; ss.minV=0.0f; ss.maxV=4.0f;
  PortSpec str; str.id="Strength"; str.name="Strength"; str.dataType="Float"; str.isInput=true; str.def=1.0f; str.minV=-4.0f; str.maxV=4.0f;
  PortSpec gox; gox.id="GridOffset.x"; gox.name="GridOffset"; gox.dataType="Float"; gox.isInput=true;
  gox.def=0.0f; gox.minV=-10.0f; gox.maxV=10.0f; gox.widget=Widget::Vec; gox.vecArity=3;
  PortSpec goy; goy.id="GridOffset.y"; goy.name="GridOffset.y"; goy.dataType="Float"; goy.isInput=true; goy.def=0.0f; goy.minV=-10.0f; goy.maxV=10.0f;
  PortSpec goz; goz.id="GridOffset.z"; goz.name="GridOffset.z"; goz.dataType="Float"; goz.isInput=true; goz.def=0.0f; goz.minV=-10.0f; goz.maxV=10.0f;
  PortSpec vs; vs.id="VolumeShape"; vs.name="VolumeShape"; vs.dataType="Float"; vs.isInput=true; vs.def=0.0f; vs.minV=0.0f; vs.maxV=4.0f;
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
  PortSpec ph; ph.id="Phase"; ph.name="Phase"; ph.dataType="Float"; ph.isInput=true; ph.def=0.0f; ph.minV=-10.0f; ph.maxV=10.0f;
  PortSpec th; th.id="Threshold"; th.name="Threshold"; th.dataType="Float"; th.isInput=true; th.def=0.0f; th.minV=-1.0f; th.maxV=1.0f;
  PortSpec out; out.id="Result"; out.name="Result"; out.dataType="Mesh"; out.isInput=false;
  s.ports = {meshIn, am, stc, ss, str, gox, goy, goz, vs, cx, cy, cz, stx, sty, stz, sc, rx, ry, rz, fo, ph, th, out};
  return s;
}

const MeshOp g_collapseVerticesOp(collapseVerticesSpec(), collapseVerticesCount, collapseVerticesCook);

}  // namespace
}  // namespace sw
