// Steps image-filter texture op (lane image_filter, render C-bucket bespoke wave — screen-quad
// postfx, same ImageFilterOp self-registration shape as ColorGradeDepth). TiXL authority: external/
// tixl/Operators/Lib/image/fx/stylize/Steps.cs (ports) + Steps.t3 (defaults; the compound wraps a
// single PixelShaderStage draw + an embedded GradientsToTexture child whose ONE "Gradients"
// MultiInput slot has BOTH the "Ramp" and "Edge" .t3 inputs wired to it — see below) + Assets/
// shaders/img/fx/Steps.hlsl (the kernel, ported verbatim into steps.metal).
//
// Ports (verbatim .cs field order): Texture2d (Image), Count, Bias, Offset, Highlight (Vec4),
// HighlightIndex, Ramp (Gradient), Edge (Gradient), SmoothRadius (dead — see steps_params.h),
// UseSuperSampling (bool), Repeat (bool), Resolution (Int2 — the standard Resolution/CustomW/CustomH
// host convention, see point_ops_convertequirectangle.cpp registrar comment for the same named fork).
//
// ★TWO-GRADIENT-PORT NAMED FORK: Steps.t3 wires BOTH "Ramp" and "Edge" into the SAME embedded
// GradientsToTexture child's single "Gradients" MultiInput slot, producing ONE 2-row texture (row0=
// Ramp LUT, row1=Edge LUT — the shader samples row 0.5/2 for the main color, row 1.5/2 for the edge
// color). Our engine's Gradient-port gather (point_graph_tex_cook.cpp's "Gradient" dataType branch)
// pushes into TexCookCtx::inputGradients ONLY for WIRED ports, in spec port order — so with TWO
// separate FIXED (non-MultiInput) Gradient ports (Ramp then Edge), inputGradients->size()==2 means
// [0]=Ramp,[1]=Edge UNAMBIGUOUSLY (both wired — the intended/common case, and the ONLY case this
// golden exercises); inputGradients->size()==1 is a GENUINE POSITIONAL AMBIGUITY the shared gather
// code cannot resolve (it cannot tell "only Ramp wired" from "only Edge wired") — outside this
// leaf's file domain to fix (point_graph_tex_cook.cpp is shared driver code). We resolve that
// ambiguous case by treating a lone gathered gradient as Ramp (the primary/likely-solo-wired port)
// and falling Edge back to its own .t3 default (a mostly-transparent accent gradient, so an
// accidentally-defaulted Edge stays visually close to a no-op). Zero-wired (size==0) uses both
// .t3 defaults. This is a named, documented limitation — not a silent bug.
#include "runtime/point_ops.h"

#include <cmath>
#include <map>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/gradient_raster.h"            // rasterizeGradientRow, kGradientRowN
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"                // TexCookCtx, cookParam
#include "runtime/steps_params.h"               // StepsParams, STEPS_Params
#include "runtime/sw_gradient.h"                // SwGradient (the consumed currency)
#include "runtime/tex_op_cache.h"              // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

int runStepsSelfTest(bool injectBug);  // point_ops_steps_golden.cpp

// Test-only injection seams (golden only — corrupt the REAL cook path, not the expected value).
// Meyers-singleton accessors (mirror colorGradeDepthDropGrading()/colorGradeDepthDropGradient() in
// point_ops_colorgradedepth.cpp) so the golden, in a SEPARATE translation unit, can flip them.
// dropHighlight: forces Highlight.a to 0 in the real params fill, collapsing the isHighlight blend
// to a no-op (mainRampColor == rampColor regardless of isHighlight) — CASE A's tooth.
bool& stepsDropHighlight() { static bool b = false; return b; }
// dropEdge: rebinds a FULLY-TRANSPARENT flat LUT for the Edge texture regardless of the real wired
// Edge gradient, so a nonzero-alpha edge-composite test's contribution vanishes — CASE B's tooth.
bool& stepsDropEdge() { static bool b = false; return b; }

// Ramp/Edge unwired-port fallbacks (Steps.t3 Inputs :20-99, each port's OWN default gradient).
SwGradient defaultStepsRampGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(9.9999e-7f, 9.9999e-7f, 1e-6f, 1.0f)});
  g.steps.push_back({1.0f, simd::make_float4(1.0f, 0.99999f, 1.0f, 1.0f)});
  return g;
}
SwGradient defaultStepsEdgeGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.8745387f, simd::make_float4(0.12546128f, 0.12546003f, 0.12546216f, 0.0f)});
  g.steps.push_back({1.0f, simd::make_float4(0.0f, 1.168251e-11f, 1e-6f, 0.16981131f)});
  return g;
}
// The injectBug-only neutral (fully transparent, zero-contribution) LUT source — distinct from the
// unwired-port .t3 default above (which is near-transparent but not EXACTLY zero-alpha at stop0).
SwGradient neutralZeroAlphaGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  const simd::float4 z = simd::make_float4(0.0f, 0.0f, 0.0f, 0.0f);
  g.steps.push_back({0.0f, z});
  g.steps.push_back({1.0f, z});
  return g;
}

namespace {

void fillParams(const TexCookCtx& c, StepsParams& p) {
  p.StepCount = cookParam(c, "Count", 4.0f);
  p.Bias = cookParam(c, "Bias", 0.5f);
  p.Offset = cookParam(c, "Offset", 0.0f);
  p.HighlightIndex = cookParam(c, "HighlightIndex", 0.0f);

  p.HighlightX = cookParam(c, "Highlight.x", 1.0f);
  p.HighlightY = cookParam(c, "Highlight.y", 1.0f);
  p.HighlightZ = cookParam(c, "Highlight.z", 1.0f);
  p.HighlightW = cookParam(c, "Highlight.w", 0.0f);
  if (stepsDropHighlight()) p.HighlightW = 0.0f;  // test-only: neutralize the highlight blend

  p.Repeat = cookParam(c, "Repeat", 1.0f);
  p.UseSuperSampling = cookParam(c, "UseSuperSampling", 1.0f);
}

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

// Steps texture op: read Image (inputTextures[0]) + Ramp/Edge (inputGradients, baked to two 1-row
// LUTs), one fullscreen pass into c.output. Not in the anonymous namespace: the golden (separate TU)
// calls this directly to exercise the SAME cook path production uses.
void cookSteps(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  const MTL::Texture* image = c.inputTextureCount > 0 ? c.inputTextures[0] : c.inputTexture;
  if (!image) { clearTexture(c.queue, c.output); return; }

  MTL::RenderPipelineState* rps = cachedTexPSO(c.dev, c.lib, "steps_vs", "steps_fs", fmt);
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // Ramp/Edge -> two 1-row LUTs (the SPLIT of Steps.t3's single 2-row RampImageA — see steps.metal
  // header). Two-gradient-port gather: see the registrar's ★TWO-GRADIENT-PORT NAMED FORK note.
  SwGradient rampG = defaultStepsRampGradient();
  SwGradient edgeG = defaultStepsEdgeGradient();
  if (c.inputGradients) {
    const size_t n = c.inputGradients->size();
    if (n >= 1) rampG = c.inputGradients->at(0);
    if (n >= 2) edgeG = c.inputGradients->at(1);
  }
  if (stepsDropEdge()) edgeG = neutralZeroAlphaGradient();  // test-only: zero the edge contribution

  MTL::Texture* rampTex = rasterizeGradientRow(c.dev, rampG, kGradientRowN);
  MTL::Texture* edgeTex = rasterizeGradientRow(c.dev, edgeG, kGradientRowN);
  if (!rampTex || !edgeTex) {
    if (rampTex) rampTex->release();
    if (edgeTex) edgeTex->release();
    samp->release();
    return;
  }

  StepsParams p{};
  fillParams(c, p);
  p.TargetWidth = (float)c.output->width();
  p.TargetHeight = (float)c.output->height();

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(image), 0);  // texture(0) = ImageA
  enc->setFragmentTexture(rampTex, 1);                            // texture(1) = RampTex
  enc->setFragmentTexture(edgeTex, 2);                            // texture(2) = EdgeTex
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(StepsParams), STEPS_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();
  rampTex->release();
  edgeTex->release();
  // rps is cache-owned (tex_op_cache), not released here.
}

// Self-registration. Ports mirror Steps.cs field order. FORKS (named): the Ramp/Edge two-gradient-
// port ambiguity (see file header); SmoothRadius exposed as a pin but never read (dead in the
// HLSL); Resolution normalized to the engine's standard Resolution/CustomW/CustomH convention (same
// fork class as every other image-filter op's output-sizing knob).
static const ImageFilterOp _reg_steps{
    {"Steps", "Steps",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      {"Count", "Count", "Float", true, 4.0f, 1.0f, 32.0f},
      {"Bias", "Bias", "Float", true, 0.5f, 0.01f, 0.99f},
      {"Offset", "Offset", "Float", true, 0.0f, -1.0f, 1.0f},
      {"Highlight.x", "Highlight", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Highlight.y", "Highlight.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"Highlight.z", "Highlight.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Highlight.w", "Highlight.w", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"HighlightIndex", "HighlightIndex", "Float", true, 0.0f, 0.0f, 32.0f},
      {"Ramp", "Ramp", "Gradient", true},
      {"Edge", "Edge", "Gradient", true},
      {"SmoothRadius", "SmoothRadius", "Float", true, 0.0f, 0.0f, 10.0f},  // dead in HLSL, pin-parity only
      {"UseSuperSampling", "UseSuperSampling", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},
      {"Repeat", "Repeat", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "Steps", cookSteps, "steps", runStepsSelfTest};

}  // namespace sw
