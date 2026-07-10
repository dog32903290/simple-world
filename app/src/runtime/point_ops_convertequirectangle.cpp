// ConvertEquirectangle image-filter texture op (lane image_filter, render C-bucket bespoke wave —
// screen-quad postfx, same ImageFilterOp self-registration shape as ColorGradeDepth). TiXL
// authority: external/tixl/Operators/Lib/render/utils/ConvertEquirectangle.cs (Image/Resolution
// ports) + ConvertEquirectangle.t3 (defaults; the compound wraps a single PixelShaderStage draw,
// no embedded gradient/depth children) + Assets/shaders/img/fx/ConvertEquirectangle.hlsl (the
// kernel, ported verbatim into convertequirectangle.metal).
//
// Ports (verbatim .cs field order): Texture2d (Image), Resolution (Int2 — sizes the output via the
// SAME "Resolution"/CustomW/CustomH host convention every other image-filter op in this engine
// uses; TiXL's own Int2 port type is a named fork, see registrar comment). ZERO real shader
// constants (see convertequirectangle_params.h header for why FaceCount exists as a test-only seam).
//
// Self-contained leaf: cookConvertEquirectangle + ImageFilterOp self-registration (registerTexOp +
// spec + selftest sinks, zero shared-file edits). The math golden lives in the sibling
// point_ops_convertequirectangle_golden.cpp (400-line ratchet split) — cookConvertEquirectangle and
// the injectBug accessor below are NOT in an anonymous namespace so that file can drive them directly.
#include "runtime/point_ops.h"

#include <cmath>
#include <map>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/convertequirectangle_params.h"  // ConvertEquirectangleParams, CONVERTEQUIRECTANGLE_Params
#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"                // TexCookCtx
#include "runtime/tex_op_cache.h"              // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

int runConvertEquirectangleSelfTest(bool injectBug);  // point_ops_convertequirectangle_golden.cpp

// Test-only injection seam (golden only — corrupts the REAL cook path's shader constant, not the
// expected value). Meyers-singleton accessor (mirrors colorGradeDepthDropGrading() in
// point_ops_colorgradedepth.cpp) so the golden, in a SEPARATE translation unit, can flip it across
// the file boundary. When true, the cook function feeds FaceCount=3.0 instead of the real 6.0 —
// every one of the 6 face branches still fires on the SAME direction test, but the sampled sub-
// region of the source cross-image is wrong (wrong face column), so a probe pinned inside one
// face's solid-color column reads a DIFFERENT face's color.
bool& convertEquirectangleBreakFaceMap() { static bool b = false; return b; }

namespace {

// Clear `out` to black (no Image input -> nothing to project; mirrors cookColorGradeDepth's empty path).
void clearTexture(MTL::CommandQueue* q, MTL::Texture* out) {
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(out);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = q->commandBuffer();
  cmd->renderCommandEncoder(pass)->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

}  // namespace

// ConvertEquirectangle texture op: read Image (inputTextures[0] / inputTexture), one fullscreen
// pass into c.output. Not in the anonymous namespace: the golden (separate TU) calls this directly
// to exercise the SAME cook path production uses (registerTexOp binds this exact symbol).
void cookConvertEquirectangle(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  const MTL::Texture* image = c.inputTextureCount > 0 ? c.inputTextures[0] : c.inputTexture;
  if (!image) { clearTexture(c.queue, c.output); return; }  // no Image -> nothing to project

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "convertequirectangle_vs", "convertequirectangle_fs", fmt);
  if (!rps) return;

  // Single shared sampler (TiXL's own SamplerState default is Clamp/Clamp/Clamp, ConvertEquirectangle
  // .t3 :182-201 — same fork class as ColorGradeDepth's filter-mode assumption).
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  ConvertEquirectangleParams p{};
  p.FaceCount = convertEquirectangleBreakFaceMap() ? 3.0f : 6.0f;  // production always 6.0

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(image), 0);  // texture(0) = Image
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(ConvertEquirectangleParams), CONVERTEQUIRECTANGLE_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();
  // rps is cache-owned (tex_op_cache), not released here.
}

// Self-registration. Ports mirror ConvertEquirectangle.cs field order. FORKS (named): TiXL's Int2
// "Resolution" port type is normalized to this engine's standard "Resolution" Widget::Enum +
// CustomW/CustomH host convention (the SAME shape every other image-filter op's output-sizing knob
// uses, resolveRenderResolution in point_ops_rendertarget.cpp) — not a parity claim about the Int2
// widget itself, just the established host-side resolution-pin plumbing.
static const ImageFilterOp _reg_convertequirectangle{
    {"ConvertEquirectangle", "ConvertEquirectangle",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "ConvertEquirectangle", cookConvertEquirectangle, "convertequirectangle",
    runConvertEquirectangleSelfTest};

}  // namespace sw
