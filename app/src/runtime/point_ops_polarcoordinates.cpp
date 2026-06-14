// PolarCoordinates image-filter texture op (lane image_filter) — geometric cartesian<->polar remap.
// TiXL authority: Operators/Lib/image/fx/distort/PolarCoordinates.cs (Image/Center/Radius/RadialBias/
// RadialOffset/Twist/Stretch/Resolution/Mode inputs; Modes enum {Cartesian2Polar,Polar2Cartesian})
// + PolarCoordinates.t3 (defaults Center=(0,0)/Radius=1/RadialBias=1/RadialOffset=0/Twist=0/
// Stretch=(1,1)/Mode=0) + Assets/shaders/img/fx/PolarCoordinates.hlsl (single-pass bidirectional
// remap: Mode<0.5 wraps rect->polar disc; else unrolls polar->rect).
//
// Single-pass port: cookPolarCoord reads c.inputTexture (upstream RenderTarget's Texture2D), runs
// one fullscreen pass of polarcoordinates_vs/_fs, writes c.output. Two constant buffers:
// b0 = PolarCoordParams (Center/Radius/Mode/RadialBias/RadialOffset/Twist/__padding/Stretch — field
// order verbatim from the .hlsl), b1 = PolarCoordResolution (TargetWidth/TargetHeight for aspect).
//
// Self-contained leaf: cookPolarCoord + registerPolarCoordOp() + runPolarCoordSelfTest.
// Shares the tex_op_cache.h PSO+scratch seam with Blur/Displace/Tint/ChromaB.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/point_graph.h"            // TexCookCtx, cookParam, registerTexOp
#include "runtime/polarcoordinates_params.h"  // PolarCoordParams/Resolution, POLARCOORD_*
#include "runtime/tex_op_cache.h"           // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// PolarCoordinates texture op: single pass. Reads c.inputTexture, writes c.output.
// No upstream texture wired: clear output to black.
void cookPolarCoord(TexCookCtx& c) {
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
      cachedTexPSO(c.dev, c.lib, "polarcoordinates_vs", "polarcoordinates_fs", fmt);
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  // fork[clamp-sampler]: TiXL PolarCoordinates uses _ImageFxShaderSetupStatic with no Wrap input ->
  // host default. We use fixed clamp (same fork class as Blur/Tint/ChromaB).
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL params: PolarCoordinates.t3 defaults. Field order mirrors the .hlsl cbuffer (b0) verbatim.
  PolarCoordParams p{};
  p.Center[0]    = cookParam(c, "Center.x", 0.0f);
  p.Center[1]    = cookParam(c, "Center.y", 0.0f);
  p.Radius       = cookParam(c, "Radius", 1.0f);
  p.Mode         = cookParam(c, "Mode", 0.0f);
  p.RadialBias   = cookParam(c, "RadialBias", 1.0f);
  p.RadialOffset = cookParam(c, "RadialOffset", 0.0f);
  p.Twist        = cookParam(c, "Twist", 0.0f);
  p.__padding    = 0.0f;
  p.Stretch[0]   = cookParam(c, "Stretch.x", 1.0f);
  p.Stretch[1]   = cookParam(c, "Stretch.y", 1.0f);

  PolarCoordResolution res{};
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
  enc->setFragmentBytes(&p,   sizeof(PolarCoordParams),     POLARCOORD_Params);
  enc->setFragmentBytes(&res, sizeof(PolarCoordResolution), POLARCOORD_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

void registerPolarCoordOp() { registerTexOp("PolarCoordinates", cookPolarCoord); }

// --- PolarCoordinates MATH golden -------------------------------------------------------------
// PolarCoordinates is fundamentally a Mode-discriminated remap: Mode<0.5 = Cartesian2Polar wraps a
// rect into a polar disc; Mode>=0.5 = Polar2Cartesian unrolls it. The two modes sample the source at
// genuinely DIFFERENT uvs, so for any non-uniform source the two outputs differ at a chosen pixel.
//
// We run the op twice on the same source into two textures — once Mode=0, once Mode=1 — then assert
// the results DIFFER at a sampled off-axis pixel (the Mode discriminant actually branches). The
// source is a 4-quadrant color wheel (R/G/B/Y by quadrant) so the angle-dependent remap of
// Cartesian2Polar reorganizes color very differently from the radius-dependent Polar2Cartesian.
//
// injectBug forces BOTH runs to Mode=0 (the discriminant collapses to one branch) -> identical
// outputs -> the "differ" assertion FAILS (teeth on the Mode-discriminant reverse: if the < 0.5
// branch test were inverted, the two runs would also collapse).
int runPolarCoordSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-polarcoord] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src      = dev->newTexture(td);
  MTL::Texture* dst_c2p  = dev->newTexture(td);  // Mode=0 Cartesian2Polar
  MTL::Texture* dst_p2c  = dev->newTexture(td);  // Mode=1 Polar2Cartesian (or Mode=0 under injectBug)

  // Source: 4-quadrant color wheel — TL red, TR green, BL blue, BR yellow.
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      bool right = (x >= W / 2);
      bool bottom = (y >= H / 2);
      uint8_t r = 0, g = 0, b = 0;
      if (!right && !bottom) { r = 255; }                 // TL red
      else if (right && !bottom) { g = 255; }             // TR green
      else if (!right && bottom) { b = 255; }             // BL blue
      else { r = 255; g = 255; }                          // BR yellow
      in[i] = r; in[i + 1] = g; in[i + 2] = b; in[i + 3] = 255;
    }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  auto run = [&](MTL::Texture* dst, float mode) {
    std::map<std::string, float> pm;
    pm["Center.x"] = 0.0f; pm["Center.y"] = 0.0f;
    pm["Radius"] = 1.0f; pm["Mode"] = mode;
    pm["RadialBias"] = 1.0f; pm["RadialOffset"] = 0.0f; pm["Twist"] = 0.0f;
    pm["Stretch.x"] = 1.0f; pm["Stretch.y"] = 1.0f;
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &pm;
    cookPolarCoord(c);
  };

  run(dst_c2p, 0.0f);                          // Cartesian2Polar
  run(dst_p2c, injectBug ? 0.0f : 1.0f);       // bug: same mode as the first run

  std::vector<uint8_t> oc2p((size_t)W * H * 4, 0), op2c((size_t)W * H * 4, 0);
  dst_c2p->getBytes(oc2p.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  dst_p2c->getBytes(op2c.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // Sum absolute RGB difference across the whole image: the two modes must produce different images.
  long long totalDiff = 0;
  for (size_t i = 0; i < (size_t)W * H * 4; i += 4) {
    totalDiff += std::abs((int)oc2p[i]     - (int)op2c[i]);
    totalDiff += std::abs((int)oc2p[i + 1] - (int)op2c[i + 1]);
    totalDiff += std::abs((int)oc2p[i + 2] - (int)op2c[i + 2]);
  }
  // Threshold: a large fraction of pixels must change between modes. Pick a conservative floor.
  bool modesDiffer = totalDiff > 100000;

  printf("[selftest-polarcoord] totalRGBdiff(c2p vs p2c)=%lld -> modesDiffer=%d -> %s\n",
         totalDiff, modesDiffer ? 1 : 0, modesDiffer ? "PASS" : "FAIL");

  src->release(); dst_c2p->release(); dst_p2c->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return modesDiffer ? 0 : 1;
}

}  // namespace sw
