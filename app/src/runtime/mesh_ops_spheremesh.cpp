// SphereMesh mesh op (UV-sphere generator) — rides the built mesh-op seam (MeshOp self-registration,
// MeshCookCtx, cookMeshParam/VecN, SwVertex/SwTriIndex). TiXL authority:
// external/tixl/Operators/Lib/mesh/generate/SphereMesh.cs (+ .t3 defaults Radius=1, Segments=(64,32)).
// One .cpp owns the whole op (NodeSpec + MeshCountFn + MeshCookFn). Base machinery FROZEN.
//
// VERBATIM math (SphereMesh.cs:23-174). SphereMesh takes ONLY Radius + Segments — NO rotation, center
// or scale (unlike NGon/Quad), so positions are pure trig.
//   uSegments = clamp(Width,2,1e4)+1 ; vSegments = clamp(Height,2,1e4)+1.                       [:25-26]
//   verticesCount = (vSegments+1) * uSegments.                                                  [:30]
//   polTriangleCount  = 2*uSegments ; sideTriangleCount = (vSegments-2)*uSegments*2 ;           [:27-28]
//   triangleCount = polTriangleCount + sideTriangleCount.                                       [:29]
//   vAngleFraction = (1/(vSegments-1))*PI ; uAngleFraction = (1/(uSegments-1))*2*PI.            [:40-41]
//   Per v-ring vIndex (0..vSegments-1):                                                         [:43]
//     vAngle = vIndex*vAngleFraction ; tubePosition1Y = cos(vAngle)*radius ;
//     radius1 = sin(vAngleFraction*vIndex)*radius ; v0 = 1 - vIndex/(vSegments-1).              [:45-49]
//   Poles (vIndex==0 top, ==vSegments-1 bottom) write fixed (0,+radius,0)/(0,-radius,0) into rows
//     0..uSegments-1 and (vSegments-1)*uSegments.. (SphereMesh.cs:54-127) — the body verts of those
//     two rings are OVERWRITTEN by the pole loop, faithfully.
//   Side rings: p = (sin(uAngle)*radius1, tubePosition1Y, cos(uAngle)*radius1).                 [:139-142]
//     normal=normalize(p); tangent=normalize(n.z,0,-n.x); binormal=cross(n,tangent).            [:146-148]
//     faceIndex = 2*(uIndex + vIndex*(uSegments-1)); nextU=(uIndex+1)%uSegments.                [:134,164]
//     tri0 = (vV+nextU, vV+uIndex, vV+uIndex+uSegments)                                         [:166-168]
//     tri1 = (vV+nextU+uSegments, vV+nextU, vV+uIndex+uSegments)  where vV=vIndex*uSegments.    [:169-171]
// FORK (faithful to TiXL): (1) the pole-fan index writes (SphereMesh.cs:120-126) write the SAME Int3
// twice into _indexBufferData[uIndex] — a TiXL quirk; the polar fan thus leaves most face slots at
// their zero-init (0,0,0). We reproduce verbatim (do NOT "fix" it — that would diverge from TiXL).
// (2) Texcoord2 left at PbrVertex default 0 (poles + sides), same as NGon/Quad.
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

struct V3 { float x, y, z; };
V3 normalize(V3 v) {
  float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
  if (len < 1e-20f) return V3{0, 0, 0};
  return V3{v.x / len, v.y / len, v.z / len};
}
V3 cross(V3 a, V3 b) {
  return V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// SphereMesh.cs:25-26 — Width clamps to [2,1e4] then +1; Height likewise. (Int2 default 64/32.)
int clampSeg2(float v) {
  int s = (int)(v + 0.5f);
  if (s < 2) s = 2;
  if (s > 10000) s = 10000;
  return s;
}

void sphereCount(const std::map<std::string, float>* params, const SwMeshView* /*inputs*/,
                 int /*inputCount*/, uint32_t& vtx, uint32_t& idx) {
  int uSegments = clampSeg2(cookMeshParam(params, "Segments.x", 64.0f)) + 1;
  int vSegments = clampSeg2(cookMeshParam(params, "Segments.y", 32.0f)) + 1;
  int polTri = 2 * uSegments;
  int sideTri = (vSegments - 2) * uSegments * 2;
  vtx = (uint32_t)((vSegments + 1) * uSegments);
  idx = (uint32_t)(polTri + sideTri);
}

void sphereCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  float radius = cookMeshParam(c.params, "Radius", 1.0f);
  int uSegments = clampSeg2(cookMeshParam(c.params, "Segments.x", 64.0f)) + 1;
  int vSegments = clampSeg2(cookMeshParam(c.params, "Segments.y", 32.0f)) + 1;

  SwVertex* verts = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* idx = (SwTriIndex*)c.output_indices->contents();
  // Zero-init both buffers: TiXL allocates fresh PbrVertex[]/Int3[] (default 0) each frame, and its
  // pole-fan quirk leaves most face slots untouched (they stay 0). Match that explicitly.
  std::memset(verts, 0, (size_t)c.vertexCount * sizeof(SwVertex));
  std::memset(idx, 0, (size_t)c.indexCount * sizeof(SwTriIndex));

  double vAngleFraction = (1.0 / (vSegments - 1)) * kPiD;
  double uAngleFraction = (1.0 / (uSegments - 1)) * 2.0 * kPiD;

  for (int vIndex = 0; vIndex < vSegments; ++vIndex) {
    double vAngle = vIndex * vAngleFraction;
    float tubePosition1Y = (float)(std::cos(vAngle) * radius);
    float radius1 = (float)(std::sin(vAngleFraction * vIndex) * radius);

    bool isTop = vIndex == 0;
    bool isBottom = vIndex == vSegments - 1;

    if (isTop || isBottom) {
      // Pole rings (SphereMesh.cs:54-127): fixed pos (0,±radius,0). normalPol0 = Up if radius>0.
      V3 normalPol0 = radius > 0 ? V3{0, 1, 0} : V3{0, -1, 0};   // VectorT3.Up / .Down
      V3 normalPol1 = radius > 0 ? V3{0, -1, 0} : V3{0, 1, 0};
      for (int uIndex = 0; uIndex < uSegments; ++uIndex) {
        float u0 = uIndex / (float)(uSegments - 1);
        double uAngle = uIndex * uAngleFraction;
        // top pole vertex (row 0..uSegments-1)
        V3 tangentPol0 = normalize(V3{std::sin((float)uAngle), 0, std::cos((float)uAngle)});
        V3 binormalPol0 = normalize(V3{std::sin((float)uAngle + (float)kPiD / 2.0f), 0,
                                       std::cos((float)uAngle + (float)kPiD / 2.0f)});
        SwVertex& vt = verts[0 + uIndex];
        vt.Position = {0, radius, 0};
        vt.Normal = {normalPol0.x, normalPol0.y, normalPol0.z};
        vt.Tangent = {tangentPol0.x, tangentPol0.y, tangentPol0.z};
        vt.Bitangent = {binormalPol0.x, binormalPol0.y, binormalPol0.z};
        vt.Texcoord = {u0, 1.0f};
        vt.Selection = 1.0f;
        vt.ColorRgb = {1, 1, 1};
        // bottom pole vertex (row (vSegments-1)*uSegments..)
        V3 tangentPol1 = normalize(V3{std::sin((float)uAngle + (float)kPiD / 2.0f), 0,
                                      std::cos((float)uAngle + (float)kPiD / 2.0f)});
        V3 binormalPol1 = normalize(V3{std::sin((float)uAngle), 0, std::cos((float)uAngle)});
        SwVertex& vb = verts[(vSegments - 1) * uSegments + uIndex];
        vb.Position = {0, -radius, 0};
        vb.Normal = {normalPol1.x, normalPol1.y, normalPol1.z};
        vb.Tangent = {tangentPol1.x, tangentPol1.y, tangentPol1.z};
        vb.Bitangent = {binormalPol1.x, binormalPol1.y, binormalPol1.z};
        vb.Texcoord = {u0, 0.0f};
        vb.Selection = 1.0f;
        vb.ColorRgb = {1, 1, 1};

        if (uIndex >= uSegments - 1) continue;
        // FORK (faithful): SphereMesh.cs:120-126 writes the SAME Int3 twice into slot [uIndex].
        idx[uIndex].X = uIndex;
        idx[uIndex].Y = uIndex + uSegments;
        idx[uIndex].Z = uIndex + uSegments + 1;
      }
    } else {
      for (int uIndex = 0; uIndex < uSegments; ++uIndex) {
        int vVertexIndex = vIndex * uSegments;
        int faceIndex = 2 * (uIndex + vIndex * (uSegments - 1));
        float u0 = uIndex / (float)(uSegments - 1);
        double uAngle = uIndex * uAngleFraction;
        V3 p = {(float)(std::sin(uAngle) * radius1), tubePosition1Y, (float)(std::cos(uAngle) * radius1)};
        float v0 = 1.0f - vIndex / (float)(vSegments - 1);
        V3 normal0 = normalize(p);
        V3 tangent0 = normalize(V3{normal0.z, 0, -normal0.x});
        V3 binormal0 = cross(normal0, tangent0);
        SwVertex& v = verts[vVertexIndex + uIndex];
        v.Position = {p.x, p.y, p.z};
        v.Normal = {normal0.x, normal0.y, normal0.z};
        v.Tangent = {tangent0.x, tangent0.y, tangent0.z};
        v.Bitangent = {binormal0.x, binormal0.y, binormal0.z};
        v.Texcoord = {u0, v0};
        v.Selection = 1.0f;
        v.ColorRgb = {1, 1, 1};

        if (vIndex >= vSegments - 1 || uIndex >= uSegments - 1) continue;
        int nextUIndex = (uIndex + 1) % uSegments;
        idx[faceIndex + 0].X = vVertexIndex + nextUIndex;
        idx[faceIndex + 0].Y = vVertexIndex + uIndex + 0;
        idx[faceIndex + 0].Z = vVertexIndex + uIndex + uSegments;
        idx[faceIndex + 1].X = vVertexIndex + nextUIndex + uSegments;
        idx[faceIndex + 1].Y = vVertexIndex + nextUIndex;
        idx[faceIndex + 1].Z = vVertexIndex + uIndex + uSegments;
      }
    }
  }

  // Test injection (golden RED): corrupt the top-pole vertex position in the REAL output.
  if (meshInjectBug() && c.vertexCount > 0) verts[0].Position = {-999.0f, -999.0f, -999.0f};
}

NodeSpec sphereSpec() {
  NodeSpec s;
  s.type = "SphereMesh";
  s.title = "Sphere Mesh";
  // Radius = float (default 1). Segments = Int2 (Width=64, Height=32) -> two Float ports.
  PortSpec rad; rad.id = "Radius"; rad.name = "Radius"; rad.dataType = "Float"; rad.isInput = true;
  rad.def = 1.0f; rad.minV = 0.0f; rad.maxV = 10.0f;
  PortSpec sgx; sgx.id = "Segments.x"; sgx.name = "Segments"; sgx.dataType = "Float"; sgx.isInput = true;
  sgx.def = 64.0f; sgx.minV = 2.0f; sgx.maxV = 256.0f; sgx.widget = Widget::Vec; sgx.vecArity = 2;
  PortSpec sgy; sgy.id = "Segments.y"; sgy.name = "Segments.y"; sgy.dataType = "Float"; sgy.isInput = true;
  sgy.def = 32.0f; sgy.minV = 2.0f; sgy.maxV = 256.0f;
  PortSpec out; out.id = "Data"; out.name = "Data"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {rad, sgx, sgy, out};
  return s;
}

const MeshOp g_sphereMeshOp(sphereSpec(), sphereCount, sphereCook);

}  // namespace
}  // namespace sw
