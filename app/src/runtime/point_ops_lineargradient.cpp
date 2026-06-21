// LinearGradient image generator op (the PROVING leaf for the Gradient->t1 image-filter binding seam).
//
// TiXL authority: external/tixl/Operators/Lib/image/generate/basic/LinearGradient.{cs,hlsl,t3}.
//   .cs   — slot declarations (Image, Gradient, Width, SizeMode, Offset, OffsetMode, PingPong, Repeat,
//           Rotate, Center, GainAndBias, BlendMode, Resolution, ...) + enum maps.
//   .t3   — defaults (Width=1, Rotate=90, GainAndBias=(0.5,0.5), SizeMode=0, OffsetMode=0,
//           PingPong/Repeat=false, Center=(0,0), Offset=0, BlendMode=0) + the Offset routing
//           (Multiply + PickFloat) + the Gradient→GradientsToTexture→t1 plumbing.
//   .hlsl — psMain (ported VERBATIM to shaders/lineargradient.metal).
//
// Port class: a .t3 compound whose terminal is _multiImageFxSetupStatic → a single fragment shader
//   (lineargradient_vs/lineargradient_fs). Like NGon, this is a RENDER op (NOT compute): cachedTexPSO
//   → renderCommandEncoder → setFragmentTexture/Sampler/Bytes → drawPrimitives triangle 3 verts. The
//   precedent cloned is point_ops_ngon.cpp.
//
// The Gradient→t1 binding (HALF B): the op reads its gathered Gradient input (c.inputGradients[0]),
// rasterizes it to a 1×512 RGBA row via rasterizeGradientRow (the SAME row sampling GradientsToTexture
// uses — gradient_raster.h, can't drift), and binds it at fragment texture(1) with the clampedSampler.
//
// ★Unwired-Gradient fallback (kDefaultLinearGradient) — TRACED from the .t3 connection, NOT the
//   child's embedded value: LinearGradient.t3 :368-371 wires the op's OWN Gradient input slot
//   (e47e9e63, default black→white at t3:71-96) INTO the GradientsToTexture child's Gradients slot
//   (588be11f). So an UNWIRED Gradient input feeds the op's Gradient SLOT DEFAULT — black→white —
//   into the gradient row; the child's embedded magenta→blue default (t3:166-192) is OVERRIDDEN by
//   that connection and never reached. We mirror the live routing: fallback = black→white (t3:71-96).
//   [fork-gradient-default-traced]
//
// ★Offset routing (Multiply + PickFloat): the shader cbuffer Offset is NOT the raw Offset input —
//   the .t3 routes it as PickFloat(Index=OffsetMode, [Offset, Width×Offset]). Replicated as a SCALAR
//   expression in the cook fn (NOT a cbuffer reshuffle). See lineargradient_params.h Offset-routing.
//
// FORKS (named): generator dummy (1×1 transparent-black ImageA when unwired); gradient-row format
//   RGBA32F (gradient_raster.h fork-grad-row-format-32f); gain/bias + BlendColors inlined in
//   lineargradient.metal; fork-gradient-default-traced (above).
//
// Self-contained leaf: cookLinearGradient + ImageFilterOp self-registration + runLinearGradientSelfTest
//   (impl in gradient_golden.cpp; declared here). CMake point_ops*.cpp glob + shaders/*.metal glob
//   auto-pick both files — no CMake edit.
#include "runtime/point_ops.h"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/gradient_raster.h"            // rasterizeGradientRow, kGradientRowN
#include "runtime/image_filter_op_registry.h"   // ImageFilterOp self-registration
#include "runtime/lineargradient_params.h"      // LinearGradientParams/Resolution, LINEARGRADIENT_*
#include "runtime/point_graph.h"                // TexCookCtx, cookParam
#include "runtime/sw_gradient.h"                // SwGradient (the consumed currency)
#include "runtime/tex_op_cache.h"              // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

int runLinearGradientSelfTest(bool injectBug);  // gradient_golden.cpp

namespace {

// The unwired-Gradient fallback: the op's Gradient SLOT default (LinearGradient.t3 :71-96), which the
// .t3 connection (:368-371) feeds into the GradientsToTexture child. Black→white, 2-stop Linear.
// [fork-gradient-default-traced]
SwGradient defaultLinearGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(0.0f, 1.2159347e-11f, 1e-06f, 1.0f)});  // t3:75-83
  g.steps.push_back({1.0f, simd::make_float4(1.0f, 0.99999f, 1.0f, 1.0f)});          // t3:85-93
  return g;
}

// 1×1 transparent-black dummy for the no-Image case (generator mode). Same convention as
// makeNGonDummyTex — the shader always gets a valid ImageA handle.
MTL::Texture* makeDummyTex(MTL::Device* dev) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  const uint8_t px[4] = {0, 0, 0, 0};
  t->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, px, 4);
  return t;
}

// LinearGradient texture op: single fullscreen pass. Reads c.inputGradients[0] (the gathered Gradient),
// rasterizes it to a 1×512 row, samples it in the shader at (dBiased, 0). Optionally composites over
// c.inputTexture (Image). Always writes c.output.
void cookLinearGradient(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "lineargradient_vs", "lineargradient_fs", fmt);
  if (!rps) return;

  // s0 texSampler: linear+Wrap (ImageA), matching _multiImageFxSetupStatic.t3 WrapMode=Wrap.
  MTL::SamplerDescriptor* sd0 = MTL::SamplerDescriptor::alloc()->init();
  sd0->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd0->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd0->setSAddressMode(MTL::SamplerAddressModeRepeat);
  sd0->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp0 = c.dev->newSamplerState(sd0);
  sd0->release();

  // ★s1 clampedSampler: linear+ClampToEdge (the gradient row). MANDATORY — the row is sampled at v=0
  // with the gradient value at u=dBiased; a Wrap sampler would corrupt the u/v edges.
  MTL::SamplerDescriptor* sd1 = MTL::SamplerDescriptor::alloc()->init();
  sd1->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd1->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd1->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd1->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp1 = c.dev->newSamplerState(sd1);
  sd1->release();

  // --- b0 params (LinearGradient.cs/.t3 defaults) ---
  LinearGradientParams p{};
  p.CenterX = cookParam(c, "Center.x", 0.0f);
  p.CenterY = cookParam(c, "Center.y", 0.0f);
  p.Width   = cookParam(c, "Width", 1.0f);
  p.Rotation = cookParam(c, "Rotate", 90.0f);  // hlsl Rotation <- Rotate input (.t3 default 90)
  p.PingPong = cookParam(c, "PingPong", 0.0f);
  p.Repeat   = cookParam(c, "Repeat", 0.0f);
  p.GainAndBiasX = cookParam(c, "GainAndBias.x", 0.5f);
  p.GainAndBiasY = cookParam(c, "GainAndBias.y", 0.5f);
  p.SizeMode = cookParam(c, "SizeMode", 0.0f);   // 0 = AlignToHeight
  p.BlendMode = cookParam(c, "BlendMode", 0.0f);  // 0 = Normal

  // ★Offset routing: PickFloat(OffsetMode, [Offset, Width×Offset]). Scalar expression (not a cbuffer
  // reshuffle). OffsetMode=0 RelativeToImage → Offset; OffsetMode=1 RelativeToSize → Width×Offset.
  const float rawOffset  = cookParam(c, "Offset", 0.0f);
  const float offsetMode = cookParam(c, "OffsetMode", 0.0f);
  p.Offset = (offsetMode < 0.5f) ? rawOffset : (p.Width * rawOffset);

  // IsTextureValid: 1.0 if Image wired, else 0.0 (generator mode → return gradient).
  p.IsTextureValid = (c.inputTexture != nullptr) ? 1.0f : 0.0f;

  // b1 Resolution
  LinearGradientResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // Pull the gradient (gathered input, or the traced black→white fallback when unwired).
  const SwGradient& g = (c.inputGradients && !c.inputGradients->empty())
                            ? (*c.inputGradients)[0]
                            : defaultLinearGradient();
  MTL::Texture* gradTex = rasterizeGradientRow(c.dev, g, kGradientRowN);  // owned; release after draw
  if (!gradTex) { samp0->release(); samp1->release(); return; }

  // Bind ImageA (or 1×1 transparent-black dummy when no upstream). [generator-dummy]
  MTL::Texture* dummyTex = nullptr;
  const MTL::Texture* imageTex = c.inputTexture;
  if (!imageTex) { dummyTex = makeDummyTex(c.dev); imageTex = dummyTex; }

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(imageTex), 0);  // t0 ImageA
  enc->setFragmentTexture(gradTex, 1);                              // t1 Gradient row
  enc->setFragmentSamplerState(samp0, 0);                          // s0 texSampler (Wrap)
  enc->setFragmentSamplerState(samp1, 1);                          // s1 clampedSampler (ClampToEdge)
  enc->setFragmentBytes(&p,   sizeof(LinearGradientParams),     LINEARGRADIENT_Params);
  enc->setFragmentBytes(&res, sizeof(LinearGradientResolution), LINEARGRADIENT_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp0->release();
  samp1->release();
  gradTex->release();
  if (dummyTex) dummyTex->release();
}

}  // namespace

// Self-registration. File-scope static feeds imageFilterSpecSink() + texReg() + imageFilterSelfTests()
// during pre-main dynamic init. No shared file edited (point_ops*.cpp glob picks this up).
static const ImageFilterOp _reg_lineargradient{
    // LinearGradient (TiXL Lib.image.generate.basic.LinearGradient): rotated/offset linear gradient.
    // Gradient input (the t1 binding) + optional Image input → Texture2D out. When no Image: returns
    // the gradient directly (IsTextureValid=0); when wired: BlendColors composite.
    {"LinearGradient", "LinearGradient",
     {// Optional Image input (TiXL default null — generator mode draws the gradient on its own)
      {"Image", "Image", "Texture2D", true},
      // Gradient input (the 8th cook flow; the t1 binding). Unwired → traced black→white fallback.
      {"Gradient", "Gradient", "Gradient", true},
      {"out", "out", "Texture2D", false},
      // Width (Single, TiXL t3 default 1.0)
      {"Width", "Width", "Float", true, 1.0f, 0.0f, 4.0f, Widget::Slider},
      // Rotate (Single, TiXL t3 default 90.0; degrees)
      {"Rotate", "Rotate", "Float", true, 90.0f, -180.0f, 180.0f},
      // Center (Vec2, TiXL t3 default (0,0))
      {"Center.x", "Center", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Center.y", "Center.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // GainAndBias (Vec2, TiXL t3 default (0.5,0.5))
      {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Offset (Single, TiXL t3 default 0.0; routed via PickFloat with OffsetMode)
      {"Offset", "Offset", "Float", true, 0.0f, -4.0f, 4.0f, Widget::Slider},
      // OffsetMode (Int→float enum; TiXL t3 default 0 = RelativeToImage)
      {"OffsetMode", "OffsetMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
       {"RelativeToImage", "RelativeToSize"}},
      // SizeMode (Int→float enum; TiXL t3 default 0 = AlignToHeight)
      {"SizeMode", "SizeMode", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Enum,
       {"AlignToHeight", "AlignToWidth"}},
      // PingPong / Repeat (bool→float; TiXL t3 default false)
      {"PingPong", "PingPong", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"Repeat", "Repeat", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      // BlendMode (Int→float; TiXL t3 default 0 = normal composite with upstream image)
      {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 9.0f, Widget::Enum,
       {"Normal", "Screen", "Multiply", "Overlay", "Difference", "SrcOnly",
        "DstOnly", "HardLight", "LinearDodge", "AlphaMask"}},
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "LinearGradient", cookLinearGradient, "lineargradient", runLinearGradientSelfTest};

}  // namespace sw
