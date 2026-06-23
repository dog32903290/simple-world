// DrawLineGrid pointlist op (gizmo C3 Tranche 1 — params → StructuredList<Point> wireframe grid plane).
// TiXL authority: external/tixl/Operators/Lib/render/gizmo/DrawLineGrid.cs (GUID 296dddbd) + DrawLineGrid.t3.
//
// HOME = the pointlist seam (the 7th cook flow, pointlist_op_registry.h), NOT the draw-command register —
// the SAME home as ConeGizmo (the Tranche-0 precedent). In TiXL each gizmo .t3 wires its generated point
// geometry into a DrawLines child (so the gizmo's VISIBLE output IS a wireframe line set); SW has no .t3
// inliner (blueprint §0 fact 3), so the gizmo is ported as a GENERATOR (Output = StructuredList<Point>)
// that feeds the existing ListToBuffer→DrawLines→RenderTarget GPU path downstream — pure producer, no
// PointList input, no camera, zero touch to point_ops_register_draw.cpp / node_registry_draw.cpp. Geometry
// lives in gizmo_geometry.cpp (emitGridLines), see its [fork-gizmo-grid-composite] note.
//
// .t3 defaults (DrawLineGrid.t3): UniformScale=1, Segments=(0,0)→ clamped to a usable grid, Orientation=0.
// SW v1 surfaces SegmentsX/SegmentsY (the Int2 split into two Float ports, the value-rail convention) +
// UniformScale + Orientation. Color/LineWidth/BlendMod/ShowAxis are DROPPED (geometry-only generator; the
// downstream DrawLines node owns Color/LineWidth) — named in gizmo_geometry.h's grid fork.
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/gizmo_geometry.h"          // emitGridLines
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListParam / pointListInjectBug
#include "runtime/tixl_point.h"              // SwPoint full def

namespace sw {

namespace {

void cookDrawLineGrid(PointListCookCtx& c) {
  if (!c.output) return;

  const std::map<std::string, float>* p = c.params;
  // Segments is an Int2 in TiXL; the param spine is float → read + truncate. .t3 default (0,0) is a
  // degenerate grid → emitGridLines clamps to >=1, but surface a friendlier default (8×8) so a fresh drop
  // shows a real grid (the .t3 0,0 is a TiXL authoring placeholder, not a usable size).
  int segsX = (int)pointListParam(p, "SegmentsX", 8.0f);
  int segsY = (int)pointListParam(p, "SegmentsY", 8.0f);
  int orientation = (int)pointListParam(p, "Orientation", 0.0f);

  emitGridLines(*c.output, segsX, segsY, orientation);

  // Test-only: corrupt the REAL output on the actual cook path (CLEAR → empty list) so the golden's RED
  // bites here, not by flipping the expected value (the conegizmo/pointlist golden convention).
  if (pointListInjectBug()) c.output->clear();
}

}  // namespace

// Self-registration. PURE PRODUCER (generator): no PointList input; one PointList output "Points".
static const PointListOp _reg_drawlinegrid{
    {"DrawLineGrid", "DrawLineGrid",
     {{"Points", "Points", "PointList", false},
      {"SegmentsX", "SegmentsX", "Float", true, 8.0f, 1.0f, 256.0f},
      {"SegmentsY", "SegmentsY", "Float", true, 8.0f, 1.0f, 256.0f},
      {"Orientation", "Orientation", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
       {"XY", "XZ", "YZ"}, true}},
     /*evaluate=*/nullptr},  // PointList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookDrawLineGrid};

}  // namespace sw
