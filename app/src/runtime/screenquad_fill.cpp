// runtime/screenquad_fill — see screenquad_fill.h for the peel rationale (ratchet split of
// point_ops_rendertarget.cpp, mirror of mesh_pbr_fill.cpp).
#include "runtime/screenquad_fill.h"

#include <Metal/Metal.hpp>

#include "runtime/draw_params.h"     // DrawScreenQuadParams + DRAWSQ_* bindings
#include "runtime/render_command.h"  // RenderDrawItem

namespace sw {

void encodeScreenQuadDraw(MTL::RenderCommandEncoder* enc, const RenderDrawItem& it, MTL::RenderPipelineState* pso,
                          MTL::SamplerState* sampler) {
  enc->setRenderPipelineState(pso);
  DrawScreenQuadParams P{};
  P.color[0] = it.color[0]; P.color[1] = it.color[1];
  P.color[2] = it.color[2]; P.color[3] = it.color[3];
  P.position[0] = it.position[0]; P.position[1] = it.position[1];
  P.width = it.width; P.height = it.height;
  // TiXL HDR clamp constant float4(1000,1000,1000,1): RGB headroom, alpha capped at 1. The item carries it
  // so a clamp golden can move the ceiling to exercise the shader.
  P.clampMax[0] = it.clampMax[0]; P.clampMax[1] = it.clampMax[1];
  P.clampMax[2] = it.clampMax[2]; P.clampMax[3] = it.clampMax[3];
  enc->setVertexBytes(&P, sizeof(P), DRAWSQ_Params);
  enc->setFragmentBytes(&P, sizeof(P), DRAWSQ_Params);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(it.srcTexture), 0);
  enc->setFragmentSamplerState(sampler, 0);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(6));
}

}  // namespace sw
