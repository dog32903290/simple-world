// Locator pointlist op (gizmo C3 Tranche 1 — params → StructuredList<Point> 3-axis cross marker).
// TiXL authority: external/tixl/Operators/Lib/render/gizmo/Locator.cs (GUID 348652c3) + Locator.t3.
//
// HOME = the pointlist seam (same as ConeGizmo), a GENERATOR feeding ListToBuffer→DrawLines downstream.
// Geometry = gizmo_geometry.cpp emitAxisCross (a 3-axis cross, CommonPointSets.CrossPoints verbatim).
//
// ★NAMED FORKS (see gizmo_geometry.h emitAxisCross [fork-gizmo-screen-constant]):
//   - LABEL DROPPED: Locator renders a bitmapfont Label ("Locator", Roboto-Black.fnt) — needs the
//     bitmapfont seam (CAMERA3D_BLUEPRINT downstream-gated). Geometry only in v1.
//   - SCREEN-CONSTANT: Locator outputs DistanceToCamera (Locator.cs:13, the fixed-pixel-size tell), but
//     its point GEOMETRY is world-space (no distance-divide in the cross), so v1 sizing is world-space,
//     NOT fixed-pixel-on-screen (out of C3 scope, §6). Named, not silent.
//   - ITransformable/TransformCallback (the interactive drag handle) is an editor-UI concern, dropped
//     (blueprint §6 — C3 is the DRAW family only; same drop as Transform's TransformCallback).
//
// .t3 defaults: Size=0.5, Thickness=0.015, Color=(0,0.773,0.682,1), Position=(0,0,0). SW surfaces Size
// (scales the unit cross). Thickness is the DrawLines LineWidth (downstream); Color/Position dropped/
// downstream.
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/gizmo_geometry.h"          // emitAxisCross
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListParam / pointListInjectBug
#include "runtime/tixl_point.h"              // SwPoint full def

namespace sw {

namespace {

void cookLocator(PointListCookCtx& c) {
  if (!c.output) return;

  const std::map<std::string, float>* p = c.params;
  float size = pointListParam(p, "Size", 0.5f);  // Locator.t3 Size; scales the unit [-0.5,0.5] cross.

  emitAxisCross(*c.output);  // unit 3-axis cross (±0.5 on X/Y/Z)
  // The emit spans ±0.5 (a unit cross of total length 1). Locator.Size is the cross HALF-extent in TiXL
  // (Size=0.5 default → the cross reaches ±0.5), so multiply by 2*size to map the unit ±0.5 → ±size.
  float k = 2.0f * size;
  if (k != 1.0f)
    for (SwPoint& pt : *c.output) {
      pt.Position.x *= k; pt.Position.y *= k; pt.Position.z *= k;
    }

  if (pointListInjectBug()) c.output->clear();
}

}  // namespace

static const PointListOp _reg_locator{
    {"Locator", "Locator",
     {{"Points", "Points", "PointList", false},
      {"Size", "Size", "Float", true, 0.5f, 0.0f, 100.0f}},
     /*evaluate=*/nullptr},
    cookLocator};

}  // namespace sw
