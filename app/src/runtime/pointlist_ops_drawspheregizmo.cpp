// DrawSphereGizmo pointlist op (gizmo C3 Tranche 1 — params → StructuredList<Point> wireframe sphere).
// TiXL authority: external/tixl/Operators/Lib/render/gizmo/DrawSphereGizmo.cs (GUID 1998f949) + .t3.
//
// HOME = the pointlist seam (same as ConeGizmo), a GENERATOR feeding ListToBuffer→DrawLines downstream.
// Geometry = gizmo_geometry.cpp emitSphereRings (lat/long rings); see its [fork-gizmo-sphere-composite]
// note — TiXL's RepeatAtPoints ring composite is composed as a standard wireframe ball instead.
//
// .t3 defaults: Radius=1, InnerRadius=0.25, Color=(1,1,1,1). SW surfaces Radius + ring density (Rings/
// Segments). InnerRadius drives a TiXL DrawPoints dot layer (dropped — geometry-only generator); Color is
// the downstream DrawLines node's. Radius scales the unit sphere.
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/gizmo_geometry.h"          // emitSphereRings
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListParam / pointListInjectBug
#include "runtime/tixl_point.h"              // SwPoint full def

namespace sw {

namespace {

void cookDrawSphereGizmo(PointListCookCtx& c) {
  if (!c.output) return;

  const std::map<std::string, float>* p = c.params;
  float radius = pointListParam(p, "Radius", 1.0f);
  int rings = (int)pointListParam(p, "Rings", 4.0f);       // lat + long circle count
  int segments = (int)pointListParam(p, "Segments", 24.0f);  // per-ring segment count (smooth circle)

  emitSphereRings(*c.output, rings, segments);  // unit sphere
  if (radius != 1.0f)
    for (SwPoint& pt : *c.output) {  // scale to Radius (separators have Position 0 → unchanged)
      pt.Position.x *= radius; pt.Position.y *= radius; pt.Position.z *= radius;
    }

  if (pointListInjectBug()) c.output->clear();
}

}  // namespace

static const PointListOp _reg_drawspheregizmo{
    {"DrawSphereGizmo", "DrawSphereGizmo",
     {{"Points", "Points", "PointList", false},
      {"Radius", "Radius", "Float", true, 1.0f, 0.0f, 100.0f},
      {"Rings", "Rings", "Float", true, 4.0f, 1.0f, 64.0f},
      {"Segments", "Segments", "Float", true, 24.0f, 3.0f, 256.0f}},
     /*evaluate=*/nullptr},
    cookDrawSphereGizmo};

}  // namespace sw
