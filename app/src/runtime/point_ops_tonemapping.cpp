// ToneMapping image-filter texture op (lane image_filter).
// TiXL authority: external/tixl/Operators/Lib/image/color/ToneMapping.cs (ports) +
// external/tixl/Operators/Lib/Assets/shaders/img/fx/ToneMap.hlsl (kernel).
//
// Single-pass port: cookToneMapping reads c.inputTexture (upstream Texture2D via gather
// direct-through), runs one fullscreen pass of tonemapping_vs/tonemapping_fs, writes c.output.
// No upstream texture: clear output to black.
//
// Inputs (from ToneMapping.cs verbatim, 5 inputs + 1 output):
//   Texture2d (Texture2D in)
//   Mode      (int, MappedType enum: 0=Aces 1=Reinhard 2=Filmic 3=Uncharted2 4=AgX 5=AgX_Punchy 6=None)
//   CorrectGamma (bool)
//   Gamma     (float, default 2.2)
//   Exposure  (float, default 1.0)
//
// Forks (named):
// 1. DX11 PS pipeline -> Metal fullscreen triangle VS+FS (same as Tint/ChannelMixer fork class).
// 2. Mode enum: TiXL stores as int on host, passes to shader as float in cbuffer b0.
//    We read it via cookParam as float (same as _ForceKind pattern in particle ops).
// 3. TiXL bug (verbatim, MUST NOT be fixed): ToneMap.hlsl:105 uses 'Mode<4.5' instead of 5.5,
//    making AgX_Punchy (Mode=5) unreachable. We clone the dead branch faithfully. See shader.
// 4. HLSL mul(vec, matrix) row-major -> Metal matrix column layout (transposed in shader).
//    See tonemapping.metal inline comments.
// 5. Fixed linear+clamp sampler — TiXL's host sampler wrapping knobs not exposed.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/tonemapping_params.h"  // ToneMappingParams, TONEMAPPING_Params
#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/point_graph.h"    // TexCookCtx, cookParam, registerTexOp
#include "runtime/resident_eval_graph.h"
#include "runtime/tex_op_cache.h"   // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// ToneMapping texture op: single pass. Reads c.inputTexture, writes c.output.
// No upstream texture wired: clear output to black.
void cookToneMapping(TexCookCtx& c) {
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
      cachedTexPSO(c.dev, c.lib, "tonemapping_vs", "tonemapping_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL ToneMapping.cs input defaults (from ToneMapping.cs and ToneMap.hlsl context):
  //   Mode = 0 (Aces), CorrectGamma = false, Gamma = 2.2, Exposure = 1.0
  ToneMappingParams p{};
  // TiXL ToneMapping.cs: InputSlot<int> Mode (MappedType enum), default 0 = Aces
  p.Mode         = cookParam(c, "Mode", 0.0f);
  // TiXL ToneMapping.cs: InputSlot<bool> CorrectGamma, default false
  p.CorrectGamma = cookParam(c, "CorrectGamma", 0.0f);
  // TiXL ToneMapping.cs: InputSlot<float> Gamma, default 2.2
  p.GammaValue   = cookParam(c, "Gamma", 2.2f);
  // TiXL ToneMapping.cs: InputSlot<float> Exposure, default 1.0
  p.Exposure     = cookParam(c, "Exposure", 1.0f);

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
  enc->setFragmentBytes(&p, sizeof(ToneMappingParams), TONEMAPPING_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

void registerToneMappingOp() { registerTexOp("ToneMapping", cookToneMapping); }

// --- ToneMapping MATH golden ----------------------------------------------------------------
// Fill a 64x64 source texture with bright HDR input (rgb = 4.0 normalised to RGBA8 = 255,
// i.e. solid white at 4× over-exposure). Using a RGBA16Float texture so the GPU sees values
// above 1.0 without clamping at ingest.
//
// Green path (injectBug=false):
//   Mode=1 (Reinhard), Exposure=1.0, CorrectGamma=false.
//   Reinhard: out = c/(c+1) = 4/(4+1) = 0.8.
//   Output rgb should all be < 1.0 (compression worked). We assert center pixel float rgb < 0.95.
//   (RGBA16Float readback as float, so precision is good.)
//
// Red path (injectBug=true):
//   Force Mode=6 (None) -> no tone compression applied -> output rgb stays at Exposure*input = 4.0.
//   We readback the texture: float rgb values should be >= 1.0 (or clamped to 1.0 by the format).
//   We assert rgb >= 0.95 (NOT compressed) — the assertion FAILS in the green path (teeth) because
//   Reinhard DID compress, making this a genuine degenerate injectBug path (not a flipped assertion).
//
// Note: we use RGBA16Float as the render target to carry HDR values >1.0. The green path proves
// Reinhard brings them back below 1.0; the red path proves no-tonemap leaves them at 1.0 (clamped
// by float16 to exactly 1.0 since 4.0 is representable, but uncompressed by Reinhard).
int runToneMappingSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-tonemapping] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Source texture: RGBA16Float, solid HDR white at 4.0 (over-exposed).
  MTL::TextureDescriptor* srcTd =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA16Float, W, H, false);
  srcTd->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  srcTd->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(srcTd);
  MTL::Texture* dst = dev->newTexture(srcTd);

  // Fill source with HDR white (4.0 per channel) using a clear pass.
  {
    MTL::RenderPassDescriptor* rpd = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = rpd->colorAttachments()->object(0);
    ca->setTexture(src);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor::Make(4.0, 4.0, 4.0, 1.0));
    ca->setStoreAction(MTL::StoreActionStore);
    MTL::CommandBuffer* cmd = q->commandBuffer();
    cmd->renderCommandEncoder(rpd)->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
  }

  // injectBug=true: force Mode=6 (None) to skip tone compression -> output stays HDR (>=1.0).
  // injectBug=false: Mode=1 (Reinhard) -> 4/(4+1)=0.8 -> compressed below 1.0.
  std::map<std::string, float> params;
  params["Mode"]         = injectBug ? 6.0f : 1.0f;  // 6=None (no-op) vs 1=Reinhard
  params["CorrectGamma"] = 0.0f;
  params["Gamma"]        = 2.2f;
  params["Exposure"]     = 1.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookToneMapping(c);

  // Readback center pixel as float16 -> interpret as float.
  // RGBA16Float: 2 bytes per channel, 8 bytes per pixel.
  std::vector<uint16_t> outBuf((size_t)W * H * 4, 0);
  dst->getBytes(outBuf.data(), W * 8, MTL::Region::Make2D(0, 0, W, H), 0);
  const uint32_t cx = W / 2, cy = H / 2;
  size_t idx = ((size_t)cy * W + cx) * 4;

  // Convert float16 half to float using bit manipulation.
  auto halfToFloat = [](uint16_t h) -> float {
    uint32_t sign = (h >> 15) & 0x1;
    uint32_t exp  = (h >> 10) & 0x1f;
    uint32_t mant = h & 0x3ff;
    uint32_t bits;
    if (exp == 0) {
      if (mant == 0) { bits = sign << 31; }
      else {
        exp = 1;
        while (!(mant & 0x400)) { mant <<= 1; exp--; }
        mant &= 0x3ff;
        bits = (sign << 31) | ((exp + 127 - 15) << 23) | (mant << 13);
      }
    } else if (exp == 31) {
      bits = (sign << 31) | 0x7f800000 | (mant << 13);
    } else {
      bits = (sign << 31) | ((exp + 127 - 15) << 23) | (mant << 13);
    }
    float f; __builtin_memcpy(&f, &bits, 4);
    return f;
  };

  float R = halfToFloat(outBuf[idx + 0]);
  float G = halfToFloat(outBuf[idx + 1]);
  float B = halfToFloat(outBuf[idx + 2]);

  // Assertion: rgb must all be < 0.95 (Reinhard compressed 4.0 -> 0.8).
  // Green path (Reinhard): 4/(4+1) = 0.8 < 0.95 -> compressed=true -> PASS.
  // Red path  (None, injectBug): output stays at Exposure*4.0=4.0 (RGBA16Float holds HDR)
  //   -> R=4.0 >= 0.95 -> compressed=false -> FAIL (real degenerate, not a flipped assertion).
  // The SAME assertion drives both paths; injectBug produces a genuine physics failure.
  bool compressed = (R < 0.95f) && (G < 0.95f) && (B < 0.95f);
  printf("[selftest-tonemapping] center R=%.3f G=%.3f B=%.3f Mode=%s compressed=%d -> %s\n",
         R, G, B,
         injectBug ? "None(6)" : "Reinhard(1)",
         compressed ? 1 : 0,
         compressed ? "PASS" : "FAIL");

  bool allPass = compressed;

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return allPass ? 0 : 1;
}

}  // namespace sw
