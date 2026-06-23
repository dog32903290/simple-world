// TorusMesh mesh op (torus / donut generator) — rides the built mesh-op seam. TiXL authority:
// external/tixl/Operators/Lib/mesh/generate/TorusMesh.cs (+ .t3 defaults Radius=1, Thickness=0.5,
// Segments=(64,32), Spin=(0,0), Fill=(360,360), SmoothAngle=60). One .cpp owns the whole op.
//
// VERBATIM math (TorusMesh.cs:21-116):
//   radiusSegments = clamp(Width,1,1e4)+1 ; tubeSegments = clamp(Height,1,1e4)+1.               [:25-26]
//   radiusSpin = Spin.X*deg2rad ; spinMinorInRad = Spin.Y*deg2rad.                                [:29-30]
//   fillRadius = Fill.X/360 ; tubeFill = Fill.Y/360.                                              [:33-34]
//   faceCount = (tubeSegments-1)*(radiusSegments-1)*2 ; verticesCount = tubeSegments*radiusSegments. [:39-40]
//   tubeAngleFraction = tubeFill/(tubeSegments-1)*2PI ; radiusAngleFraction = fillRadius/(radiusSegments-1)*2PI. [:51-52]
//   Per tubeIndex: tubeAngle = tubeIndex*tubeAngleFraction + spinMinorInRad ;                     [:56]
//     tubePosition1X = sin(tubeAngle)*tubeRadius ; tubePosition1Y = cos(tubeAngle)*tubeRadius.    [:58-59]
//   Per radiusIndex: radiusAngle = radiusIndex*radiusAngleFraction + radiusSpin ;                 [:74]
//     p = (sin(radiusAngle)*(tubePosition1X+majorRadius), cos(radiusAngle)*(tubePosition1X+majorRadius), tubePosition1Y). [:76-78]
//   normal = normalize( flatShading ? cross(p-p1, p-p2) : p - tubeCenter1 ) where
//     tubeCenter1 = (sin(radiusAngle), cos(radiusAngle), 0)*majorRadius.                          [:92-95]
//   TBN via CalcTBNSpace (MeshUtils.cs:5-22, inlined faithfully).                                 [:97]
//   face: faceIndex = 2*(radiusIndex + tubeIndex*(radiusSegments-1)).                             [:69]
//     tri0=(vi, vi+1, vi+radiusSegments) ; tri1=(vi+radiusSegments, vi+1, vi+radiusSegments+1).   [:113-114]
// FORK: Texcoord2 left at PbrVertex default 0 (same as NGon/Quad). Color = (1,1,1).
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
struct V2 { float x, y; };
V3 sub(V3 a, V3 b) { return V3{a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 cross(V3 a, V3 b) { return V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 normalize(V3 v) {
  float len = std::sqrt(dot(v, v));
  if (len < 1e-20f) return V3{0, 0, 0};
  return V3{v.x / len, v.y / len, v.z / len};
}

// MeshUtils.CalcTBNSpace (MeshUtils.cs:5-22) — inlined 1:1.
void calcTBN(V3 p0, V2 uv0, V3 p1, V2 uv1, V3 p2, V2 uv2, V3 normal, V3& tangent, V3& bitangent) {
  V3 q1 = sub(p1, p0), q2 = sub(p2, p0);
  float s1 = uv1.x - uv0.x, t1 = uv1.y - uv0.y;
  float s2 = uv2.x - uv0.x, t2 = uv2.y - uv0.y;
  float denom = s1 * t2 - s2 * t1;
  float inv = denom != 0.0f ? 1.0f / denom : 0.0f;
  V3 t = V3{(q1.x * t2 - q2.x * t1) * inv, (q1.y * t2 - q2.y * t1) * inv, (q1.z * t2 - q2.z * t1) * inv};
  bitangent = normalize(cross(normal, t));
  tangent = normalize(cross(bitangent, normal));
}

int clampSeg1(float v) {
  int s = (int)(v + 0.5f);
  if (s < 1) s = 1;
  if (s > 10000) s = 10000;
  return s;
}

void torusCount(const std::map<std::string, float>* params, const SwMeshView* /*inputs*/,
                int /*inputCount*/, uint32_t& vtx, uint32_t& idx) {
  int radiusSegments = clampSeg1(cookMeshParam(params, "Segments.x", 64.0f)) + 1;
  int tubeSegments = clampSeg1(cookMeshParam(params, "Segments.y", 32.0f)) + 1;
  vtx = (uint32_t)(tubeSegments * radiusSegments);
  idx = (uint32_t)((tubeSegments - 1) * (radiusSegments - 1) * 2);
}

void torusCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  float majorRadius = cookMeshParam(c.params, "Radius", 1.0f);
  float tubeRadius = cookMeshParam(c.params, "Thickness", 0.5f);
  int radiusSegments = clampSeg1(cookMeshParam(c.params, "Segments.x", 64.0f)) + 1;
  int tubeSegments = clampSeg1(cookMeshParam(c.params, "Segments.y", 32.0f)) + 1;
  float spinDef[2] = {0.0f, 0.0f};
  float spin[2]; cookMeshVecN(c.params, "Spin", spinDef, 2, spin);
  float radiusSpin = spin[0] * kDeg2Rad;
  float spinMinorInRad = spin[1] * kDeg2Rad;
  float fillDef[2] = {360.0f, 360.0f};
  float fill[2]; cookMeshVecN(c.params, "Fill", fillDef, 2, fill);
  float fillRadius = fill[0] / 360.0f;
  float tubeFill = fill[1] / 360.0f;
  float smoothAngle = cookMeshParam(c.params, "SmoothAngle", 60.0f);

  bool useFlatShading = fillRadius / tubeSegments > smoothAngle / 360.0f ||
                        tubeFill / radiusSegments > smoothAngle / 360.0f;

  SwVertex* verts = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* idx = (SwTriIndex*)c.output_indices->contents();
  std::memset(verts, 0, (size_t)c.vertexCount * sizeof(SwVertex));
  std::memset(idx, 0, (size_t)c.indexCount * sizeof(SwTriIndex));

  double tubeAngleFraction = (double)tubeFill / (tubeSegments - 1) * 2.0 * kPiD;
  double radiusAngleFraction = (double)fillRadius / (radiusSegments - 1) * 2.0 * kPiD;

  for (int tubeIndex = 0; tubeIndex < tubeSegments; ++tubeIndex) {
    double tubeAngle = tubeIndex * tubeAngleFraction + spinMinorInRad;
    double tubePosition1X = std::sin(tubeAngle) * tubeRadius;
    double tubePosition1Y = std::cos(tubeAngle) * tubeRadius;
    double tubePosition2X = std::sin(tubeAngle + tubeAngleFraction) * tubeRadius;
    double tubePosition2Y = std::cos(tubeAngle + tubeAngleFraction) * tubeRadius;
    float v0 = tubeIndex / (float)(tubeSegments - 1);
    float v1 = (tubeIndex + 1) / (float)(tubeSegments - 1);

    for (int radiusIndex = 0; radiusIndex < radiusSegments; ++radiusIndex) {
      int vertexIndex = radiusIndex + tubeIndex * radiusSegments;
      int faceIndex = 2 * (radiusIndex + tubeIndex * (radiusSegments - 1));
      float u0 = radiusIndex / (float)(radiusSegments - 1);
      float u1 = (radiusIndex + 1) / (float)(radiusSegments - 1);
      double radiusAngle = radiusIndex * radiusAngleFraction + radiusSpin;

      V3 p = {(float)(std::sin(radiusAngle) * (tubePosition1X + majorRadius)),
              (float)(std::cos(radiusAngle) * (tubePosition1X + majorRadius)),
              (float)tubePosition1Y};
      V3 p1 = {(float)(std::sin(radiusAngle + radiusAngleFraction) * (tubePosition1X + majorRadius)),
               (float)(std::cos(radiusAngle + radiusAngleFraction) * (tubePosition1X + majorRadius)),
               (float)tubePosition1Y};
      V3 p2 = {(float)(std::sin(radiusAngle) * (tubePosition2X + majorRadius)),
               (float)(std::cos(radiusAngle) * (tubePosition2X + majorRadius)),
               (float)tubePosition2Y};
      (void)tubePosition2Y;

      V2 uv0 = {u0, v1}, uv1 = {u1, v1}, uv2 = {u1, v0};
      V3 tubeCenter1 = {(float)std::sin(radiusAngle) * majorRadius,
                        (float)std::cos(radiusAngle) * majorRadius, 0.0f};
      V3 normal0 = normalize(useFlatShading ? cross(sub(p, p1), sub(p, p2)) : sub(p, tubeCenter1));
      V3 tangent0, binormal0;
      calcTBN(p, uv0, p1, uv1, p2, uv2, normal0, tangent0, binormal0);

      SwVertex& v = verts[vertexIndex];
      v.Position = {p.x, p.y, p.z};
      v.Normal = {normal0.x, normal0.y, normal0.z};
      v.Tangent = {tangent0.x, tangent0.y, tangent0.z};
      v.Bitangent = {binormal0.x, binormal0.y, binormal0.z};
      v.Texcoord = {uv0.x, uv0.y};
      v.Selection = 1.0f;
      v.ColorRgb = {1, 1, 1};

      if (tubeIndex >= tubeSegments - 1 || radiusIndex >= radiusSegments - 1) continue;
      idx[faceIndex + 0].X = vertexIndex + 0;
      idx[faceIndex + 0].Y = vertexIndex + 1;
      idx[faceIndex + 0].Z = vertexIndex + radiusSegments;
      idx[faceIndex + 1].X = vertexIndex + radiusSegments;
      idx[faceIndex + 1].Y = vertexIndex + 1;
      idx[faceIndex + 1].Z = vertexIndex + radiusSegments + 1;
    }
  }

  // Test injection (golden RED): corrupt the first vertex position in the REAL output.
  if (meshInjectBug() && c.vertexCount > 0) verts[0].Position = {-999.0f, -999.0f, -999.0f};
}

NodeSpec torusSpec() {
  NodeSpec s;
  s.type = "TorusMesh";
  s.title = "Torus Mesh";
  PortSpec rad; rad.id = "Radius"; rad.name = "Radius"; rad.dataType = "Float"; rad.isInput = true;
  rad.def = 1.0f; rad.minV = 0.0f; rad.maxV = 10.0f;
  PortSpec thk; thk.id = "Thickness"; thk.name = "Thickness"; thk.dataType = "Float"; thk.isInput = true;
  thk.def = 0.5f; thk.minV = 0.0f; thk.maxV = 10.0f;
  PortSpec sgx; sgx.id = "Segments.x"; sgx.name = "Segments"; sgx.dataType = "Float"; sgx.isInput = true;
  sgx.def = 64.0f; sgx.minV = 1.0f; sgx.maxV = 256.0f; sgx.widget = Widget::Vec; sgx.vecArity = 2;
  PortSpec sgy; sgy.id = "Segments.y"; sgy.name = "Segments.y"; sgy.dataType = "Float"; sgy.isInput = true;
  sgy.def = 32.0f; sgy.minV = 1.0f; sgy.maxV = 256.0f;
  PortSpec spx; spx.id = "Spin.x"; spx.name = "Spin"; spx.dataType = "Float"; spx.isInput = true;
  spx.def = 0.0f; spx.minV = -360.0f; spx.maxV = 360.0f; spx.widget = Widget::Vec; spx.vecArity = 2;
  PortSpec spy; spy.id = "Spin.y"; spy.name = "Spin.y"; spy.dataType = "Float"; spy.isInput = true;
  spy.def = 0.0f; spy.minV = -360.0f; spy.maxV = 360.0f;
  PortSpec flx; flx.id = "Fill.x"; flx.name = "Fill"; flx.dataType = "Float"; flx.isInput = true;
  flx.def = 360.0f; flx.minV = 0.0f; flx.maxV = 360.0f; flx.widget = Widget::Vec; flx.vecArity = 2;
  PortSpec fly; fly.id = "Fill.y"; fly.name = "Fill.y"; fly.dataType = "Float"; fly.isInput = true;
  fly.def = 360.0f; fly.minV = 0.0f; fly.maxV = 360.0f;
  PortSpec sma; sma.id = "SmoothAngle"; sma.name = "SmoothAngle"; sma.dataType = "Float"; sma.isInput = true;
  sma.def = 60.0f; sma.minV = 0.0f; sma.maxV = 180.0f;
  PortSpec out; out.id = "Data"; out.name = "Data"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {rad, thk, sgx, sgy, spx, spy, flx, fly, sma, out};
  return s;
}

const MeshOp g_torusMeshOp(torusSpec(), torusCount, torusCook);

}  // namespace
}  // namespace sw
