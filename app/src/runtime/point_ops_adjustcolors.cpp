// AdjustColors image-filter texture op (lane F3-3) — filter wave 3, op 3.
// TiXL authority: Operators/Lib/image/color/AdjustColors.cs (Texture2d/Colorize/Saturation/
// Hue/Contrast/Exposure/Brightness/PreventClamping/Vignette/OrangeTeal/Background inputs) +
// AdjustColors.t3 (defaults) + Assets/shaders/img/fx/AdjustColors.hlsl (the single-pass kernel:
// HSB color space ops, vignette, colorize blend, contrast S-curve, brightness, background
// composite).
//
// Single-pass port: cookAdjustColors reads c.inputTexture (upstream RenderTarget's Texture2D),
// runs one fullscreen pass of adjustcolors_vs/adjustcolors_fs, writes c.output. Vec4 params
// (Colorize, Background) decomposed into r/g/b/a components; Vec2 (PreventClamping) split as
// x/y. AdjustColorsParams maps the cbuffer in HLSL order.
//
// Self-contained leaf: cookAdjustColors + registerAdjustColorsOp() + runAdjustColorsSelfTest.
// Shares the D2-2 PSO+scratch cache seam (tex_op_cache.h) with Blur/Displace/Tint/ChromaB.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/adjustcolors_params.h"  // AdjustColorsParams, ADJUSTCOLORS_Params
#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"           // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"          // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// AdjustColors texture op: single pass. Reads c.inputTexture, writes c.output.
// No upstream texture wired: clear output to black.
void cookAdjustColors(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  if (!c.inputTexture) {
    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = pass->colorAttachments()->object(0);
    ca->setTexture(c.output);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
    ca->setStoreAction(MTL::StoreActionStore);
    MTL::CommandBuffer* cmd = c.queue->commandBuffer();
    cmd->renderCommandEncoder(pass)->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    return;
  }

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "adjustcolors_vs", "adjustcolors_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp (see adjustcolors.metal)
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL params: AdjustColors.cs/.t3 defaults.
  AdjustColorsParams p{};
  // Colorize (Vec4), default (1,1,1,0) — alpha=0 means no colorize
  p.ColorizeR = cookParam(c, "Colorize.r", 1.0f);
  p.ColorizeG = cookParam(c, "Colorize.g", 1.0f);
  p.ColorizeB = cookParam(c, "Colorize.b", 1.0f);
  p.ColorizeA = cookParam(c, "Colorize.a", 0.0f);
  // Background (Vec4), default near-zero rgb with alpha=1 (transparent black)
  p.BackgroundR = cookParam(c, "Background.r", 1e-6f);
  p.BackgroundG = cookParam(c, "Background.g", 1e-6f);
  p.BackgroundB = cookParam(c, "Background.b", 1e-6f);
  p.BackgroundA = cookParam(c, "Background.a", 1.0f);
  // Scalars
  p.Exposure       = cookParam(c, "Exposure", 1.0f);
  p.Contrast       = cookParam(c, "Contrast", 0.0f);
  p.Saturation     = cookParam(c, "Saturation", 1.0f);
  p.OrangeTeal     = cookParam(c, "OrangeTeal", 0.0f);
  p.PreventClampX  = cookParam(c, "PreventClamping.x", 0.0f);
  p.PreventClampY  = cookParam(c, "PreventClamping.y", 5.0f);
  p.Brightness     = cookParam(c, "Brightness", 0.0f);
  p.Hue            = cookParam(c, "Hue", 0.0f);
  p.Vignette       = cookParam(c, "Vignette", 0.0f);

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(c.inputTexture), 0);
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(AdjustColorsParams), ADJUSTCOLORS_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

// Self-registration. NodeSpec literal moved verbatim from node_registry_image_filter.cpp.
static const ImageFilterOp _reg_adjustcolors{
    // AdjustColors (TiXL Lib.image.color.AdjustColors): comprehensive color grading — HSB ops
    // (hue/saturation/exposure/brightness/contrast), vignette, colorize, background composite.
    // Single Texture2D in → Texture2D out (point_ops_adjustcolors.cpp). Params mirror
    // AdjustColors.cs. Resolution = same enum. FORK (named): TiXL's Wrap omitted (fixed clamp).
    {"AdjustColors", "AdjustColors",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      {"Exposure", "Exposure", "Float", true, 1.0f, 0.0f, 4.0f},
      {"Contrast", "Contrast", "Float", true, 0.0f, -1.0f, 2.0f},
      {"Saturation", "Saturation", "Float", true, 1.0f, 0.0f, 4.0f, Widget::Slider},
      {"Brightness", "Brightness", "Float", true, 0.0f, -1.0f, 1.0f},
      {"Hue", "Hue", "Float", true, 0.0f, -180.0f, 180.0f},
      {"Vignette", "Vignette", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Slider},
      {"OrangeTeal", "OrangeTeal", "Float", true, 0.0f, -1.0f, 1.0f},
      // Colorize (Vec4, TiXL default (1,1,1,0) — alpha=0 means no colorize)
      {"Colorize.r", "Colorize", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Colorize.g", "Colorize.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Colorize.b", "Colorize.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Colorize.a", "Colorize.a", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // PreventClamping (Vec2, TiXL default (0, 5))
      {"PreventClamping.x", "PreventClamping", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"PreventClamping.y", "PreventClamping.y", "Float", true, 5.0f, 1.0f, 10.0f, Widget::Vec, {}, true, 1},
      // Background (Vec4, TiXL default ~(0,0,0,1) — near-zero rgb transparent black)
      {"Background.r", "Background", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Background.g", "Background.g", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.b", "Background.b", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.a", "Background.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "AdjustColors", cookAdjustColors, "adjustcolors", runAdjustColorsSelfTest};

// --- AdjustColors MATH golden -----------------------------------------------------------------
// Source: a solid red square (r=200, g=40, b=40, a=255). Run AdjustColors with Saturation=0.0
// (fully desaturated): the output should be greyscale (r≈g≈b). We assert that the green and
// blue channels of the output rise to near the red level (all three within 30 of each other).
// With default Saturation=1.0 (injectBug) the red square stays red (g≈40, r≈200 >> g) so the
// equality assertion FAILS (teeth).
int runAdjustColorsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-adjustcolors] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Source: solid red (r=200, g=40, b=40, a=255).
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (size_t i = 0; i < (size_t)W * H; ++i) {
    in[i * 4 + 0] = 200; in[i * 4 + 1] = 40; in[i * 4 + 2] = 40; in[i * 4 + 3] = 255;
  }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  std::map<std::string, float> params;
  params["Saturation"]     = injectBug ? 1.0f : 0.0f;  // bug: keep saturation -> red stays red
  params["Exposure"]       = 1.0f;
  params["Contrast"]       = 0.0f;
  params["Brightness"]     = 0.0f;
  params["Hue"]            = 0.0f;
  params["Vignette"]       = 0.0f;
  params["OrangeTeal"]     = 0.0f;
  params["Colorize.a"]     = 0.0f;  // no colorize
  params["Background.a"]   = 1.0f;  // opaque background (near-zero rgb = transparent effectively)
  params["Background.r"]   = 1e-6f; params["Background.g"] = 1e-6f; params["Background.b"] = 1e-6f;
  params["PreventClamping.x"] = 0.0f; params["PreventClamping.y"] = 5.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookAdjustColors(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  const uint32_t cx = W / 2, cy = H / 2;
  size_t i = ((size_t)cy * W + cx) * 4;
  int R = out[i], G = out[i + 1], B = out[i + 2];
  // Desaturated: all three channels should be close to the same brightness.
  bool grey = (std::abs(R - G) < 30 && std::abs(R - B) < 30 && std::abs(G - B) < 30);
  bool nonBlack = (R + G + B) > 60;  // desaturated red is still bright
  bool pass = grey && nonBlack;
  printf("[selftest-adjustcolors] center R=%d G=%d B=%d grey=%d nonBlack=%d -> %s\n",
         R, G, B, grey ? 1 : 0, nonBlack ? 1 : 0, pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
