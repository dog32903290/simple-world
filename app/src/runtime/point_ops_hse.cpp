// HSE (Hue/Saturation/Exposure) image-filter texture op (lane multi-image, image/color) — a 2-input
// Fx-modulation op: the SECOND input (FxTexture) adds its .g channel to the hue shift PER-PIXEL,
// the SAME shape as the shipped Displace (Image + DisplaceMap). Faithful single-pass port: cookHse
// reads inputTextures[0]=Image, [1]=FxTexture, runs HueShift.hlsl's HSB hue/sat/exposure math.
//
// TiXL authority:
//   external/tixl Operators/Lib/image/color/HSE.cs   — op ports: Texture2d(t0 Image) / Hue /
//       FxTexture(t1) / Saturation / Exposure. Two FIXED numbered Texture2D inputs (NOT MultiInput).
//   .../image/color/HSE.t3 — STEP-0 verified: a STANDARD _multiImageFxSetupStatic wrapper with
//       ShaderPath = "Lib:shaders/img/fx/HueShift.hlsl" (the SAME image-filter boilerplate as
//       Displace/Blur — the op's BEHAVIOR is the single .hlsl psMain -> ATOMIC). The FloatParams
//       multi-input connection order (Hue, Saturation, Exposure) maps 1:1 to the HLSL cbuffer
//       { Hue, Saturation, Exposure } — NO intermediate Multiply/IntToFloat math (no DirectionalBlur
//       trap). ImageA(55126bff)<-Texture2d(t0), ImageB(0bb90f8d)<-FxTexture(t1). .t3 DEFAULTS:
//       Hue 0.0, Saturation 1.0, Exposure 1.0; WrapMode "Wrap", TextureFilter "MinMagPointMipLinear".
//   .../Assets/shaders/img/fx/HueShift.hlsl — the pixel shader, ported VERBATIM into hse.metal.
//
// SEAM NOTE — ZERO shared-graph edit. The multi-image seam already gathers up to kMaxTexInputs
// Texture2D ports into TexCookCtx::inputTextures[] in spec port order (point_graph_tex_cook.cpp /
// point_graph_resident_tex_cook.cpp). slot 0=Image, 1=FxTexture always.
//
// FORK (named):
//   [fork-hse-unwired-fxtexture] — an unwired FxTexture slot is null. TiXL binds the slot-default
//     (empty / null) and the shader reads fx.g from it; a null Metal texture binding samples
//     (0,0,0,0) -> fx.g = 0 -> hueShift = Hue + 0 (pure Hue, no per-pixel modulation). We mirror
//     this by binding the Image itself when FxTexture is unwired BUT then... no: a null fx must read
//     0. So when FxTexture is unwired we DO NOT substitute Image (that would inject Image.g into the
//     hue). Instead the cook binds a 1x1 BLACK dummy for the FxTexture slot so fx.g=0 — byte-faithful
//     to TiXL's null-slot (hue shift = Hue only). Same fork class as Displace's empty-input handling.
//   [fork-hse-point-wrap-sampler-not-load-bearing] — texSampler = Point min/mag + Repeat (TiXL .t3
//     WrapMode "Wrap" / TextureFilter "MinMagPointMipLinear"). Both inputs sample at the SAME
//     in-[0,1] texCoord, no warp/OOB -> wrap is not load-bearing (mirror of CMC's mirror-sampler
//     note); kept faithful.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/hse_params.h"  // HseParams, HSE_Params
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"
#include "runtime/tex_op_cache.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

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

// A 1x1 opaque-black texture for an unwired FxTexture slot (fx.g = 0 -> hue shift = Hue only,
// byte-faithful to TiXL's null FxTexture slot). Caller owns the returned texture.
MTL::Texture* makeBlack1x1(MTL::Device* dev) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  if (!t) return nullptr;
  const uint8_t black[4] = {0, 0, 0, 255};
  t->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, black, 4);
  return t;
}

// HSE cook: read Image (inputTextures[0]) + FxTexture (inputTextures[1], or 1x1 black if unwired),
// one fullscreen pass into c.output running HueShift.hlsl's HSB math.
void cookHse(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  const MTL::Texture* image = c.inputTextureCount > 0 ? c.inputTextures[0] : nullptr;
  const MTL::Texture* fxIn = c.inputTextureCount > 1 ? c.inputTextures[1] : nullptr;
  if (!image) { clearTexture(c.queue, c.output); return; }  // no Image -> nothing to recolor

  MTL::Texture* blackDummy = nullptr;
  const MTL::Texture* fx = fxIn;
  if (!fx) { blackDummy = makeBlack1x1(c.dev); fx = blackDummy; }  // [fork-hse-unwired-fxtexture]

  MTL::RenderPipelineState* rps = cachedTexPSO(c.dev, c.lib, "hse_vs", "hse_fs", fmt);
  if (!rps) { if (blackDummy) blackDummy->release(); return; }

  // texSampler (s0): Point min/mag + Repeat (TiXL .t3 WrapMode "Wrap" / "MinMagPointMipLinear").
  // [fork-hse-point-wrap-sampler-not-load-bearing]
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterNearest);
  sd->setMagFilter(MTL::SamplerMinMagFilterNearest);
  sd->setSAddressMode(MTL::SamplerAddressModeRepeat);
  sd->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL params (HSE.t3 defaults). cbuffer order { Hue, Saturation, Exposure } (1:1, no math).
  HseParams p{};
  p.Hue = cookParam(c, "Hue", 0.0f);
  p.Saturation = cookParam(c, "Saturation", 1.0f);
  p.Exposure = cookParam(c, "Exposure", 1.0f);
  p._pad0 = 0.0f;

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
  enc->setFragmentTexture(const_cast<MTL::Texture*>(fx), 1);     // texture(1) = FxTexture
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(HseParams), HSE_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();
  if (blackDummy) blackDummy->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

int runHseSelfTest(bool injectBug);

// Self-registration. Ports 1:1 from HSE.cs — two FIXED Texture2D inputs (Image + FxTexture), the out,
// Hue/Saturation/Exposure scalars (HSE.t3 defaults: 0/1/1). Type name "HSE". Resolution plumbing
// mirrors the other image filters (RenderTarget/Blur/Displace enum).
static const ImageFilterOp _reg_hse{
    {"HSE", "HSE",
     {{"Image", "Image", "Texture2D", true},
      {"FxTexture", "FxTexture", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      {"Hue", "Hue", "Float", true, 0.0f, -1.0f, 1.0f},
      {"Saturation", "Saturation", "Float", true, 1.0f, 0.0f, 4.0f},
      {"Exposure", "Exposure", "Float", true, 1.0f, 0.0f, 4.0f},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "HSE", cookHse, "hse", runHseSelfTest};

// --- HSE MATH golden (closed-form, d=0 saturated solid) ------------------------------------------
// Image = solid pure RED (255,0,0). FxTexture = solid with g = 1/3 (the .g channel is the ONLY
// channel HueShift reads from the 2nd input). Params = .t3 defaults (Hue 0, Saturation 1,
// Exposure 1) so the ONLY transform is the per-pixel hue shift driven by FxTexture.g.
//
// Hand derivation (HueShift.hlsl):
//   rgb2hsb((1,0,0)) = (H=0, S=1, B=0.5)            (B = max(rgb)*0.5 = 1*0.5 = 0.5)
//   Exposure: B *= 1            -> 0.5
//   hueShift = Hue + fx.g = 0 + 1/3 = 1/3
//   H = mod(0 + 1/3, 1) = 1/3
//   Saturation: S = saturate(1 * 1) = 1
//   hsb2rgb((1/3, 1, 0.5)): z=0.5 -> the (z<0.5) FALSE branch with mix-weight (c.z*2-1)=0 ->
//     weight = c.y = 1 -> clamp(abs(fract(H + (1,2/3,1/3))*6 - 3) - 1, 0, 1) = (0, 1, 0) = pure GREEN.
// => Image RED + FxTexture.g=1/3 -> output GREEN (G high, R/B low). Large, non-degenerate.
//
// TOOTH: if inputTextures[1] (FxTexture) were ignored, fx.g would read 0 -> hueShift = 0 -> H stays
// 0 -> output stays RED. injectBug forces fx.g read to 0 (clears the FxTexture to black) so the
// output is RED, failing the "is GREEN" assertion. (No -bug CLI flag — the proof is also exercised
// by breaking the op manually; see report.)
constexpr uint32_t kGW = 32, kGH = 32;

static void fillSolid(MTL::Texture* t, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  for (size_t i = 0; i < (size_t)w * h; ++i) {
    px[i * 4 + 0] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255;
  }
  t->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

static bool hseCookCenter(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool dropFx,
                          uint8_t out[4]) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kGW, kGH, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* image = dev->newTexture(td);
  MTL::Texture* fx = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  fillSolid(image, kGW, kGH, 255, 0, 0);  // pure RED
  // FxTexture: g = 1/3 (85/255 = 0.3333). injectBug clears it to black -> g = 0 (FxTexture ignored).
  fillSolid(fx, kGW, kGH, 0, dropFx ? 0 : 85, 0);

  std::map<std::string, float> params;  // Hue/Saturation/Exposure default (0/1/1) via cookParam.
  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.output = dst; c.params = &params;
  c.inputTextures[0] = image; c.inputTextures[1] = fx; c.inputTextureCount = 2;
  c.inputTexture = image;
  cookHse(c);

  std::vector<uint8_t> px((size_t)kGW * kGH * 4, 0);
  dst->getBytes(px.data(), kGW * 4, MTL::Region::Make2D(0, 0, kGW, kGH), 0);
  const uint32_t cx = kGW / 2, cy = kGH / 2;
  size_t i = ((size_t)cy * kGW + cx) * 4;
  out[0] = px[i]; out[1] = px[i + 1]; out[2] = px[i + 2]; out[3] = px[i + 3];

  image->release(); fx->release(); dst->release();
  return true;
}

int runHseSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-hse] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  uint8_t got[4] = {0, 0, 0, 0};
  hseCookCenter(dev, q, lib, /*dropFx=*/injectBug, got);

  // Expected (fx.g=1/3): GREEN — G high, R+B low. The hue-shifted red rotates to green.
  bool isGreen = got[1] > 200 && got[0] < 55 && got[2] < 55 && got[3] > 250;
  bool pass = isGreen;
  printf("[selftest-hse] got=(%u,%u,%u,%u) expect GREEN(G>200,R<55,B<55,A>250) isGreen=%d "
         "injectBug=%d -> %s\n",
         got[0], got[1], got[2], got[3], isGreen ? 1 : 0, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release();
  clearTexOpCache();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
