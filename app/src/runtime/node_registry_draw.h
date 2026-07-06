// runtime/node_registry_draw — NodeSpec sub-table for DRAW/RENDER ops.
// Split from node_registry.cpp (批次16-R).
#pragma once
#include "runtime/graph.h"
#include <vector>

namespace sw {
const std::vector<NodeSpec>& drawSpecs();

// Per-family draw sub-tables (parallel-lane peel: each family owns its own .cpp so the four
// render/camera/flow/data lanes never touch drawSpecs()'s shared table = no merge conflict).
// drawSpecs() appends these in source order (table order unchanged). Adding a node in a family =
// add ONE row to that family's file; no shared-file edit. Mirror of drawTransformSpecs /
// drawRenderStateSpecs (the earlier peels).
const std::vector<NodeSpec>& drawRenderSpecs();  // DrawPoints/DrawLines/…/RenderTarget/SetRequestedResolution
const std::vector<NodeSpec>& drawCameraSpecs();  // Camera / OrthographicCamera / ShiftCamera
const std::vector<NodeSpec>& drawFlowSpecs();    // Execute/Loop/SetXxxVarCmd/LogMessage/…
const std::vector<NodeSpec>& drawDataSpecs();    // data.* ops (lane hook — starts empty)
const std::vector<NodeSpec>& drawGizmoSpecs();   // render.gizmo Command ops (VisibleGizmos)
const std::vector<NodeSpec>& drawShadingSpecs(); // render.shading PBR ops (SetMaterial/…/DrawMeshPbr)
}  // namespace sw
