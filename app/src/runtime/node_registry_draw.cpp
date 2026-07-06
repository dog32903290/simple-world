// runtime/node_registry_draw — driver that assembles the DRAW/RENDER NodeSpec table from per-family
// sub-tables. The ops themselves live in family files so the render/camera/flow/data lanes can each
// extend their own family WITHOUT touching this shared table (parallel-lane peel — no merge conflict).
// Split from node_registry.cpp (ARCHITECTURE rule 4); this file is now just the concatenation seam.
//
// Family sub-tables (ARCHITECTURE rule 7 — add a node = add one row to the matching family file):
//   node_registry_draw_render.cpp      — DrawPoints/DrawLines/…/DrawMeshUnlit/RenderTarget/SetRequestedResolution
//   node_registry_draw_camera.cpp      — Camera / OrthographicCamera / ShiftCamera
//   node_registry_draw_flow.cpp        — Execute/Loop/ExecuteOnce/ExecRepeatedly/LogMessage/SetXxxVarCmd
//   node_registry_draw_data.cpp        — data.* ops (lane hook — starts empty)
//   node_registry_draw_gizmo.cpp       — render.gizmo Command ops (VisibleGizmos — camera-B peel)
//   node_registry_draw_transform.cpp   — RotateAroundAxis/Shear/Transform/RotateTowards (earlier peel)
//   node_registry_draw_renderstate.cpp — Rasterizer/OutputMerger (earlier peel)
#include "runtime/node_registry_draw.h"
#include "runtime/graph.h"

namespace sw {

// Peeled NodeSpec leaves, appended below (declared in node_registry_draw.h): the four parallel-lane
// families + the two earlier render-island peels (transform-context ops + Seam 2 render-state).
const std::vector<NodeSpec>& drawTransformSpecs();
const std::vector<NodeSpec>& drawRenderStateSpecs();

const std::vector<NodeSpec>& drawSpecs() {
  static const std::vector<NodeSpec> specs = [] {
    std::vector<NodeSpec> base;
    auto append = [&](const std::vector<NodeSpec>& src) {
      base.insert(base.end(), src.begin(), src.end());
    };
    // The four parallel-lane families (each owns its own .cpp — the whole point of this peel).
    append(drawRenderSpecs());       // DrawPoints/DrawLines/…/RenderTarget/SetRequestedResolution
    append(drawCameraSpecs());       // Camera / OrthographicCamera
    append(drawFlowSpecs());         // Execute/Loop/SetXxxVarCmd/LogMessage/…
    append(drawDataSpecs());         // data.* ops (lane hook — starts empty)
    append(drawGizmoSpecs());        // render.gizmo Command ops (VisibleGizmos — camera-B peel)
    append(drawIoVideoSpecs());      // io.video/io.ptz Texture2D sources (PlayVideo/VideoDeviceInput/ViscaCamera/OnvifCamera)
    append(drawShadingSpecs());      // render.shading PBR ops (SetMaterial/…/DrawMeshPbr)
    // The two earlier render-island peels (transform-context ops + Seam 2 render-state).
    append(drawTransformSpecs());
    append(drawRenderStateSpecs());
    return base;
  }();
  return specs;
}

}  // namespace sw
