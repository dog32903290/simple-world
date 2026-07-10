// runtime/screenquad_fill — encode one DrawKind::ScreenQuad RenderDrawItem (peeled out of the
// point_ops_rendertarget.cpp executor case for the line-count ratchet, mirror of mesh_pbr_fill.cpp). TiXL
// DrawScreenQuad: a textured CLIP-SPACE fullscreen quad (no ObjectToClipSpace — raw clip space, unlike
// Layer2d). The executor has already gated (srcTexture non-null), picked/built the PSO, and lazily built the
// shared quad `sampler` (reused with Layer2d — TiXL DrawScreenQuad's own SamplerState child, Filter default =
// Linear+Clamp). Pure CPU param-fill + Metal encoder calls, runtime leaf.
#pragma once

namespace MTL {
class RenderCommandEncoder;
class RenderPipelineState;
class SamplerState;
}  // namespace MTL

namespace sw {

struct RenderDrawItem;  // render_command.h

// Encode one DrawKind::ScreenQuad item: fills DrawScreenQuadParams (color/position/width/height/clampMax —
// the TiXL HDR clamp float4(1000,1000,1000,1), RGB headroom/alpha capped at 1), binds `sampler` +
// it.srcTexture, and draws the 6-vert clip-space quad.
void encodeScreenQuadDraw(MTL::RenderCommandEncoder* enc, const RenderDrawItem& it, MTL::RenderPipelineState* pso,
                          MTL::SamplerState* sampler);

}  // namespace sw
