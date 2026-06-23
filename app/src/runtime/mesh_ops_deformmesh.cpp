// DeformMesh mesh op (a mesh→mesh CONSUMER: per-vertex POSITION deform = Spherize + Taper + Twist, in
// that order, each blended by vertex Selection). TiXL authority:
// external/tixl/Operators/Lib/mesh/modify/DeformMesh.cs (one Mesh input + UseVertexSelection/Spherize/
// Radius/Pivot/Taper/TaperAxis/AmountPerAxis/Twist/TwistAxis/TwistPivot) + compute shader mesh-Deform.hlsl.
//
// VERBATIM math (mesh-Deform.hlsl main):
//   s = UseVertexSelection>0.5 ? Selected : 1
//   pos = SourceVerts.Position
//   spherePos = (pos - Pivot) * (Radius / length(pos - Pivot))
//   pos = lerp(pos, lerp(pos, spherePos, Spherize), s)
//   tapered = pos; switch TaperAxis:
//     X: tapered.yz *= TaperFunction(pos.x, Taper2*TaperAmount)
//     Y: tapered.xz *= TaperFunction(pos.y, Taper2*TaperAmount)
//     Z: tapered.xy *= TaperFunction(pos.z, Taper2*TaperAmount)
//   where TaperFunction(y, amt) = (1 - amt.x*y, 1 - amt.y*y)   (amt = Taper2*TaperAmount, Taper2=AmountPerAxis)
//   pos = lerp(pos, tapered, s)
//   twisted = TwistFunction(pos, radians(TwistAmount))   // TwistAmount in DEGREES → radians()
//     TwistFunction rotates (pos - TwistPivot) about TwistAxis by angle = axisComponent*twistAmount, +TwistPivot
//   Result.Position = lerp(pos, twisted, s);  Normal/Tangent/Bitangent/TexCoord/etc. copied verbatim.
//
// FORKS (named): none — Spherize/Taper/Twist all ported 1:1. Normals are NOT recomputed (the shader copies
// them through, so a strong deform leaves stale normals — faithful to TiXL; pair with RecomputeNormals).
// TwistAmount is DEGREES (radians() in the shader). TaperFunction's `amt` = AmountPerAxis * Taper.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <cmath>
#include <map>
#include <string>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/mesh_op_registry.h"  // MeshOp self-registration + MeshCookCtx + SwMeshView
#include "runtime/sw_mesh.h"           // SwVertex (80B)

namespace sw {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kDeg2Rad = kPi / 180.0f;

void deformMeshCount(const std::map<std::string, float>* /*params*/, const SwMeshView* inputs,
                     int inputCount, uint32_t& vtx, uint32_t& idx) {
  if (inputCount < 1 || !inputs[0].vtx) { vtx = 0; idx = 0; return; }
  vtx = inputs[0].vtxCount; idx = inputs[0].faceCount;
}

void deformMeshCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  if (c.inputMeshCount < 1 || !c.inputMeshes[0].vtx || !c.inputMeshes[0].idx) return;  // unwired → empty
  const SwMeshView& in = c.inputMeshes[0];

  bool useSel = cookMeshParam(c.params, "UseVertexSelection", 0.0f) > 0.5f;
  float spherize = cookMeshParam(c.params, "Spherize", 0.0f);
  float radius = cookMeshParam(c.params, "Radius", 1.0f);
  float pivotDef[3] = {0,0,0}, pivot[3]; cookMeshVecN(c.params, "Pivot", pivotDef, 3, pivot);
  float taperAmt = cookMeshParam(c.params, "Taper", 0.0f);
  float taperAxis = cookMeshParam(c.params, "TaperAxis", 1.0f);  // 0=X 1=Y 2=Z (default Y)
  float apaDef[2] = {1,1}, apa[2]; cookMeshVecN(c.params, "AmountPerAxis", apaDef, 2, apa);  // Vector2
  float twistAmt = cookMeshParam(c.params, "Twist", 0.0f);       // DEGREES
  float twistAxis = cookMeshParam(c.params, "TwistAxis", 1.0f);  // 0=X 1=Y 2=Z (default Y)
  float twPivotDef[3] = {0,0,0}, twPivot[3]; cookMeshVecN(c.params, "TwistPivot", twPivotDef, 3, twPivot);

  float amt[2] = {apa[0] * taperAmt, apa[1] * taperAmt};  // Taper2 * TaperAmount

  const SwVertex* src = (const SwVertex*)const_cast<MTL::Buffer*>(in.vtx)->contents();
  const SwTriIndex* srcIdx = (const SwTriIndex*)const_cast<MTL::Buffer*>(in.idx)->contents();
  SwVertex* dst = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* dstIdx = (SwTriIndex*)c.output_indices->contents();

  int taxis = (int)taperAxis, twaxis = (int)twistAxis;
  float twistRad = twistAmt * kDeg2Rad;  // radians(TwistAmount)

  uint32_t nv = c.vertexCount < in.vtxCount ? c.vertexCount : in.vtxCount;
  for (uint32_t i = 0; i < nv; ++i) {
    SwVertex v = src[i];  // carry all attributes; only Position changes
    float s = useSel ? v.Selection : 1.0f;
    float pos[3] = {v.Position.x, v.Position.y, v.Position.z};

    // Spherize: spherePos = (pos-Pivot) * (Radius/len(pos-Pivot)); pos = lerp(pos, lerp(pos,spherePos,Spherize), s)
    float pwp[3] = {pos[0]-pivot[0], pos[1]-pivot[1], pos[2]-pivot[2]};
    float curR = std::sqrt(pwp[0]*pwp[0] + pwp[1]*pwp[1] + pwp[2]*pwp[2]);
    float sph[3] = {pwp[0], pwp[1], pwp[2]};
    if (curR > 1e-12f) { float k = radius / curR; sph[0]=pwp[0]*k; sph[1]=pwp[1]*k; sph[2]=pwp[2]*k; }
    float inner[3] = {pos[0] + (sph[0]-pos[0])*spherize, pos[1] + (sph[1]-pos[1])*spherize,
                      pos[2] + (sph[2]-pos[2])*spherize};
    pos[0] = pos[0] + (inner[0]-pos[0])*s;
    pos[1] = pos[1] + (inner[1]-pos[1])*s;
    pos[2] = pos[2] + (inner[2]-pos[2])*s;

    // Taper: scale the two off-axis components by TaperFunction(axisComp, amt) = (1-amt.x*y, 1-amt.y*y).
    float tapered[3] = {pos[0], pos[1], pos[2]};
    if (taxis == 0) {        // X: tapered.yz *= f(pos.x)
      float fy = 1.0f - amt[0]*pos[0], fz = 1.0f - amt[1]*pos[0];
      tapered[1] = pos[1]*fy; tapered[2] = pos[2]*fz;
    } else if (taxis == 1) { // Y: tapered.xz *= f(pos.y)
      float fx = 1.0f - amt[0]*pos[1], fz = 1.0f - amt[1]*pos[1];
      tapered[0] = pos[0]*fx; tapered[2] = pos[2]*fz;
    } else {                 // Z: tapered.xy *= f(pos.z)
      float fx = 1.0f - amt[0]*pos[2], fy = 1.0f - amt[1]*pos[2];
      tapered[0] = pos[0]*fx; tapered[1] = pos[1]*fy;
    }
    pos[0] = pos[0] + (tapered[0]-pos[0])*s;
    pos[1] = pos[1] + (tapered[1]-pos[1])*s;
    pos[2] = pos[2] + (tapered[2]-pos[2])*s;

    // Twist: rotate (pos - TwistPivot) about TwistAxis by angle = axisComponent*twistRad, then + TwistPivot.
    float twp[3] = {pos[0]-twPivot[0], pos[1]-twPivot[1], pos[2]-twPivot[2]};
    float tw[3] = {twp[0], twp[1], twp[2]};
    if (twaxis == 0) {        // X axis
      float ang = twp[0]*twistRad, ca=std::cos(ang), sa=std::sin(ang);
      tw[0]=twp[0]; tw[1]=twp[1]*ca - twp[2]*sa; tw[2]=twp[1]*sa + twp[2]*ca;
    } else if (twaxis == 1) { // Y axis
      float ang = twp[1]*twistRad, ca=std::cos(ang), sa=std::sin(ang);
      tw[0]=twp[0]*ca - twp[2]*sa; tw[1]=twp[1]; tw[2]=twp[0]*sa + twp[2]*ca;
    } else {                  // Z axis
      float ang = twp[2]*twistRad, ca=std::cos(ang), sa=std::sin(ang);
      tw[0]=twp[0]*ca - twp[1]*sa; tw[1]=twp[0]*sa + twp[1]*ca; tw[2]=twp[2];
    }
    float twisted[3] = {tw[0]+twPivot[0], tw[1]+twPivot[1], tw[2]+twPivot[2]};
    v.Position = {pos[0] + (twisted[0]-pos[0])*s, pos[1] + (twisted[1]-pos[1])*s,
                  pos[2] + (twisted[2]-pos[2])*s};
    dst[i] = v;
  }

  uint32_t nf = c.indexCount < in.faceCount ? c.indexCount : in.faceCount;
  for (uint32_t f = 0; f < nf; ++f) dstIdx[f] = srcIdx[f];  // topology unchanged

  if (meshInjectBug() && c.vertexCount > 0) dst[0].Position = {-999.0f, -999.0f, -999.0f};
}

NodeSpec deformMeshSpec() {
  NodeSpec s;
  s.type = "DeformMesh";
  s.title = "Deform Mesh";
  PortSpec meshIn; meshIn.id="Mesh"; meshIn.name="Mesh"; meshIn.dataType="Mesh"; meshIn.isInput=true;
  PortSpec uvs; uvs.id="UseVertexSelection"; uvs.name="UseVertexSelection"; uvs.dataType="Float"; uvs.isInput=true;
  uvs.def=0.0f; uvs.minV=0.0f; uvs.maxV=1.0f; uvs.widget=Widget::Bool;
  PortSpec sp; sp.id="Spherize"; sp.name="Spherize"; sp.dataType="Float"; sp.isInput=true; sp.def=0.0f; sp.minV=0.0f; sp.maxV=1.0f;
  PortSpec rad; rad.id="Radius"; rad.name="Radius"; rad.dataType="Float"; rad.isInput=true; rad.def=1.0f; rad.minV=0.0f; rad.maxV=10.0f;
  PortSpec px; px.id="Pivot.x"; px.name="Pivot"; px.dataType="Float"; px.isInput=true;
  px.def=0.0f; px.minV=-10.0f; px.maxV=10.0f; px.widget=Widget::Vec; px.vecArity=3;
  PortSpec py; py.id="Pivot.y"; py.name="Pivot.y"; py.dataType="Float"; py.isInput=true; py.def=0.0f; py.minV=-10.0f; py.maxV=10.0f;
  PortSpec pz; pz.id="Pivot.z"; pz.name="Pivot.z"; pz.dataType="Float"; pz.isInput=true; pz.def=0.0f; pz.minV=-10.0f; pz.maxV=10.0f;
  PortSpec tp; tp.id="Taper"; tp.name="Taper"; tp.dataType="Float"; tp.isInput=true; tp.def=0.0f; tp.minV=-2.0f; tp.maxV=2.0f;
  PortSpec ta; ta.id="TaperAxis"; ta.name="TaperAxis"; ta.dataType="Float"; ta.isInput=true; ta.def=1.0f; ta.minV=0.0f; ta.maxV=2.0f;
  PortSpec ax; ax.id="AmountPerAxis.x"; ax.name="AmountPerAxis"; ax.dataType="Float"; ax.isInput=true;
  ax.def=1.0f; ax.minV=-2.0f; ax.maxV=2.0f; ax.widget=Widget::Vec; ax.vecArity=2;
  PortSpec ay; ay.id="AmountPerAxis.y"; ay.name="AmountPerAxis.y"; ay.dataType="Float"; ay.isInput=true; ay.def=1.0f; ay.minV=-2.0f; ay.maxV=2.0f;
  PortSpec tw; tw.id="Twist"; tw.name="Twist"; tw.dataType="Float"; tw.isInput=true; tw.def=0.0f; tw.minV=-360.0f; tw.maxV=360.0f;
  PortSpec twa; twa.id="TwistAxis"; twa.name="TwistAxis"; twa.dataType="Float"; twa.isInput=true; twa.def=1.0f; twa.minV=0.0f; twa.maxV=2.0f;
  PortSpec tpx; tpx.id="TwistPivot.x"; tpx.name="TwistPivot"; tpx.dataType="Float"; tpx.isInput=true;
  tpx.def=0.0f; tpx.minV=-10.0f; tpx.maxV=10.0f; tpx.widget=Widget::Vec; tpx.vecArity=3;
  PortSpec tpy; tpy.id="TwistPivot.y"; tpy.name="TwistPivot.y"; tpy.dataType="Float"; tpy.isInput=true; tpy.def=0.0f; tpy.minV=-10.0f; tpy.maxV=10.0f;
  PortSpec tpz; tpz.id="TwistPivot.z"; tpz.name="TwistPivot.z"; tpz.dataType="Float"; tpz.isInput=true; tpz.def=0.0f; tpz.minV=-10.0f; tpz.maxV=10.0f;
  PortSpec out; out.id="Result"; out.name="Result"; out.dataType="Mesh"; out.isInput=false;
  s.ports = {meshIn, uvs, sp, rad, px, py, pz, tp, ta, ax, ay, tw, twa, tpx, tpy, tpz, out};
  return s;
}

const MeshOp g_deformMeshOp(deformMeshSpec(), deformMeshCount, deformMeshCook);

}  // namespace
}  // namespace sw
