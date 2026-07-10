// runtime/gridplane_fill — encode one DrawKind::GridPlane RenderDrawItem (mirror of mesh_pbr_fill.h /
// layer2d_fill.h — the whole draw lives in ONE peeled function so point_ops_rendertarget.cpp's switch
// case stays a PSO-gate + one call, same shape as every other kind). TiXL GridPlane → draw-GridPlane.hlsl:
// an object-space quad, ObjectToClipSpace-projected, with a procedural grid pattern in the fragment shader
// (no texture, no point buffer). The executor has already gated hasGroup/hasRenderState are irrelevant
// here (GridPlane always draws — no unwired-input skip like ScreenQuad/Layer2d) and picked/built the PSO.
#pragma once

namespace MTL {
class RenderCommandEncoder;
class RenderPipelineState;
}  // namespace MTL

namespace sw {

struct RenderDrawItem;      // render_command.h
struct LayerCameraForward;  // field_camera.h

// Encode one DrawKind::GridPlane item: composes ObjectToWorld (groupObjectToWorld when hasGroup, else
// identity — the S·R stamp point_ops_gridplane.cpp bakes from GridPlane.t3's internal Transform child),
// inverts it for the HLSL VS's host-hoisted `scale` varying (draw-GridPlane.hlsl:57, see gridplane.metal's
// file header), builds ObjectToClipSpace via `cam`, fills DrawGridPlaneParams, and draws the 6-vert quad.
void encodeGridPlaneDraw(MTL::RenderCommandEncoder* enc, const RenderDrawItem& it, MTL::RenderPipelineState* pso,
                         const LayerCameraForward& cam);

}  // namespace sw
