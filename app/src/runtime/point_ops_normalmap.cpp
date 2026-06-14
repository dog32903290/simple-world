// NormalMap image-filter texture op (lane image_filter) — finite-difference gradient -> normal.
// TiXL authority: external/tixl Operators/Lib/Assets/shaders/img/fx/NormalMap.hlsl (NO .cs — port
// authority = the .hlsl cbuffer b0). Per-pixel: read ±SampleRadius neighbours, build a gradient d,
// encode into a tangent-space normal (Mode<0.5 = RGB with flipped Y, the default).
//
// Single-pass port: cookNormalMap reads c.inputTexture (upstream Texture2D), runs one fullscreen
// pass of normalmap_vs/normalmap_fs, writes c.output. Binds b0 = NormalMapParams. No upstream
// texture wired: clear output to black.
//
// FORK (named): b1 TimeConstants unused -> not bound; b2 Resolution replaced by texture-own dims
// read in-shader (same as DetectEdges). See normalmap.metal / normalmap_params.h.
//
// Self-contained leaf: cookNormalMap + registerNormalMapOp() + runNormalMapSelfTest.
// Shares the D2-2 PSO+scratch cache seam (tex_op_cache.h) with Blur/Displace/DetectEdges/etc.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/normalmap_params.h"  // NormalMapParams, NORMALMAP_Params
#include "runtime/point_graph.h"       // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"      // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

void cookNormalMap(TexCookCtx& c) {
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
      cachedTexPSO(c.dev, c.lib, "normalmap_vs", "normalmap_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp (see .metal)
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL NormalMap.hlsl ParamConstants (b0). No .t3/.cs defaults file -> defaults match the
  // .hlsl's neutral expectations (Impact 1, SampleRadius 1, Twist 0, Mode 0 = RGB flipped-Y).
  NormalMapParams p{};
  p.Impact       = cookParam(c, "Impact", 1.0f);
  p.SampleRadius = cookParam(c, "SampleRadius", 1.0f);
  p.Twist        = cookParam(c, "Twist", 0.0f);
  p.Mode         = cookParam(c, "Mode", 0.0f);

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
  enc->setFragmentBytes(&p, sizeof(NormalMapParams), NORMALMAP_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

void registerNormalMapOp() { registerTexOp("NormalMap", cookNormalMap); }

// --- NormalMap MATH golden --------------------------------------------------------------------
// Source: vertical step edge — left half BLACK (x < W/2), right half WHITE. There is a strong
// horizontal (X) intensity gradient at the seam and zero gradient deep inside either half.
// With Mode=0 (default): the seam pixel has d.x = (right neighbour - left neighbour) != 0, so the
// normal tilts in X -> the encoded R channel (normal.x/2+0.5) departs from the flat value 0.5.
// A flat interior pixel sees d≈0 -> normal=(0,0,1) -> encoded RGB=(0.5,0.5,1.0) -> R≈0.5.
// Assert: seam R departs from 128 (|R-128|>20) AND flat-interior R stays near 128 (|R-128|<=12).
// injectBug Impact=0 -> normalize((0,0,1))=(0,0,1) everywhere -> seam R also ≈128 -> the
// "seam tilts" assertion FAILS (teeth) — a real degeneracy, not a flipped assertion.
int runNormalMapSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-normalmap] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // Source: left half black, right half white (vertical step edge at x=W/2).
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      uint8_t v = (x < W / 2) ? 0 : 255;
      in[i] = v; in[i + 1] = v; in[i + 2] = v; in[i + 3] = 255;
    }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  std::map<std::string, float> params;
  params["Impact"]       = injectBug ? 0.0f : 4.0f;  // bug: no tilt -> normal=(0,0,1) -> R=128
  params["SampleRadius"] = 1.0f;
  params["Twist"]        = 0.0f;
  params["Mode"]         = 0.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookNormalMap(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // Seam pixel at the step (x=W/2, y=H/2) — neighbours straddle the black/white edge.
  const uint32_t sx = W / 2, sy = H / 2;
  size_t si = ((size_t)sy * W + sx) * 4;
  int seamR = out[si];
  // Flat interior pixel (deep in the white half, all neighbours white) -> d≈0.
  const uint32_t fx = W * 3 / 4, fy = H / 2;
  size_t fi = ((size_t)fy * W + fx) * 4;
  int flatR = out[fi];

  bool seamTilts  = std::abs(seamR - 128) > 20;  // injectBug Impact=0 -> ~128 -> fails
  bool flatNeutral = std::abs(flatR - 128) <= 12;
  bool pass = seamTilts && flatNeutral;
  printf("[selftest-normalmap] seamR=%d flatR=%d -> seamTilts=%d flatNeutral=%d -> %s\n",
         seamR, flatR, seamTilts ? 1 : 0, flatNeutral ? 1 : 0, pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
