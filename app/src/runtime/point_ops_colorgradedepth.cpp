// ColorGradeDepth image-filter texture op (lane image_filter, render C-bucket bespoke wave — the
// FIRST render-C-bucket leaf: an independent-algorithm node with no .t3-import registration). TiXL
// authority: external/tixl/Operators/Lib/image/color/ColorGradeDepth.cs (ports) + ColorGradeDepth.t3
// (defaults; the compound wraps a single PixelShaderStage draw + an embedded GradientsToTexture child
// that bakes the Gradient input to a sampleable row before the shader reads it) + Assets/shaders/img/
// adjust/ColorGradeWithDepth.hlsl (the kernel, ported verbatim in colorgradedepth.metal).
//
// Ports (verbatim .cs field order): Texture2d (Image), PreSaturate, Gain/Gamma/Lift (Vec4 grading
// triads), VignetteColor/VignetteRadius/VignetteFeather/VignetteCenter (vignette), DepthBuffer
// (SECOND Texture2D input — this is the SECOND op after Displace with two Texture2D ports),
// Gradient (the depth-tint LUT — reuses the SAME Gradient->texture rail-crossing seam
// GradientsToTexture/LinearGradient already proved: TexCookCtx::inputGradients, populated by the tex-
// cook driver for any "Gradient"-dataType port, STANDARD non-own-output branch — see
// point_graph_tex_cook.cpp:343-348 / point_graph_resident_tex_cook.cpp:358-362), GradientDepthRange,
// CamNearFarClip.x/.y -> NearClip/FarClip.
//
// Self-contained leaf: cookColorGradeDepth + ImageFilterOp self-registration (registerTexOp + spec +
// selftest sinks, zero shared-file edits — CMake's point_ops*.cpp / shaders/*.metal globs pick up this
// leaf and its .metal automatically). The math golden (two closed-form teeth) lives in the sibling
// point_ops_colorgradedepth_golden.cpp (400-line ratchet split) — cookColorGradeDepth and the
// injectBug accessors below are NOT in an anonymous namespace so that file can drive them directly.
#include "runtime/point_ops.h"

#include <cmath>
#include <map>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/colorgradedepth_params.h"    // ColorGradeDepthParams, COLORGRADEDEPTH_Params
#include "runtime/eval_context.h"
#include "runtime/gradient_raster.h"            // rasterizeGradientRow, kGradientRowN
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"                // TexCookCtx, cookParam
#include "runtime/sw_gradient.h"                // SwGradient (the consumed currency)
#include "runtime/tex_op_cache.h"              // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

int runColorGradeDepthSelfTest(bool injectBug);  // point_ops_colorgradedepth_golden.cpp

// Test-only injection seams (golden only — corrupt the REAL cook path, not the expected value).
// Meyers-singleton accessors (mirror gradientsToTextureInjectBug in point_ops_gradientstotexture.cpp)
// so the golden, in a SEPARATE translation unit, can flip them across the file boundary.
// dropGrading: neutralizes Gain (forces GainX/Y/Z back to 0.5, the TiXL-identity value) so an
// off-identity grading test collapses to a no-op multiply.
bool& colorGradeDepthDropGrading() { static bool b = false; return b; }
// dropGradient: rebinds a FLAT neutral-gray LUT regardless of the real wired gradient, so a
// depth-tint test's contribution vanishes.
bool& colorGradeDepthDropGradient() { static bool b = false; return b; }

// Unwired-Gradient fallback (ColorGradeDepth.t3 Inputs :48-86, the op's OWN Gradient slot default):
// a flat 3-stop gray gradient, R=G=B=0.5019608 A=0.5 at every stop. rgb==0.5 -> the shader's
// (gradientColor.rgb-0.5) term is exactly 0 regardless of A, so an unwired Gradient contributes
// nothing to gainScaled (matches TiXL's own neutral default, not a behavior fork).
SwGradient defaultColorGradeDepthGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  const simd::float4 kNeutral = simd::make_float4(0.5019608f, 0.5019608f, 0.5019608f, 0.5f);
  g.steps.push_back({0.0f, kNeutral});
  g.steps.push_back({0.47330096f, kNeutral});
  g.steps.push_back({1.0f, kNeutral});
  return g;
}

namespace {

// Fill ColorGradeDepthParams from the cook params. Defaults verbatim from ColorGradeDepth.t3 Inputs.
void fillParams(const TexCookCtx& c, ColorGradeDepthParams& p) {
  p.GainX = cookParam(c, "Gain.x", 0.5f);
  p.GainY = cookParam(c, "Gain.y", 0.5f);
  p.GainZ = cookParam(c, "Gain.z", 0.5f);
  p.GainW = cookParam(c, "Gain.w", 0.506f);
  if (colorGradeDepthDropGrading()) { p.GainX = p.GainY = p.GainZ = 0.5f; }

  p.GammaX = cookParam(c, "Gamma.x", 0.5f);
  p.GammaY = cookParam(c, "Gamma.y", 0.5f);
  p.GammaZ = cookParam(c, "Gamma.z", 0.5f);
  p.GammaW = cookParam(c, "Gamma.w", 0.506f);

  p.LiftX = cookParam(c, "Lift.x", 0.5f);
  p.LiftY = cookParam(c, "Lift.y", 0.5f);
  p.LiftZ = cookParam(c, "Lift.z", 0.5f);
  p.LiftW = cookParam(c, "Lift.w", 0.25f);

  p.VignetteColorX = cookParam(c, "VignetteColor.x", 0.49999997f);
  p.VignetteColorY = cookParam(c, "VignetteColor.y", 0.49999997f);
  p.VignetteColorZ = cookParam(c, "VignetteColor.z", 0.49999997f);
  p.VignetteColorW = cookParam(c, "VignetteColor.w", 0.0f);

  p.VignetteCenterX = cookParam(c, "VignetteCenter.x", 0.0f);
  p.VignetteCenterY = cookParam(c, "VignetteCenter.y", 0.0f);
  p.VignetteRadius  = cookParam(c, "VignetteRadius", 1.0f);
  p.VignetteBias    = cookParam(c, "VignetteFeather", 1.0f);

  p.GradientDepthRangeX = cookParam(c, "GradientDepthRange.x", 0.1f);
  p.GradientDepthRangeY = cookParam(c, "GradientDepthRange.y", 100.0f);
  p.NearClip = cookParam(c, "CamNearFarClip.x", 0.01f);
  p.FarClip  = cookParam(c, "CamNearFarClip.y", 1000.0f);

  p.PreSaturate = cookParam(c, "PreSaturate", 1.0f);
}

// Clear `out` to black (no Image input -> nothing to grade; mirrors cookDisplace's empty path).
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

// ColorGradeDepth texture op: read Image (inputTextures[0]) + DepthBuffer (inputTextures[1]) +
// Gradient (inputGradients[0], baked to a LUT row via the shared rasterizeGradientRow seam), one
// fullscreen pass into c.output. Not in the anonymous namespace: the golden (separate TU) calls this
// directly to exercise the SAME cook path production uses (registerTexOp binds this exact symbol).
void cookColorGradeDepth(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  const MTL::Texture* image = c.inputTextureCount > 0 ? c.inputTextures[0] : nullptr;
  if (!image) { clearTexture(c.queue, c.output); return; }  // no Image -> nothing to grade

  // DepthBuffer (SECOND Texture2D port): unwired -> fall back to a 1x1 depth=0 dummy (named fork: no
  // crash on a null bind; TiXL's own DepthBuffer default is null/undefined, so the substitute value
  // is a host-side "keep the pipe alive" choice, not a parity claim about an unwired-DepthBuffer
  // TiXL render — that state doesn't occur in a wired scene).
  const MTL::Texture* depthTexIn = c.inputTextureCount > 1 ? c.inputTextures[1] : nullptr;
  MTL::Texture* depthDummy = nullptr;
  if (!depthTexIn) {
    MTL::TextureDescriptor* td =
        MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
    td->setUsage(MTL::TextureUsageShaderRead);
    td->setStorageMode(MTL::StorageModeShared);
    depthDummy = c.dev->newTexture(td);
    uint8_t zero[4] = {0, 0, 0, 255};
    depthDummy->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, zero, 4);
    depthTexIn = depthDummy;
  }

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "colorgradedepth_vs", "colorgradedepth_fs", fmt);  // D2-2 reuse
  if (!rps) { if (depthDummy) depthDummy->release(); return; }

  // Single shared sampler (all three reads) — Linear+Clamp (see colorgradedepth.metal header FORK
  // note: TiXL's own SamplerState default is unverifiable in this vendor tree; inert for the golden).
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // Gradient -> LUT row (the Gradient rail-crossing, shared seam with LinearGradient/
  // GradientsToTexture — gradient_raster.h, can't drift). Unwired -> the op's own slot default.
  SwGradient gr = (c.inputGradients && !c.inputGradients->empty()) ? c.inputGradients->at(0)
                                                                   : defaultColorGradeDepthGradient();
  if (colorGradeDepthDropGradient()) gr = defaultColorGradeDepthGradient();  // test-only: neutral LUT
  MTL::Texture* gradTex = rasterizeGradientRow(c.dev, gr, kGradientRowN);
  if (!gradTex) {
    // Degenerate (no device) -> a 1x1 neutral texel so the shader still has something bound.
    MTL::TextureDescriptor* td =
        MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA32Float, 1, 1, false);
    td->setUsage(MTL::TextureUsageShaderRead);
    td->setStorageMode(MTL::StorageModeShared);
    gradTex = c.dev->newTexture(td);
    float neutral[4] = {0.5f, 0.5f, 0.5f, 0.5f};
    gradTex->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, neutral, 4 * sizeof(float));
  }

  ColorGradeDepthParams p{};
  fillParams(c, p);

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(image), 0);      // texture(0) = InputTexture
  enc->setFragmentTexture(const_cast<MTL::Texture*>(depthTexIn), 1);  // texture(1) = DepthBuffer
  enc->setFragmentTexture(gradTex, 2);                                // texture(2) = Gradient LUT
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(ColorGradeDepthParams), COLORGRADEDEPTH_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();
  gradTex->release();
  if (depthDummy) depthDummy->release();
  // rps is cache-owned (tex_op_cache), not released here.
}

// Self-registration. Ports mirror ColorGradeDepth.cs field order. FORKS (named): TiXL's
// Wrap/TextureFiltering/GenerateMips host plumbing on the underlying RenderTarget/PixelShaderStage
// machinery is omitted (fixed clamp+linear sampler, no mips — same fork class as Displace/Blur);
// an unwired DepthBuffer binds a 1x1 depth=0 dummy; an unwired Gradient uses the op's own TiXL slot
// default (flat neutral gray, zero-contribution — not a fork, matches ColorGradeDepth.t3's default).
static const ImageFilterOp _reg_colorgradedepth{
    {"ColorGradeDepth", "ColorGradeDepth",
     {{"Image", "Image", "Texture2D", true},
      {"DepthBuffer", "DepthBuffer", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      {"PreSaturate", "PreSaturate", "Float", true, 1.0f, 0.0f, 2.0f},
      {"Gain.x", "Gain", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Gain.y", "Gain.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"Gain.z", "Gain.z", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Gain.w", "Gain.w", "Float", true, 0.506f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Gamma.x", "Gamma", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Gamma.y", "Gamma.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"Gamma.z", "Gamma.z", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Gamma.w", "Gamma.w", "Float", true, 0.506f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Lift.x", "Lift", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Lift.y", "Lift.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"Lift.z", "Lift.z", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Lift.w", "Lift.w", "Float", true, 0.25f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"VignetteColor.x", "VignetteColor", "Float", true, 0.49999997f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"VignetteColor.y", "VignetteColor.y", "Float", true, 0.49999997f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"VignetteColor.z", "VignetteColor.z", "Float", true, 0.49999997f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"VignetteColor.w", "VignetteColor.w", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"VignetteRadius", "VignetteRadius", "Float", true, 1.0f, -2.0f, 2.0f},
      {"VignetteFeather", "VignetteFeather", "Float", true, 1.0f, 0.0f, 4.0f},
      {"VignetteCenter.x", "VignetteCenter", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"VignetteCenter.y", "VignetteCenter.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Gradient", "Gradient", "Gradient", true},
      {"GradientDepthRange.x", "GradientDepthRange", "Float", true, 0.1f, 0.0f, 1000.0f, Widget::Vec, {}, true, 2},
      {"GradientDepthRange.y", "GradientDepthRange.y", "Float", true, 100.0f, 0.0f, 1000.0f, Widget::Vec, {}, true, 1},
      {"CamNearFarClip.x", "CamNearFarClip", "Float", true, 0.01f, 0.0f, 100000.0f, Widget::Vec, {}, true, 2},
      {"CamNearFarClip.y", "CamNearFarClip.y", "Float", true, 1000.0f, 0.0f, 100000.0f, Widget::Vec, {}, true, 1},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "ColorGradeDepth", cookColorGradeDepth, "colorgradedepth", runColorGradeDepthSelfTest};

}  // namespace sw
