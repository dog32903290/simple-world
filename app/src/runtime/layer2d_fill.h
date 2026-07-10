// runtime/layer2d_fill — encode one DrawKind::Layer2d RenderDrawItem (peeled out of the point_ops_rendertarget.cpp
// executor case for the line-count ratchet, mirror of mesh_pbr_fill.cpp/encodeMeshPbrDraw). TiXL Layer2d →
// draw-Quad-vs.hlsl: a textured quad composed by ObjectToWorld (SRT or the legacy raw-matrix seam tooth) ×
// Group SRT, then projected by the item's camera. The executor has already gated (srcTexture non-null),
// picked/built the PSO, lazily built the shared quad `sampler` (reused with DrawScreenQuad — TiXL Layer2d
// Filter default = Linear+Clamp), and resolved the item's camera (itemCamera(it): stamped Camera/Ortho +
// ShiftCamera). Pure CPU matrix math + Metal encoder calls, runtime leaf.
#pragma once

namespace MTL {
class RenderCommandEncoder;
class RenderPipelineState;
class SamplerState;
}  // namespace MTL

namespace sw {

struct RenderDrawItem;       // render_command.h
struct LayerCameraForward;   // field_camera.h

// Encode one DrawKind::Layer2d item: composes ObjectToWorld (layer2dComposeSRT → S·R·T from the raw params,
// else the legacy objectToClipSpace[16] verbatim — the Cut-1 seam tooth), right-multiplies the Group SRT
// stamp (identity when !hasGroup → byte-identical), builds ObjectToClipSpace via `cam`, fills DrawQuadXfParams
// (color/position/width/height/clampMax/applyTransform), binds `sampler` + it.srcTexture, and draws the
// 6-vert quad.
void encodeLayer2dDraw(MTL::RenderCommandEncoder* enc, const RenderDrawItem& it, MTL::RenderPipelineState* pso,
                       MTL::SamplerState* sampler, const LayerCameraForward& cam);

}  // namespace sw
