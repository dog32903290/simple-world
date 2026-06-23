// ConeGizmo pointlist op (gizmo seam Tranche 0 — params -> StructuredList<Point> wireframe audio cone).
// TiXL authority: external/tixl/Operators/Lib/render/gizmo/ConeGizmo.cs:23-129 (real C# Update; the only
// gizmo that carries draw logic in C# instead of a .t3 sub-graph). Geometry lives in gizmo_geometry.cpp
// (emitConeLines), transcribed verbatim from ConeGizmo.cs:30-107; this leaf is just the op shell + wiring.
//
// HOME = the pointlist seam (the 7th cook flow, pointlist_op_registry.h), NOT the draw-command register:
// ConeGizmo's TiXL Output is Slot<StructuredList> = StructuredList<Point> (ConeGizmo.cs:16), a host point
// list — it is a GENERATOR that feeds DrawLines downstream, NOT a Command. So it registers as a PointListOp
// exactly like LinePointsCpu/RadialPointsCpu (pure producer, no PointList input, no camera, zero touch to
// point_ops_register_draw.cpp / node_registry_draw.cpp). This is why C3 Tranche 0 has zero register-draw
// collision risk (blueprint §4): the first gizmo consumer rides the existing pointlist generator seam.
//
// .t3 defaults (ConeGizmo.t3): Angle=90, Length=2, Segments=24, RayCount=4 — mirrored below so a fresh
// drop = a 90° cone, 24-segment base circle + 4 apex rays, in the z=-2 plane.
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/gizmo_geometry.h"          // emitConeLines (the transcribed ConeGizmo.cs geometry)
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListParam / pointListInjectBug
#include "runtime/tixl_point.h"              // SwPoint full def

namespace sw {

namespace {

void cookConeGizmo(PointListCookCtx& c) {
  if (!c.output) return;

  const std::map<std::string, float>* p = c.params;
  float angle = pointListParam(p, "Angle", 90.0f);
  float length = pointListParam(p, "Length", 2.0f);
  // Segments/RayCount are int slots in TiXL; the pointlist param spine is float → read + truncate toward
  // zero (matches an int-cast of the resolved slider). emitConeLines re-applies TiXL's Math.Max clamps.
  int segments = (int)pointListParam(p, "Segments", 24.0f);
  int rayCount = (int)pointListParam(p, "RayCount", 4.0f);

  emitConeLines(*c.output, angle, length, segments, rayCount);

  // Test-only: corrupt the REAL output on the actual cook path so the golden's RED bites here (NOT by
  // flipping the expected value). CLEAR the whole list → empty list (transport readback ≠ expected).
  if (pointListInjectBug()) c.output->clear();
}

}  // namespace

// Self-registration. PURE PRODUCER (generator): no PointList input; one PointList output "Points". Params
// are the four ConeGizmo scalars. NodeSpec append into pointListSpecSink via PointListOp (like LinePointsCpu).
static const PointListOp _reg_conegizmo{
    {"ConeGizmo", "ConeGizmo",
     {{"Points", "Points", "PointList", false},
      {"Angle", "Angle", "Float", true, 90.0f, 0.0f, 360.0f},
      {"Length", "Length", "Float", true, 2.0f, 0.0f, 100.0f},
      // Segments/RayCount are integer counts; the param spine is float, edited as a slider (no Int
      // widget exists — Widget = Slider/Enum/Bool/Vec). The cook truncates the resolved float to int.
      {"Segments", "Segments", "Float", true, 24.0f, 3.0f, 256.0f},
      {"RayCount", "RayCount", "Float", true, 4.0f, 0.0f, 256.0f}},
     /*evaluate=*/nullptr},  // PointList output cannot ride NodeSpec::evaluate (returns ONE float)
    cookConeGizmo};

}  // namespace sw
