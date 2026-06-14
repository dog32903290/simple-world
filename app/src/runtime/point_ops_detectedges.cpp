// DetectEdges image-filter texture op (lane image_filter).
// TiXL authority: external/tixl/Operators/Lib/image/fx/stylize/DetectEdges.cs (ports) +
// DetectEdges.t3 (defaults) + Assets/shaders/img/fx/DetectEdges.hlsl (kernel).
//
// Single-pass port: cookDetectEdges reads c.inputTexture (upstream RenderTarget's Texture2D via
// the gather direct-through), runs one fullscreen pass of detectedges_vs/detectedges_fs, writes
// c.output. No upstream texture wired: clear output to black.
//
// The op binds ONE constant buffer (b0 = DetectEdgesParams). The HLSL's b1 Resolution cbuffer is
// replaced by reading the bound texture's own dimensions in-shader (see detectedges.metal); its
// b2 ParamConstants{int Invert} is an unwired TiXL field (always 0) — not bound, not a port.
//
// Self-contained leaf: cookDetectEdges + registerDetectEdgesOp() + runDetectEdgesSelfTest.
// Shares the D2-2 PSO+scratch cache seam (tex_op_cache.h) with Blur/Displace/Tint/ChannelMixer.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/detectedges_params.h"  // DetectEdgesParams, DETECTEDGES_Params
#include "runtime/eval_context.h"
#include "runtime/point_graph.h"         // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"        // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// DetectEdges texture op: single pass. Reads c.inputTexture, writes c.output.
// No upstream texture wired: clear output to black.
void cookDetectEdges(TexCookCtx& c) {
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
      cachedTexPSO(c.dev, c.lib, "detectedges_vs", "detectedges_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp (see .metal)
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL DetectEdges.cs defaults from DetectEdges.t3:
  // Color (1,1,1,1), SampleRadius 1.0, Strength 1.0, Contrast 0.0, MixOriginal 0.0,
  // OutputAsTransparent false.
  DetectEdgesParams p{};
  p.ColorR = cookParam(c, "Color.r", 1.0f);
  p.ColorG = cookParam(c, "Color.g", 1.0f);
  p.ColorB = cookParam(c, "Color.b", 1.0f);
  p.ColorA = cookParam(c, "Color.a", 1.0f);
  p.SampleRadius        = cookParam(c, "SampleRadius", 1.0f);
  p.Strength            = cookParam(c, "Strength", 1.0f);
  p.Contrast            = cookParam(c, "Contrast", 0.0f);
  p.MixOriginal         = cookParam(c, "MixOriginal", 0.0f);
  p.OutputAsTransparent = cookParam(c, "OutputAsTransparent", 0.0f);  // bool, default false

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
  enc->setFragmentBytes(&p, sizeof(DetectEdgesParams), DETECTEDGES_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

void registerDetectEdgesOp() { registerTexOp("DetectEdges", cookDetectEdges); }

// --- DetectEdges MATH golden ----------------------------------------------------------------
// Source: top half white (y < H/2), bottom half black — one strong horizontal edge at y=H/2.
// DetectEdges with Strength=1, Contrast=0, MixOriginal=0, Color=(1,1,1,1) maps:
//   - The border row (y=H/2): vertical neighbours straddle the white/black step ->
//     |y1-m| etc. are large -> average ~> 1 -> edgeColor ~ white -> BRIGHT.
//   - A flat interior row (e.g. y=H/4, fully inside the white region): all neighbours equal m
//     -> average=0 -> edgeColor=black -> DARK.
// Assert: border pixel BRIGHT (sum rgb > 150) AND flat interior pixel DARK (sum rgb < 30).
// injectBug Strength=0: edge magnitude *0 + Contrast(0) = 0 everywhere -> border NOT bright ->
// the "border bright" assertion FAILS (teeth).
int runDetectEdgesSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-detectedges] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // Source: top half white (y < H/2), bottom half black.
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      uint8_t v = (y < H / 2) ? 255 : 0;
      in[i] = v; in[i + 1] = v; in[i + 2] = v; in[i + 3] = 255;
    }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  std::map<std::string, float> params;
  params["Color.r"] = 1.0f; params["Color.g"] = 1.0f;
  params["Color.b"] = 1.0f; params["Color.a"] = 1.0f;
  params["SampleRadius"] = 1.0f;
  params["Strength"] = injectBug ? 0.0f : 1.0f;  // bug: edge magnitude scaled to 0
  params["Contrast"] = 0.0f;
  params["MixOriginal"] = 0.0f;
  params["OutputAsTransparent"] = 0.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookDetectEdges(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // Border pixel (y=H/2, the first black row, neighbours straddle the white/black step).
  const uint32_t bx = W / 2, by = H / 2;
  size_t bi = ((size_t)by * W + bx) * 4;
  int bR = out[bi], bG = out[bi + 1], bB = out[bi + 2];
  // Flat interior pixel (y=H/4, deep inside the white region; all neighbours white).
  const uint32_t fx = W / 2, fy = H / 4;
  size_t fpi = ((size_t)fy * W + fx) * 4;
  int fR = out[fpi], fG = out[fpi + 1], fB = out[fpi + 2];

  bool borderBright   = (bR + bG + bB) > 150;  // injectBug Strength=0 -> 0 -> fails
  bool interiorDark   = (fR + fG + fB) < 30;
  bool pass = borderBright && interiorDark;
  printf("[selftest-detectedges] border(R=%d,G=%d,B=%d) interior(R=%d,G=%d,B=%d) -> "
         "borderBright=%d interiorDark=%d -> %s\n",
         bR, bG, bB, fR, fG, fB, borderBright ? 1 : 0, interiorDark ? 1 : 0,
         pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
