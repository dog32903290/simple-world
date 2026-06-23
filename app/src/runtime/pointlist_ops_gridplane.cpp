// GridPlane pointlist op (gizmo C3 Tranche 1 — params → StructuredList<Point> wireframe grid plane).
// TiXL authority: external/tixl/Operators/Lib/render/gizmo/GridPlane.cs (GUID 935e6597) + GridPlane.t3.
//
// HOME = the pointlist seam (same as DrawLineGrid/ConeGizmo), a GENERATOR feeding ListToBuffer→DrawLines.
//
// ★NAMED FORK [fork-gizmo-gridplane-shader]: TiXL GridPlane.t3 is NOT a DrawLines wireframe gizmo — it is
// a custom-HLSL pass: a transformed full quad (Draw VertexCount=6) shaded by Lib:shaders/dx11/
// draw-GridPlane.hlsl, which computes the grid lines PROCEDURALLY in the pixel shader (a fragment-grid,
// anti-aliased, fading). Porting that faithfully needs the inline-HLSL-codegen / custom-shader seam
// (out of C3 clean-leaf scope, the shader-graph island the census defers). SW v1 renders GridPlane as a
// DrawLines WIREFRAME grid (emitGridLines) instead — the VISIBLE intent (a grid plane in 3D) is the same;
// the procedural fragment shading (AA, distance fade) is NOT reproduced. This is the blueprint §6
// "if the .t3 is a shader pass, that op is a shader-seam fork" case, flagged loudly here.
//
// .t3 defaults (GridPlane.t3): Size=10, Scale=1, Rotation=(90,0,0) (the quad lies flat in XZ via the
// 90° X rotation). SW surfaces Segments (the grid density — TiXL's is baked in the shader; SW's is the
// emitGridLines cell count) + Orientation. Color/Size/Scale/Rotation handled downstream / dropped (named).
#include "runtime/graph.h"                   // NodeSpec, PortSpec, Widget
#include "runtime/gizmo_geometry.h"          // emitGridLines
#include "runtime/pointlist_op_registry.h"   // PointListOp / PointListCookCtx / pointListParam / pointListInjectBug
#include "runtime/tixl_point.h"              // SwPoint full def

namespace sw {

namespace {

void cookGridPlane(PointListCookCtx& c) {
  if (!c.output) return;

  const std::map<std::string, float>* p = c.params;
  // GridPlane's TiXL grid density lives in the fragment shader (not an op input); SW exposes it as a
  // Segments count. Default to a 10-cell grid (matching the .t3 Size=10 "10 units across" feel).
  int segments = (int)pointListParam(p, "Segments", 10.0f);
  int orientation = (int)pointListParam(p, "Orientation", 1.0f);  // 1=XZ (the .t3 90°X rotation lays it flat)

  emitGridLines(*c.output, segments, segments, orientation);

  if (pointListInjectBug()) c.output->clear();
}

}  // namespace

static const PointListOp _reg_gridplane{
    {"GridPlane", "GridPlane",
     {{"Points", "Points", "PointList", false},
      {"Segments", "Segments", "Float", true, 10.0f, 1.0f, 256.0f},
      {"Orientation", "Orientation", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Enum,
       {"XY", "XZ", "YZ"}, true}},
     /*evaluate=*/nullptr},
    cookGridPlane};

}  // namespace sw
