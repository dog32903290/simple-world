// EdgeRepeat image-filter texture op (lane image_filter) — mirror-line repeat / kaleidoscope fold.
// TiXL authority: Operators/Lib/image/fx/distort/EdgeRepeat.cs (Image/Fill/Background/LineColor/
// Center/Width/Rotation/LineThickness/Resolution inputs) + EdgeRepeat.t3 (defaults Fill=(1,1,1,1),
// Background=(1,.99999,.99999,.804), LineColor=(1,1,1,1), Center=(0,0), Width=0.25, Rotation=45,
// LineThickness=0, Wrap="Mirror", BlendMode=4) + Assets/shaders/img/fx/EdgeRepeat.hlsl (single-pass
// mirror fold across a rotated line through Center; out-of-band region reflects and is Background-
// tinted, a LineColor fold line is drawn at the band edge).
//
// Single-pass port: cookEdgeRepeat reads c.inputTexture, runs one fullscreen pass of
// edgerepeat_vs/_fs, writes c.output. Two constant buffers: b0 = EdgeRepeatParams (Fill/Background/
// LineColor/Center/Width/Rotation/LineThickness — field order verbatim from the .hlsl), and a
// resolution buffer (TargetWidth/TargetHeight for aspect). The sampler uses MIRROR wrap (TiXL .t3
// Wrap="Mirror", load-bearing for the fold).
//
// Self-contained leaf: cookEdgeRepeat + registerEdgeRepeatOp() + runEdgeRepeatSelfTest.
// Shares the tex_op_cache.h PSO+scratch seam with Blur/Displace/Tint/ChromaB/PolarCoordinates.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/edgerepeat_params.h"  // EdgeRepeatParams/Resolution, EDGEREPEAT_*
#include "runtime/eval_context.h"
#include "runtime/point_graph.h"        // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"       // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// EdgeRepeat texture op: single pass. Reads c.inputTexture, writes c.output.
// No upstream texture wired: clear output to black.
void cookEdgeRepeat(TexCookCtx& c) {
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
      cachedTexPSO(c.dev, c.lib, "edgerepeat_vs", "edgerepeat_fs", fmt);
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  // fork[mirror-sampler]: TiXL EdgeRepeat.t3 Wrap="Mirror" (load-bearing — the fold relies on the
  // sampler mirroring UVs outside [0,1], NOT clamping). Use MTL MirrorRepeat on both axes.
  sd->setSAddressMode(MTL::SamplerAddressModeMirrorRepeat);
  sd->setTAddressMode(MTL::SamplerAddressModeMirrorRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL params: EdgeRepeat.t3 defaults. Field order mirrors the .hlsl cbuffer (b0) verbatim.
  EdgeRepeatParams p{};
  p.Fill[0]       = cookParam(c, "Fill.r", 1.0f);
  p.Fill[1]       = cookParam(c, "Fill.g", 1.0f);
  p.Fill[2]       = cookParam(c, "Fill.b", 1.0f);
  p.Fill[3]       = cookParam(c, "Fill.a", 1.0f);
  p.Background[0] = cookParam(c, "Background.r", 1.0f);
  p.Background[1] = cookParam(c, "Background.g", 0.99999f);
  p.Background[2] = cookParam(c, "Background.b", 0.99999f);
  p.Background[3] = cookParam(c, "Background.a", 0.804f);
  p.LineColor[0]  = cookParam(c, "LineColor.r", 1.0f);
  p.LineColor[1]  = cookParam(c, "LineColor.g", 1.0f);
  p.LineColor[2]  = cookParam(c, "LineColor.b", 1.0f);
  p.LineColor[3]  = cookParam(c, "LineColor.a", 1.0f);
  p.Center[0]     = cookParam(c, "Center.x", 0.0f);
  p.Center[1]     = cookParam(c, "Center.y", 0.0f);
  p.Width         = cookParam(c, "Width", 0.25f);
  p.Rotation      = cookParam(c, "Rotation", 45.0f);
  p.LineThickness = cookParam(c, "LineThickness", 0.0f);

  EdgeRepeatResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

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
  enc->setFragmentBytes(&p,   sizeof(EdgeRepeatParams),     EDGEREPEAT_Params);
  enc->setFragmentBytes(&res, sizeof(EdgeRepeatResolution), EDGEREPEAT_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

void registerEdgeRepeatOp() { registerTexOp("EdgeRepeat", cookEdgeRepeat); }

// --- EdgeRepeat MATH golden -------------------------------------------------------------------
// EdgeRepeat folds the image across a rotated line: points whose signed distance along the line
// normal exceeds the band (dist>1) are MIRRORED back and tinted with Background, and a LineColor
// fold line is drawn. The fold ONLY fires for out-of-band pixels — if Width is large enough that
// every pixel has dist<=1, nothing folds (output = source * Fill, a near-passthrough).
//
// We run twice on the same source: (a) a folding run with a SMALL Width (band tiny -> most pixels
// fold, the mirror axis is exercised) using a vivid Background color; (b) a no-fold run with a HUGE
// Width (band covers everything -> dist<=1 -> no fold). We assert the two DIFFER substantially:
// the fold both relocates UVs (mirror) and applies the Background tint, so the folded image cannot
// match the near-passthrough. This bites the mirror-axis math (Rotation->angle, dist sign flip,
// dist>1 branch).
//
// injectBug makes the folding run ALSO use the huge Width -> no fold -> identical to the no-fold
// run -> the "differ" assertion FAILS.
int runEdgeRepeatSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-edgerepeat] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src      = dev->newTexture(td);
  MTL::Texture* dst_fold = dev->newTexture(td);  // small Width -> folds (or huge under injectBug)
  MTL::Texture* dst_flat = dev->newTexture(td);  // huge Width -> no fold

  // Source: smooth diagonal gradient so any UV relocation by the fold changes the sampled color.
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      in[i]     = (uint8_t)(x * 255 / (W - 1));        // R ramps left->right
      in[i + 1] = (uint8_t)(y * 255 / (H - 1));        // G ramps top->bottom
      in[i + 2] = (uint8_t)((x + y) * 255 / (W + H));  // B diagonal
      in[i + 3] = 255;
    }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  auto run = [&](MTL::Texture* dst, float width) {
    std::map<std::string, float> pm;
    pm["Fill.r"] = 1.0f; pm["Fill.g"] = 1.0f; pm["Fill.b"] = 1.0f; pm["Fill.a"] = 1.0f;
    // Vivid Background so the folded region is unmistakably re-tinted.
    pm["Background.r"] = 0.0f; pm["Background.g"] = 1.0f; pm["Background.b"] = 0.0f; pm["Background.a"] = 1.0f;
    pm["LineColor.r"] = 1.0f; pm["LineColor.g"] = 1.0f; pm["LineColor.b"] = 1.0f; pm["LineColor.a"] = 1.0f;
    pm["Center.x"] = 0.0f; pm["Center.y"] = 0.0f;
    pm["Width"] = width; pm["Rotation"] = 45.0f; pm["LineThickness"] = 0.0f;
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &pm;
    cookEdgeRepeat(c);
  };

  const float kHugeWidth = 100.0f;   // band covers the whole image -> dist<=1 everywhere -> no fold
  const float kSmallWidth = 0.05f;   // tiny band -> most pixels fold (dist>1)
  run(dst_fold, injectBug ? kHugeWidth : kSmallWidth);
  run(dst_flat, kHugeWidth);

  std::vector<uint8_t> ofold((size_t)W * H * 4, 0), oflat((size_t)W * H * 4, 0);
  dst_fold->getBytes(ofold.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  dst_flat->getBytes(oflat.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  long long totalDiff = 0;
  for (size_t i = 0; i < (size_t)W * H * 4; i += 4) {
    totalDiff += std::abs((int)ofold[i]     - (int)oflat[i]);
    totalDiff += std::abs((int)ofold[i + 1] - (int)oflat[i + 1]);
    totalDiff += std::abs((int)ofold[i + 2] - (int)oflat[i + 2]);
  }
  bool foldChanges = totalDiff > 100000;

  printf("[selftest-edgerepeat] totalRGBdiff(fold vs flat)=%lld -> foldChanges=%d -> %s\n",
         totalDiff, foldChanges ? 1 : 0, foldChanges ? "PASS" : "FAIL");

  src->release(); dst_fold->release(); dst_flat->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return foldChanges ? 0 : 1;
}

}  // namespace sw
