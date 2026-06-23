// Grain image-filter texture op (Lane B Phase-C fan-out) — animated colour-noise grain.
// TiXL authority: external/tixl Operators/Lib/image/generate/noise/Grain.cs (ports) + Grain.t3
// (defaults + routing) + Assets/shaders/img/generate/Grain.hlsl (the single-pass kernel: animated
// hash noise added to the source rgb; Scale>1 quantises into blocks).
//
// BACKWARD-TRACE (Cut58 discipline): Grain.t3 uses _ImageFxShaderSetupStatic (source
// "Lib:shaders/img/generate/Grain.hlsl") — the simple static-shader setup, NOT a _multiImageFxSetup
// FloatsToBuffer compound, so the b0 cbuffer (Amount/Color/Exponent/Brightness/Time/Scale) is filled
// 1:1 from the op's float inputs. The only host-side composition is Time = Animate*time + RandomPhase
// (TiXL feeds the setup's time slot); we expose Animate/RandomPhase as ports for parity and fill Time
// host-side from them (selftest passes Time=0 -> deterministic). Grain.t3 defaults: Amount 0.05 /
// Color 0 / Exponent 1 / Brightness 0 / Animate 5 / RandomPhase 0 / Scale 0.
//
// Single-pass port: cookGrain reads c.inputTexture, runs one fullscreen pass of grain_vs/grain_fs,
// writes c.output. Binds b0 = GrainParams, b1 = GrainResolution (the Scale>1 per-pixel step).
//
// FORK (named): host supplies the animation phase Time (= Animate * cook-time + RandomPhase). In a
// headless cook with no global clock the time term is 0 by default -> static grain (parity-safe; the
// golden pins Amount=0 so the noise term vanishes entirely and the result is exact passthrough).
// Sampler: fixed linear+clamp.
//
// Self-contained leaf: cookGrain + ImageFilterOp registrar + runGrainSelfTest. New shader grain.metal
// + grain_params.h. Shares the PSO+scratch cache seam (tex_op_cache.h) with Blur/ChromaB/etc.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>  // std::abs(int) in the golden tolerance check
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/grain_params.h"  // GrainParams, GrainResolution, GRAIN_Params/Resolution
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"   // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"  // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

int runGrainSelfTest(bool injectBug);

namespace {

// Grain texture op: single pass. Reads c.inputTexture, writes c.output. No upstream texture wired:
// clear output to black (image-filter rule — Grain is a single-input filter, not a pure generator).
void cookGrain(TexCookCtx& c) {
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

  MTL::RenderPipelineState* rps = cachedTexPSO(c.dev, c.lib, "grain_vs", "grain_fs", fmt);
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp (see grain.metal)
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL Grain.t3 defaults. Time = Animate * cook-time + RandomPhase; with no headless clock the
  // time term is 0 -> Time defaults to RandomPhase (0). (FORK, named — see header.)
  GrainParams p{};
  p.Amount     = cookParam(c, "Amount", 0.05f);
  p.Color      = cookParam(c, "Color", 0.0f);
  p.Exponent   = cookParam(c, "Exponent", 1.0f);
  p.Brightness = cookParam(c, "Brightness", 0.0f);
  p.Time       = cookParam(c, "RandomPhase", 0.0f);  // animation phase; Animate*time term = 0 headless
  p.Scale      = cookParam(c, "Scale", 0.0f);

  GrainResolution res{};
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
  enc->setFragmentBytes(&p,   sizeof(GrainParams),     GRAIN_Params);
  enc->setFragmentBytes(&res, sizeof(GrainResolution), GRAIN_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

// Self-registration. Ports mirror Grain.cs (Image/Amount/Color/Exponent/Brightness/Animate/
// RandomPhase/Scale) with Grain.t3 defaults; cbuffer maps 1:1 onto Grain.hlsl b0 (Time host-fed).
static const ImageFilterOp _reg_grain{
    {"Grain", "Grain",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Amount (float): noise gain added to source rgb; Grain.t3 default 0.05.
      {"Amount", "Amount", "Float", true, 0.05f, 0.0f, 1.0f, Widget::Slider},
      // Color (float 0..1): 0 = grayscale grain, 1 = per-channel colour grain; default 0.
      {"Color", "Color", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider},
      // Exponent (float): signed contrast curve on the noise; default 1.
      {"Exponent", "Exponent", "Float", true, 1.0f, 0.0f, 8.0f},
      // Brightness (float): additive bias on the noise; default 0.
      {"Brightness", "Brightness", "Float", true, 0.0f, -1.0f, 1.0f},
      // Animate (float): time multiplier for the animation phase; Grain.t3 default 5.
      {"Animate", "Animate", "Float", true, 5.0f, 0.0f, 100.0f},
      // RandomPhase (float): static phase offset added to Time; default 0.
      {"RandomPhase", "RandomPhase", "Float", true, 0.0f, 0.0f, 1000.0f},
      // Scale (float): >1 quantises the grain into pixel blocks; default 0 (per-pixel grain).
      {"Scale", "Scale", "Float", true, 0.0f, 0.0f, 64.0f},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "Grain", cookGrain, "grain", runGrainSelfTest};

// --- Grain MATH golden ------------------------------------------------------------------------
// Grain.hlsl psMain (Scale<=1 branch, the default since Scale.t3 default = 0):
//   float4 noise = GetNoiseFromRandom(uv);
//   return float4(orgColor.rgb + noise.rgb * Amount, 1);
// Closed-form d=0 plateau: with Amount = 0 the noise term vanishes EXACTLY (noise.rgb * 0 = 0) for
// every pixel regardless of Time / hash / Color / Exponent / Brightness, so the output is an exact
// passthrough of the source RGB with ALPHA forced to 1:
//   out.rgb == src.rgb  (byte-exact, no fwidth/smoothstep, fully deterministic)
//   out.a   == 255      (the kernel writes constant alpha = 1)
// We assert this on three independent plateau pixels (mid-gray fill / pure red / pure blue) — the
// passthrough must hold at any colour. Linear sampling at texel centres returns the exact texel.
//   injectBug: cook with Amount = 0.5 instead of 0 -> the noise term is now added -> the mid-gray
//   pixel's rgb drifts off its source value (noise is non-zero almost everywhere) -> the byte-exact
//   passthrough assertion FAILS (teeth). (The grain at a fixed uv with Time=0 is deterministic, so
//   the bug reliably perturbs the probed pixel.)
int runGrainSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-grain] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // Source: three horizontal bands — top mid-gray (128), middle pure red, bottom pure blue.
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      uint8_t r, g, b;
      if (y < H / 3)            { r = 128; g = 128; b = 128; }  // mid-gray
      else if (y < 2 * H / 3)   { r = 255; g = 0;   b = 0;   }  // red
      else                      { r = 0;   g = 0;   b = 255; }  // blue
      in[i] = r; in[i + 1] = g; in[i + 2] = b; in[i + 3] = 255;
    }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  std::map<std::string, float> params;
  params["Amount"]      = injectBug ? 0.5f : 0.0f;  // 0 -> exact passthrough; bug 0.5 -> noise added
  params["Color"]       = 0.0f;
  params["Exponent"]    = 1.0f;
  params["Brightness"]  = 0.0f;
  params["Animate"]     = 5.0f;
  params["RandomPhase"] = 0.0f;  // Time = 0 -> deterministic
  params["Scale"]       = 0.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookGrain(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // Probe one pixel deep in each band.
  auto probe = [&](uint32_t y) -> size_t { return ((size_t)y * W + W / 2) * 4; };
  size_t gi = probe(H / 6);          // mid-gray band
  size_t ri = probe(H / 2);          // red band
  size_t bi = probe(5 * H / 6);      // blue band

  // Passthrough: rgb byte-exact, alpha 255. (Tolerance 1 LSB for sampler rounding; with Amount=0
  // and texel-centre sampling this is exact, but allow ±1 for robustness across GPUs.)
  auto eq = [](int a, int b) { return std::abs(a - b) <= 1; };
  bool grayOk = eq(out[gi], 128) && eq(out[gi+1], 128) && eq(out[gi+2], 128) && out[gi+3] == 255;
  bool redOk  = eq(out[ri], 255) && eq(out[ri+1], 0)   && eq(out[ri+2], 0)   && out[ri+3] == 255;
  bool blueOk = eq(out[bi], 0)   && eq(out[bi+1], 0)   && eq(out[bi+2], 255) && out[bi+3] == 255;

  bool pass = grayOk && redOk && blueOk;
  printf("[selftest-grain] gray(%d,%d,%d,%d) red(%d,%d,%d,%d) blue(%d,%d,%d,%d) -> %s\n",
         out[gi], out[gi+1], out[gi+2], out[gi+3],
         out[ri], out[ri+1], out[ri+2], out[ri+3],
         out[bi], out[bi+1], out[bi+2], out[bi+3], pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
