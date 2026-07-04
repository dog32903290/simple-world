// DelaunayMesh mesh op — Delaunay-triangulate a 2D boundary point ring (+ fill/extra points) into the
// MESH currency. FIRST consumer of the pointlist-into-mesh seam (MeshCookCtx::inputPointLists + the
// countPts count twin, mesh_op_registry.h) — TiXL's BoundaryPoints/ExtraPoints are StructuredList<Point>
// (the CPU list currency) = sw's host PointList rail.
//
// TiXL authority: external/tixl/Operators/Lib/mesh/generate/DelaunayMesh.cs — Update (:23-414):
//   fillDensity = 1 - FillDensity (:32, inverted for the user); no points → warn+return (:37-41);
//   NaN-Scale filter (:66-98, hash cache = perf only); <3 valid → return (:106-110);
//   edge SUBDIVISION (:117-160): maxEdge = 1 - SubdivideLongEdges when >0; per boundary edge i→(i+1)%N,
//     if len > maxEdge && maxEdge > 1e-4: subdivisions = ceil(len/maxEdge), insert lerped points
//     (Position/Color lerp; F1=1, Orientation=identity, Scale=1, F2=1);
//   boundaryPolygon = subdivided ring (:163-168);
//   FILL: fillDensity < 0.9999 → Poisson samples over the bbox (:178-231), keep those inside the
//     polygon and not tooCloseToBoundary(fillDensity·Tweak); ELSE the ExtraPoints branch (:234-274):
//     NaN-filter, inside-polygon, not-too-close, append VERBATIM point;
//   TRIANGULATE all points on (x,y) (:281-284, DelaunatorSharp — fork-delaunay-bowyer-watson,
//     delaunay_core.h: SET-equal, order/rotation impl-specific);
//   UV bbox (:291-312, degenerate range → 1); VERTICES (:315-341): PbrVertex{Position, N=(0,0,1),
//     T=(1,0,0), B=(0,1,0), Texcoord=((x-minX)/rangeX,(y-minY)/rangeY), Selection=F1, ColorRgb};
//   TRIANGLE FILTER (:343-377): keep iff centroid inside boundaryPolygon; emit Int3(idx0, idx2, idx1)
//     — REVERSED winding (:375, Tixl back-face culling), so CCW triangulation → CW output faces.
//   .t3 defaults: SubdivideLongEdges=0, FillDensity=0 (→ fillDensity=1 → ExtraPoints branch), Seed=0,
//     Tweak=0.
//
// NAMED FORKS (beyond delaunay_core.h's two):
//   • fork-delaunay-no-hash-cache: the boundary-points hash cache (:53-104, :560-600) only skips
//     recomputation across frames; sw recomputes each cook — value-identical.
//   • fork-delaunay-count-runs-cook: counts require the FULL algorithm (triangulation decides face
//     count) → countPts runs it and stashes the built buffers; the cook that follows copies them out
//     (the driver's count→ensure→cook sequence is single-threaded per node).
//   • SwPoint field map (proven in point_ops_boundingboxpoints.cpp FORK 2): F1@12=FX1, F2@60=FX2,
//     Scale@48 — same 64-byte TiXL Point stride.
#include "runtime/graph.h"  // NodeSpec, PortSpec, Widget

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/delaunay_core.h"     // DotNetRandom / delaunayTriangulate / poisson / polygon predicates
#include "runtime/mesh_op_registry.h"  // MeshOp registrar + MeshCookCtx + countPts twin + meshInjectBug
#include "runtime/sw_mesh.h"           // SwVertex (80B) + SwTriIndex (12B)
#include "runtime/tixl_point.h"        // SwPoint (64B host point)

namespace sw {
namespace {

// A working point mirroring the TiXL Point fields the op touches.
struct DPoint {
  float px, py, pz;
  float f1;
  float qx, qy, qz, qw;      // Orientation
  float cr, cg, cb, ca;      // Color
  float sx, sy, sz;          // Scale
  float f2;
};

DPoint fromSw(const SwPoint& p) {
  return {p.Position.x, p.Position.y, p.Position.z, p.FX1,
          p.Rotation.x, p.Rotation.y, p.Rotation.z, p.Rotation.w,
          p.Color.x,    p.Color.y,    p.Color.z,    p.Color.w,
          p.Scale.x,    p.Scale.y,    p.Scale.z,    p.FX2};
}

// fork-delaunay-count-runs-cook: the single-slot stash countPts fills and the cook drains.
struct DelaunayResult {
  std::vector<SwVertex> verts;
  std::vector<SwTriIndex> faces;
};
DelaunayResult& delaunayStash() {
  static DelaunayResult r;
  return r;
}

// The full Update() walk (header line cites). Returns via the stash.
void delaunayCompute(const std::map<std::string, float>* params,
                     const std::vector<SwPoint>* boundaryList,
                     const std::vector<SwPoint>* extraList) {
  DelaunayResult& out = delaunayStash();
  out.verts.clear();
  out.faces.clear();

  const float tweak = cookMeshParam(params, "Tweak", 0.0f);
  const int seed = (int)cookMeshParam(params, "Seed", 0.0f);
  const float subdivideLongEdges = cookMeshParam(params, "SubdivideLongEdges", 0.0f);
  const float fillDensity = 1.0f - cookMeshParam(params, "FillDensity", 0.0f);  // :32 inverted

  if (!boundaryList || boundaryList->empty()) return;  // :37-41 "No points in list"

  // NaN-Scale filter (:72-92; cache skipped, fork-delaunay-no-hash-cache).
  std::vector<DPoint> pointArray;
  pointArray.reserve(boundaryList->size());
  for (const SwPoint& sp : *boundaryList) {
    if (std::isnan(sp.Scale.x) || std::isnan(sp.Scale.y) || std::isnan(sp.Scale.z)) continue;
    pointArray.push_back(fromSw(sp));
  }
  if (pointArray.size() < 3) return;  // :106-110

  // Edge subdivision (:117-160). maxEdge = 1 - SubdivideLongEdges when the knob is >0 (:119-122).
  float maxEdgeSubdivisionLength = 0.0f;
  if (subdivideLongEdges > 0.0f) maxEdgeSubdivisionLength = 1.0f - subdivideLongEdges;
  std::vector<DPoint> subdivided;
  const size_t nb = pointArray.size();
  for (size_t i = 0; i < nb; ++i) {
    const DPoint& cur = pointArray[i];
    const DPoint& nxt = pointArray[(i + 1) % nb];
    subdivided.push_back(cur);  // :130
    const float ex = nxt.px - cur.px, ey = nxt.py - cur.py;
    const float edgeLength = std::sqrt(ex * ex + ey * ey);  // :133-135 Vector2.Distance (xy only)
    if (edgeLength > maxEdgeSubdivisionLength && maxEdgeSubdivisionLength > 0.0001f) {  // :138
      const int subdivisions = (int)std::ceil(edgeLength / maxEdgeSubdivisionLength);   // :140
      for (int j = 1; j < subdivisions; ++j) {  // :142-156
        const float t = (float)j / (float)subdivisions;
        DPoint p{};
        p.px = cur.px + (nxt.px - cur.px) * t;  // Vector3.Lerp
        p.py = cur.py + (nxt.py - cur.py) * t;
        p.pz = cur.pz + (nxt.pz - cur.pz) * t;
        p.f1 = 1.0f;
        p.qx = 0; p.qy = 0; p.qz = 0; p.qw = 1;  // Quaternion.Identity
        p.cr = cur.cr + (nxt.cr - cur.cr) * t;   // Vector4.Lerp color
        p.cg = cur.cg + (nxt.cg - cur.cg) * t;
        p.cb = cur.cb + (nxt.cb - cur.cb) * t;
        p.ca = cur.ca + (nxt.ca - cur.ca) * t;
        p.sx = p.sy = p.sz = 1.0f;               // Vector3.One
        p.f2 = 1.0f;
        subdivided.push_back(p);
      }
    }
  }
  pointArray.swap(subdivided);  // :160

  // Boundary polygon = the subdivided ring (:163-168).
  std::vector<DelaunayV2> boundaryPolygon;
  boundaryPolygon.reserve(pointArray.size());
  for (const DPoint& p : pointArray) boundaryPolygon.push_back({p.px, p.py});

  std::vector<DPoint> allPoints = pointArray;  // :175

  if (fillDensity < 0.9999f) {  // :178 — Poisson fill branch
    float minXb = pointArray[0].px, maxXb = pointArray[0].px;
    float minYb = pointArray[0].py, maxYb = pointArray[0].py;
    for (const DPoint& p : pointArray) {  // :181-193
      minXb = std::fmin(minXb, p.px); maxXb = std::fmax(maxXb, p.px);
      minYb = std::fmin(minYb, p.py); maxYb = std::fmax(maxYb, p.py);
    }
    const std::vector<DelaunayV2> fillPoints =
        poissonDiscSamples(minXb, maxXb, minYb, maxYb, fillDensity, seed);  // :197
    const float minDistanceFromBoundary = fillDensity * tweak;              // :202
    for (const DelaunayV2& fp : fillPoints) {  // :205-216
      if (!pointInPolygon(fp, boundaryPolygon)) continue;
      if (tooCloseToBoundary(fp, minDistanceFromBoundary, boundaryPolygon)) continue;
      DPoint p{};  // :219-230
      p.px = fp.x; p.py = fp.y; p.pz = 0.0f;
      p.f1 = 1.0f;
      p.qx = 0; p.qy = 0; p.qz = 0; p.qw = 1;
      p.cr = p.cg = p.cb = p.ca = 1.0f;  // Vector4.One
      p.sx = p.sy = p.sz = 1.0f;
      p.f2 = 1.0f;
      allPoints.push_back(p);
    }
  } else if (extraList && !extraList->empty()) {  // :234-273 — ExtraPoints branch
    const float minDistanceFromBoundary = fillDensity * tweak;  // :244
    for (const SwPoint& sp : *extraList) {
      if (std::isnan(sp.Scale.x) || std::isnan(sp.Scale.y) || std::isnan(sp.Scale.z)) continue;  // :252
      const DelaunayV2 p2{sp.Position.x, sp.Position.y};
      if (!pointInPolygon(p2, boundaryPolygon)) continue;                                  // :259
      if (tooCloseToBoundary(p2, minDistanceFromBoundary, boundaryPolygon)) continue;      // :263
      allPoints.push_back(fromSw(sp));                                                     // :266
    }
  }

  // Triangulate on (x,y) (:281-284; fork-delaunay-bowyer-watson — SET parity, CCW-normalized).
  std::vector<DelaunayV2> flat;
  flat.reserve(allPoints.size());
  for (const DPoint& p : allPoints) flat.push_back({p.px, p.py});
  const std::vector<DelaunayTri> tris = delaunayTriangulate(flat);

  // UV bbox (:291-312): degenerate range < 1e-4 → 1.
  float minX = std::numeric_limits<float>::max(), maxX = std::numeric_limits<float>::lowest();
  float minY = minX, maxY = maxX;
  for (const DPoint& p : allPoints) {
    minX = std::fmin(minX, p.px); maxX = std::fmax(maxX, p.px);
    minY = std::fmin(minY, p.py); maxY = std::fmax(maxY, p.py);
  }
  float rangeX = maxX - minX, rangeY = maxY - minY;
  if (rangeX < 0.0001f) rangeX = 1.0f;
  if (rangeY < 0.0001f) rangeY = 1.0f;

  // Vertices (:315-341).
  out.verts.resize(allPoints.size());
  for (size_t i = 0; i < allPoints.size(); ++i) {
    const DPoint& p = allPoints[i];
    SwVertex& v = out.verts[i];
    std::memset(&v, 0, sizeof(SwVertex));
    v.Position = {p.px, p.py, p.pz};
    v.Normal = {0.0f, 0.0f, 1.0f};     // :334
    v.Tangent = {1.0f, 0.0f, 0.0f};    // :335
    v.Bitangent = {0.0f, 1.0f, 0.0f};  // :336
    v.Texcoord = {(p.px - minX) / rangeX, (p.py - minY) / rangeY};  // :327-329
    v.Texcoord2 = {0.0f, 0.0f};
    v.Selection = p.f1;                // :338
    v.ColorRgb = {p.cr, p.cg, p.cb};   // :324, :339
  }

  // Triangle filter + REVERSED winding (:343-377).
  for (const DelaunayTri& t : tris) {
    const DPoint& p0 = allPoints[(size_t)t.a];
    const DPoint& p1 = allPoints[(size_t)t.b];
    const DPoint& p2 = allPoints[(size_t)t.c];
    const DelaunayV2 centroid{(p0.px + p1.px + p2.px) / 3.0f, (p0.py + p1.py + p2.py) / 3.0f};  // :362-365
    if (!pointInPolygon(centroid, boundaryPolygon)) continue;  // :367-370
    out.faces.push_back({t.a, t.c, t.b});  // :375 Int3(idx0, idx2, idx1) — CW output
  }
}

void delaunayCount(const std::map<std::string, float>* params, const SwMeshView* /*inputs*/,
                   int /*inputCount*/, const std::vector<SwPoint>* const* pointLists,
                   int pointListCount, uint32_t& vtx, uint32_t& idx) {
  const std::vector<SwPoint>* boundary = pointListCount > 0 ? pointLists[0] : nullptr;
  const std::vector<SwPoint>* extra = pointListCount > 1 ? pointLists[1] : nullptr;
  delaunayCompute(params, boundary, extra);  // fork-delaunay-count-runs-cook (stash filled)
  vtx = (uint32_t)delaunayStash().verts.size();
  idx = (uint32_t)delaunayStash().faces.size();
}

void delaunayCook(MeshCookCtx& c) {
  if (!c.output_vertices || !c.output_indices) return;
  const DelaunayResult& r = delaunayStash();  // filled by the count that just sized these buffers
  SwVertex* verts = (SwVertex*)c.output_vertices->contents();
  SwTriIndex* idx = (SwTriIndex*)c.output_indices->contents();
  const size_t nv = std::min((size_t)c.vertexCount, r.verts.size());
  const size_t nf = std::min((size_t)c.indexCount, r.faces.size());
  if (nv) std::memcpy(verts, r.verts.data(), nv * sizeof(SwVertex));
  if (nf) std::memcpy(idx, r.faces.data(), nf * sizeof(SwTriIndex));

  // Test injection (golden RED): corrupt the first vertex position in the REAL output (family seam).
  if (meshInjectBug() && c.vertexCount > 0) verts[0].Position = {-999.0f, -999.0f, -999.0f};
}

NodeSpec delaunaySpec() {
  NodeSpec s;
  s.type = "DelaunayMesh";
  s.title = "Delaunay Mesh";
  PortSpec bp; bp.id = "BoundaryPoints"; bp.name = "BoundaryPoints"; bp.dataType = "PointList";
  bp.isInput = true;
  PortSpec ep; ep.id = "ExtraPoints"; ep.name = "ExtraPoints"; ep.dataType = "PointList";
  ep.isInput = true;
  PortSpec se; se.id = "SubdivideLongEdges"; se.name = "SubdivideLongEdges"; se.dataType = "Float";
  se.isInput = true; se.def = 0.0f; se.minV = 0.0f; se.maxV = 1.0f;
  PortSpec fd; fd.id = "FillDensity"; fd.name = "FillDensity"; fd.dataType = "Float";
  fd.isInput = true; fd.def = 0.0f; fd.minV = 0.0f; fd.maxV = 1.0f;
  PortSpec sd; sd.id = "Seed"; sd.name = "Seed"; sd.dataType = "Float";
  sd.isInput = true; sd.def = 0.0f; sd.minV = 0.0f; sd.maxV = 10000.0f;
  PortSpec tw; tw.id = "Tweak"; tw.name = "Tweak"; tw.dataType = "Float";
  tw.isInput = true; tw.def = 0.0f; tw.minV = 0.0f; tw.maxV = 10.0f;
  PortSpec out; out.id = "Data"; out.name = "Data"; out.dataType = "Mesh"; out.isInput = false;
  s.ports = {bp, ep, se, fd, sd, tw, out};
  return s;
}

const MeshOp g_delaunayMeshOp(delaunaySpec(), delaunayCount, delaunayCook);

}  // namespace
}  // namespace sw
