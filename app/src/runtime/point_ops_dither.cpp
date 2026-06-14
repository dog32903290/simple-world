// Dither image-filter texture op (lane image_filter) — Bayer/hash ordered-dither quantizer.
// TiXL authority: external/tixl Operators/Lib/Assets/shaders/img/fx/Dither.hlsl +
// Operators/Lib/image/fx/stylize/Dither.cs (ports). Per-pixel: resample the source on a Scale/
// Offset grid, gain/bias to a grayscale, Bayer64 (or hash) dither threshold -> binary, lerp
// ShadowColor<->HighlightColor, optionally blend back over the source.
//
// Single-pass port: cookDither reads c.inputTexture, runs one fullscreen pass of
// dither_vs/dither_fs, writes c.output. Binds b0 = DitherParams (Black/White/GrayScaleWeights/
// GainAndBias/Scale/Method/Offset/BlendMode/IsTextureValid), b1 = DitherResolution (source dims =
// the .hlsl's framework Resolution). No upstream texture wired: IsTextureValid=0, output the
// dither colour directly (no source to blend over) — but we still need a texture to sample, so the
// no-input branch clears to black (matches the other image filters' no-input contract).
//
// FORK (named): GrayScaleWeights port kept (Dither.cs InputSlot) but unused by the kernel exactly
// as TiXL; grayScale float4->float truncates to .r. See dither.metal / dither_params.h.
//
// Self-contained leaf: cookDither + registerDitherOp() + runDitherSelfTest.
// Shares the D2-2 PSO+scratch cache seam (tex_op_cache.h) with Blur/Displace/Pixelate/etc.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/dither_params.h"  // DitherParams, DitherResolution, DITHER_Params/Resolution
#include "runtime/eval_context.h"
#include "runtime/point_graph.h"    // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"   // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

void cookDither(TexCookCtx& c) {
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

  MTL::RenderPipelineState* rps = cachedTexPSO(c.dev, c.lib, "dither_vs", "dither_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp (see .metal)
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL Dither.hlsl ParamConstants (b0). Ports = Dither.cs InputSlots; defaults match Dither's
  // neutral expectations (black shadow / white highlight, luma weights, identity gain/bias, Bayer
  // Method, Scale 1, no Offset, Normal blend).
  DitherParams p{};
  p.BlackR = cookParam(c, "ShadowColor.r", 0.0f);   // ShadowColor (Dither.cs)
  p.BlackG = cookParam(c, "ShadowColor.g", 0.0f);
  p.BlackB = cookParam(c, "ShadowColor.b", 0.0f);
  p.BlackA = cookParam(c, "ShadowColor.a", 1.0f);
  p.WhiteR = cookParam(c, "HighlightColor.r", 1.0f);  // HighlightColor (Dither.cs)
  p.WhiteG = cookParam(c, "HighlightColor.g", 1.0f);
  p.WhiteB = cookParam(c, "HighlightColor.b", 1.0f);
  p.WhiteA = cookParam(c, "HighlightColor.a", 1.0f);
  p.GrayR = cookParam(c, "GrayScaleWeights.r", 0.299f);  // GrayScaleWeights (declared/unused fork)
  p.GrayG = cookParam(c, "GrayScaleWeights.g", 0.587f);
  p.GrayB = cookParam(c, "GrayScaleWeights.b", 0.114f);
  p.GrayA = cookParam(c, "GrayScaleWeights.a", 0.0f);
  p.GainAndBiasX = cookParam(c, "GainAndBias.x", 0.5f);
  p.GainAndBiasY = cookParam(c, "GainAndBias.y", 0.5f);
  p.Scale  = cookParam(c, "Scale", 1.0f);
  p.Method = cookParam(c, "Method", 0.0f);  // 0 = Bayer (FloydSteinberg enum), else hash
  p.OffsetX = cookParam(c, "Offset.x", 0.0f);
  p.OffsetY = cookParam(c, "Offset.y", 0.0f);
  p.BlendMode = cookParam(c, "BlendMethod", 0.0f);  // BlendMethod (Dither.cs) -> BlendMode
  p.IsTextureValid = 1.0f;  // host flag: input texture is wired (we are past the no-input branch)

  DitherResolution res{};
  res.TargetWidth  = (float)c.inputTexture->width();   // = Dither.hlsl framework Resolution
  res.TargetHeight = (float)c.inputTexture->height();

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
  enc->setFragmentBytes(&p,   sizeof(DitherParams),     DITHER_Params);
  enc->setFragmentBytes(&res, sizeof(DitherResolution), DITHER_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

void registerDitherOp() { registerTexOp("Dither", cookDither); }

// --- Dither MATH golden -----------------------------------------------------------------------
// Dither quantizes the (gain/biased) source luminance into a binary Shadow/Highlight field via a
// Bayer ordered-dither threshold: brighter source regions cross the threshold more often -> more
// Highlight (white) pixels -> higher MEAN output luminance.
// Source: TOP half bright (luma ~0.75 grey), BOTTOM half dark (luma ~0.25 grey). ShadowColor=black,
// HighlightColor=white, BlendMethod=Normal -> with full-alpha colours BlendColors is a passthrough
// of the dithered colour. We assert MEAN luminance(top) > MEAN luminance(bottom) by a margin: the
// dither density tracks source brightness.
// injectBug Scale=0: epsilonScale = -0.0001 -> divisions explode -> every pixel resamples ~the same
// cell center (uniform grayScale, source-region-independent) -> top and bottom means CONVERGE ->
// the "brighter-on-top" ordering assertion FAILS (teeth). Scale is the grid-resolution knob; Scale=0
// is a real grid collapse, not a flipped assertion.
int runDitherSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-dither] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // Source: top half bright grey (~0.75), bottom half dark grey (~0.25).
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      uint8_t v = (y < H / 2) ? 191 : 64;  // 0.75 vs 0.25
      in[i] = v; in[i + 1] = v; in[i + 2] = v; in[i + 3] = 255;
    }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  std::map<std::string, float> params;
  params["ShadowColor.r"] = 0.0f; params["ShadowColor.g"] = 0.0f;
  params["ShadowColor.b"] = 0.0f; params["ShadowColor.a"] = 1.0f;
  params["HighlightColor.r"] = 1.0f; params["HighlightColor.g"] = 1.0f;
  params["HighlightColor.b"] = 1.0f; params["HighlightColor.a"] = 1.0f;
  params["GrayScaleWeights.r"] = 0.299f; params["GrayScaleWeights.g"] = 0.587f;
  params["GrayScaleWeights.b"] = 0.114f; params["GrayScaleWeights.a"] = 0.0f;
  params["GainAndBias.x"] = 0.5f; params["GainAndBias.y"] = 0.5f;  // identity gain/bias
  params["Scale"]   = injectBug ? 0.0f : 16.0f;  // bug: grid collapse -> source-independent output
  params["Method"]  = 0.0f;  // Bayer
  params["Offset.x"] = 0.0f; params["Offset.y"] = 0.0f;
  params["BlendMethod"] = 0.0f;  // Normal -> passthrough of dither colour (full-alpha)

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookDither(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // Mean luminance of the top half vs the bottom half (use R channel; output is grey black/white).
  double topSum = 0.0, botSum = 0.0;
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      int v = out[((size_t)y * W + x) * 4];
      if (y < H / 2) topSum += v; else botSum += v;
    }
  double topMean = topSum / (double)(W * H / 2);
  double botMean = botSum / (double)(W * H / 2);

  bool brighterOnTop = (topMean - botMean) > 25.0;  // injectBug Scale=0 -> means converge -> fails
  bool pass = brighterOnTop;
  printf("[selftest-dither] topMean=%.1f botMean=%.1f diff=%.1f -> brighterOnTop=%d -> %s\n",
         topMean, botMean, topMean - botMean, brighterOnTop ? 1 : 0, pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
