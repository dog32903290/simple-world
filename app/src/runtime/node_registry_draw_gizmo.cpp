// runtime/node_registry_draw_gizmo — NodeSpec rows for the GIZMO family (TiXL island Lib/render/gizmo):
// the Command-flow gizmo ops. New per-family peel (same pattern as node_registry_draw_camera.cpp):
// drawSpecs() appends this table; adding a gizmo Command op = add ONE row here, no shared-file edit.
//
// ISLAND JUDGMENT (camera-B lane): the gizmo GENERATOR atoms (ConeGizmo / DrawBoxGizmo /
// DrawSphereGizmo, also Lib/render/gizmo in TiXL) live on the pointlist seam (pointlist_ops_*.cpp) —
// they emit point geometry, not Commands. VisibleGizmos is a COMMAND-flow gate (MultiInput Commands in,
// Command out), so it belongs to the draw island; it gets its TiXL island's own family file rather than
// squatting in flow/ (its .cs namespace is Lib.render.gizmo, not Lib.flow).
#include "runtime/node_registry_draw.h"
#include "runtime/graph.h"

namespace sw {

const std::vector<NodeSpec>& drawGizmoSpecs() {
  static const std::vector<NodeSpec> specs = {
      // VisibleGizmos (TiXL Lib.render.gizmo.VisibleGizmos): a VISIBILITY GATE over a MultiInput Commands
      // subtree (VisibleGizmos.cs:50-51: visibility != On && !showIfSelected → the subtree does not render).
      // Visibility stores the RAW TiXL GizmoVisibility int (Inherit=-1/Off=0/On=1/IfSelected=2,
      // EvaluationContext.cs:16-22; .t3 default "Inherit" = -1 — Widget::Enum labels can't carry the -1
      // base, so the knob is a slider over the enum ints). FORKS (named, point_ops_visiblegizmos.h):
      // Inherit → Off (fresh-context ShowGizmos default; no output-window gizmo toggle in sw yet);
      // IfSelected → hidden (editor selection does not reach the cook rail).
      {"VisibleGizmos", "VisibleGizmos",
       {{"Commands", "Commands", "Command", true, 0.0f, 0.0f, 1.0f, Widget::Slider, {}, false, 1, true},
        {"out", "out", "Command", false},
        {"Visibility", "Visibility", "Float", true, -1.0f, -1.0f, 2.0f}},
       nullptr,
       "render.gizmo"},
      // GridPlane (TiXL Lib.render.gizmo.GridPlane, Guid 935e6597): a Command SOURCE (no input pins) —
      // a procedural-grid quad, hand-crafted DrawKind::GridPlane (point_ops_gridplane.cpp). Rotation
      // default X=90 tips the quad flat (a ground-reference gizmo); Size/Scale/Color are shader params.
      {"GridPlane", "GridPlane",
       {{"out", "out", "Command", false},
        {"Color.x", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
        {"Color.y", "Color.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.z", "Color.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Color.w", "Color.w", "Float", true, 0.25f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
        {"Size", "Size", "Float", true, 10.0f, 0.0f, 1000.0f},
        {"Scale", "Scale", "Float", true, 1.0f, 0.001f, 100.0f},
        {"Rotation.x", "Rotation", "Float", true, 90.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 3},
        {"Rotation.y", "Rotation.y", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1},
        {"Rotation.z", "Rotation.z", "Float", true, 0.0f, -360.0f, 360.0f, Widget::Vec, {}, true, 1}},
       nullptr,
       "render.gizmo"},
  };
  return specs;
}

}  // namespace sw
