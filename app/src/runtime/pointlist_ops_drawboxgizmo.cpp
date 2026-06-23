// DrawBoxGizmo pointlist op (gizmo C3 Tranche 1 — params → StructuredList<Point> 12-edge wireframe box).
// TiXL authority: external/tixl/Operators/Lib/render/gizmo/DrawBoxGizmo.cs (GUID 9123651a) + DrawBoxGizmo.t3.
//
// HOME = the pointlist seam (same as ConeGizmo), a GENERATOR feeding ListToBuffer→DrawLines downstream.
// DrawBoxGizmo.t3 is the CLEANEST of the five: CommonPointSets(Set=2 = CubePoints) → Transform(Position/
// Stretch/Scale) → DrawLines(LineWidth=0.018). The geometry is a VERBATIM transcription of the 12 cube
// edges (gizmo_geometry.cpp emitBoxEdges, from CommonPointSets.CubePoints) — no fork in the geometry.
//
// .t3 defaults: Color=(1,0.638,0.144,0.555) (an orange), Stretch=(1,1,1), Scale=1, Position=(0,0,0). SW
// surfaces the Scale (a uniform size knob); Stretch/Position are downstream-transform concerns and Color
// is the DrawLines node's (geometry-only generator). Scale multiplies the unit-cube emit.
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/gizmo_geometry.h"          // emitBoxEdges
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListParam / pointListInjectBug
#include "runtime/tixl_point.h"              // SwPoint full def

namespace sw {

namespace {

void cookDrawBoxGizmo(PointListCookCtx& c) {
  if (!c.output) return;

  const std::map<std::string, float>* p = c.params;
  float scale = pointListParam(p, "Scale", 1.0f);  // DrawBoxGizmo.t3 Scale (uniform); .t3 default 1.

  emitBoxEdges(*c.output);  // unit cube [-0.5,0.5]^3 (12 edges, CommonPointSets.CubePoints order)
  if (scale != 1.0f)
    for (SwPoint& pt : *c.output) {  // apply the uniform Scale knob (separators have Position 0 → unchanged)
      pt.Position.x *= scale; pt.Position.y *= scale; pt.Position.z *= scale;
    }

  if (pointListInjectBug()) c.output->clear();
}

}  // namespace

static const PointListOp _reg_drawboxgizmo{
    {"DrawBoxGizmo", "DrawBoxGizmo",
     {{"Points", "Points", "PointList", false},
      {"Scale", "Scale", "Float", true, 1.0f, 0.0f, 100.0f}},
     /*evaluate=*/nullptr},
    cookDrawBoxGizmo};

}  // namespace sw
