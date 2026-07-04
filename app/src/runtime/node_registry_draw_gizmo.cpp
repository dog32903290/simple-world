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
  };
  return specs;
}

}  // namespace sw
