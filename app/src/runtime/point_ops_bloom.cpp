// Bloom image-filter texture op — THE multi-pass-executor seam (Texture2D in -> Texture2D out).
// Authority (ported 1:1): external/tixl Operators/Lib/image/fx/_/_ExecuteBloomPasses.cs (the real
// pass executor — the .cs/.t3 wrapper just wires it) + Operators/Lib/Assets/shaders/img/blur/
// Bloom-{Brightpass,Downsample,SeparableBlur,Copy,Upsample}PS.hlsl (the five per-pass shaders) +
// Bloom.cs:10-35 / Bloom.t3 (the OUTER public ports + defaults). Mirrors point_ops_blur.cpp's
// 2-pass-into-cached-scratch idiom, scaled to 4N+2 passes over N scratch targets.
//
// Pass sequence (Levels = clamp(MaxLevels,1,10), total 4N+2):
//   1.  Brightpass:            source         -> bloom.bright (full res)
//   2.  per level i=0..N-1:    Downsample lastSrv -> bloom.A.i (dims max(1,d/2) per level),
//                              BlurV  bloom.A.i -> bloom.B.i,  BlurH bloom.B.i -> bloom.A.i;
//                              lastSrv = bloom.A.i (feeds next level's downsample).
//   3.  Copy base:             source (point) -> bloom.composite
//   4.  per level i=N-1..0:    Upsample-add (ADDITIVE blend One,One) bloom.A.i -> bloom.composite
//   Output = bloom.composite.
//
// New vs blur (the executor seam's load-bearing bits):
//   - Additive-blend PSO variant (cachedTexPSOBlendAdd) for the upsample-add passes.
//   - N scratch targets via cachedScratchTex with DISTINCT stable keys (bloom.bright/composite/
//     A.i/B.i) — frame-stable, reused (NOT in-cook newTexture). Format = source/output pixel format
//     (NOT hardcoded RGBA8). dims max(1,d/2) per level.
//   - CalculateDistribution (host-side bisection root-find of ApplyGainAndBias, _ExecuteBloomPasses
//     .cs:715-801) -> per-level normalized intensity weights.
//
// Cut55 trap respected: Bloom's .t3 is a compound whose output comes from the _ExecuteBloomPasses
// child; we collapse the whole pyramid into ONE Bloom op with Bloom.cs:10-35 ports + OUTER defaults,
// NOT replicating the internal shader-child wiring.
//
// R-2 (auto-route): Bloom is one Texture2D in + one out, no special ports, so registerTexOp("Bloom",
// cookBloom) auto-routes through the standard cookTexNode branch on BOTH flat and resident.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/bloom_params.h"               // BloomThresholdParams/BlurParams/CompositeParams
#include "runtime/eval_context.h"               // EvaluationContext (chain selftest builds one)
#include "runtime/graph.h"                       // Graph/Node/pinId
#include "runtime/graph_bridge.h"                // libFromGraph (resident selftest leg)
#include "runtime/image_filter_op_registry.h"    // ImageFilterOp self-registration
#include "runtime/point_graph.h"                 // TexCookCtx, cookParam, registerTexOp
#include "runtime/resident_eval_graph.h"         // buildEvalGraph (resident selftest leg)
#include "runtime/tex_op_cache.h"                // cachedTexPSO/cachedTexPSOBlendAdd/cachedScratchTex

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// --- CalculateDistribution host-side (verbatim from _ExecuteBloomPasses.cs:715-801 + the
// MathUtils.ApplyGainAndBias/GetBias/GetSchlickBias chain, MathUtils.cs:44-88). Runs on dim/levels
// change only (cheap: <=10 bisections). Normalizes the per-level frequency weights to sum 1.0.

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// MathUtils.cs:44-46
inline float getBias(float b, float x) { return x / (((1.0f / b - 2.0f) * (1.0f - x)) + 1.0f); }

// MathUtils.cs:50-63
inline float getSchlickBias(float g, float x) {
  if (x < 0.5f) {
    x *= 2.0f;
    x = 0.5f * getBias(g, x);
  } else {
    x = 2.0f * x - 1.0f;
    x = 0.5f * getBias(1.0f - g, x) + 0.5f;
  }
  return x;
}

// MathUtils.cs:65-88
inline float applyGainAndBias(float value, float gain, float bias) {
  float b = clampf(bias, 0.0f, 1.0f);
  float g = clampf(gain, 0.0f, 1.0f);
  if (value > 0.999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) {
    value = getBias(b, value);
    value = getSchlickBias(g, value);
  } else {
    value = getSchlickBias(g, value);
    value = getBias(b, value);
  }
  return value;
}

// _ExecuteBloomPasses.cs:745-801 — bisection root finder for a monotonic fn on [0,1].
bool tryFindRootBisection(float gain, float bias, float yTarget, float& result) {
  result = 0.0f;
  const float tolerance = 0.001f;
  const int maxIterations = 20;
  float lowerBound = 0.0f, upperBound = 1.0f;
  auto fn = [&](float x) { return applyGainAndBias(x, gain, bias) - yTarget; };

  float fLower = fn(lowerBound);
  if (std::fabs(fLower) < tolerance) { result = lowerBound; return true; }
  float fUpper = fn(upperBound);
  if (std::fabs(fUpper) < tolerance) { result = upperBound; return true; }

  // Bracketing check: same sign at both ends -> yTarget outside actual numerical range.
  auto sign = [](float v) { return (v > 0.0f) - (v < 0.0f); };
  if (sign(fLower) == sign(fUpper)) return false;

  for (int i = 0; i < maxIterations; ++i) {
    float mid = lowerBound + 0.5f * (upperBound - lowerBound);
    float fMid = fn(mid);
    if (std::fabs(fMid) < tolerance) { result = mid; return true; }
    if (sign(fMid) == sign(fLower)) {
      lowerBound = mid;
      fLower = fMid;
    } else {
      upperBound = mid;
    }
  }
  return false;  // max iterations
}

// _ExecuteBloomPasses.cs:715-738 — distribution[k] = (root(k+1/N) - root(k/N)), normalized to sum 1.
void calculateDistribution(float gain, float bias, int bucketCount, std::vector<float>& out) {
  out.clear();
  if (bucketCount <= 0) return;
  float g = clampf(gain, 0.002f, 0.95f);
  float b = clampf(bias, 0.002f, 0.95f);
  float last = 0.0f;
  for (int k = 1; k < bucketCount; ++k) {
    float yTarget = (float)k / (float)bucketCount;
    float r;
    if (!tryFindRootBisection(g, b, yTarget, r)) r = (float)k / (float)bucketCount;
    r = clampf(r, 0.0f, 1.0f);
    out.push_back(r - last);
    last = r;
  }
  out.push_back(1.0f - last);
}

// One fullscreen pass with the given (non-additive) PSO + cbuffer. Clears the target. Mirrors
// blurPass (point_ops_blur.cpp). cbuffer may be null (Downsample/Copy take no constants).
void bloomPass(MTL::CommandQueue* q, MTL::RenderPipelineState* rps, MTL::SamplerState* samp,
               MTL::Texture* src, MTL::Texture* dst, const void* cbuf, size_t cbufLen, int cbufSlot) {
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(dst);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(src, 0);
  enc->setFragmentSamplerState(samp, 0);
  if (cbuf) enc->setFragmentBytes(cbuf, cbufLen, cbufSlot);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

// One ADDITIVE upsample-add pass: LOAD (not clear) the composite, accumulate src*intensity*color.
void bloomAddPass(MTL::CommandQueue* q, MTL::RenderPipelineState* addRps, MTL::SamplerState* samp,
                  MTL::Texture* src, MTL::Texture* dst, const BloomCompositeParams& p) {
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(dst);
  ca->setLoadAction(MTL::LoadActionLoad);  // accumulate onto the existing composite (additive blend)
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(addRps);
  enc->setFragmentTexture(src, 0);
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(BloomCompositeParams), BLOOM_CompositeParams);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

void clearToBlack(MTL::CommandQueue* q, MTL::Texture* dst) {
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(dst);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = q->commandBuffer();
  cmd->renderCommandEncoder(pass)->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

// Bloom cook: reads c.inputTexture (upstream tex op output, gather direct-through), writes c.output.
void cookBloom(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  // No upstream texture wired: nothing to bloom -> clear output (don't show stale). Mirrors cookBlur.
  if (!c.inputTexture) { clearToBlack(c.queue, c.output); return; }

  const uint32_t W = (uint32_t)c.output->width(), H = (uint32_t)c.output->height();
  if (W == 0 || H == 0) return;

  // Outer Bloom ports/defaults (Bloom.cs:10-35 + Bloom.t3): MaxLevels 10, Intensity 6, Threshold
  // 0.25, GainAndBias (0.5,0.5), ColorWeights Rec.601 (0.299,0.587,0.114), Blur 1.0, Clamp false.
  const int levels = (int)clampf(std::round(cookParam(c, "MaxLevels", 10.0f)), 1.0f, 10.0f);
  const float intensity = cookParam(c, "Intensity", 6.0f);
  const float threshold = cookParam(c, "Threshold", 0.25f);
  const float blurOffset = cookParam(c, "Blur", 1.0f);
  const float cwX = cookParam(c, "ColorWeights.x", 0.299f);
  const float cwY = cookParam(c, "ColorWeights.y", 0.587f);
  const float cwZ = cookParam(c, "ColorWeights.z", 0.114f);
  const float gbGain = cookParam(c, "GainAndBias.x", 0.5f);
  const float gbBias = cookParam(c, "GainAndBias.y", 0.5f);
  const bool clampTex = cookParam(c, "Clamp", 0.0f) > 0.5f;

  // PSOs (cached, device-global). Non-additive for bright/down/blur/copy; additive for upsample-add.
  MTL::RenderPipelineState* psoBright = cachedTexPSO(c.dev, c.lib, "bloom_vs", "bloom_bright_fs", fmt);
  MTL::RenderPipelineState* psoDown = cachedTexPSO(c.dev, c.lib, "bloom_vs", "bloom_down_fs", fmt);
  MTL::RenderPipelineState* psoBlur = cachedTexPSO(c.dev, c.lib, "bloom_vs", "bloom_blur_fs", fmt);
  MTL::RenderPipelineState* psoCopy = cachedTexPSO(c.dev, c.lib, "bloom_vs", "bloom_copy_fs", fmt);
  MTL::RenderPipelineState* psoAdd =
      cachedTexPSOBlendAdd(c.dev, c.lib, "bloom_vs", "bloom_upsample_fs", fmt);
  if (!psoBright || !psoDown || !psoBlur || !psoCopy || !psoAdd) return;

  // Samplers: linear (most passes) + point (Copy base). Clamp-to-edge (= TiXL linear/point states).
  MTL::SamplerDescriptor* sdL = MTL::SamplerDescriptor::alloc()->init();
  sdL->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sdL->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sdL->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sdL->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* sampLinear = c.dev->newSamplerState(sdL);
  sdL->release();
  MTL::SamplerDescriptor* sdP = MTL::SamplerDescriptor::alloc()->init();
  sdP->setMinFilter(MTL::SamplerMinMagFilterNearest);
  sdP->setMagFilter(MTL::SamplerMinMagFilterNearest);
  sdP->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sdP->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* sampPoint = c.dev->newSamplerState(sdP);
  sdP->release();

  // Per-level dims (max(1, d/2) per level) + cached scratch targets (DISTINCT stable keys; format =
  // output format). bright/composite at full res; A.i/B.i at level i dims.
  MTL::Texture* bright = cachedScratchTex(c.dev, fmt, W, H, "bloom.bright");
  MTL::Texture* composite = cachedScratchTex(c.dev, fmt, W, H, "bloom.composite");
  std::vector<MTL::Texture*> A(levels, nullptr), B(levels, nullptr);
  std::vector<uint32_t> lw(levels), lh(levels);
  {
    uint32_t cw = W, ch = H;
    for (int i = 0; i < levels; ++i) {
      cw = std::max(1u, cw / 2);
      ch = std::max(1u, ch / 2);
      lw[i] = cw; lh[i] = ch;
      A[i] = cachedScratchTex(c.dev, fmt, cw, ch, "bloom.A." + std::to_string(i));
      B[i] = cachedScratchTex(c.dev, fmt, cw, ch, "bloom.B." + std::to_string(i));
    }
  }
  if (!bright || !composite) { sampLinear->release(); sampPoint->release(); return; }

  // Per-level normalized intensity weights (host-side bisection, dim/levels-stable in practice).
  std::vector<float> levelIntensities;
  calculateDistribution(gbGain, gbBias, levels, levelIntensities);

  MTL::Texture* srcTex = const_cast<MTL::Texture*>(c.inputTexture);

  // 1. Brightpass: source -> bright (full res).
  {
    BloomThresholdParams tp{};
    tp.ColorWeights[0] = cwX; tp.ColorWeights[1] = cwY; tp.ColorWeights[2] = cwZ;
    tp.Threshold = threshold;
    bloomPass(c.queue, psoBright, sampLinear, srcTex, bright, &tp, sizeof(tp),
              BLOOM_ThresholdParams);
  }

  // 2. Downsample / blur pyramid. lastSrv starts at the bright pass output.
  MTL::Texture* lastSrv = bright;
  for (int i = 0; i < levels; ++i) {
    if (!A[i] || !B[i]) continue;
    // Downsample lastSrv -> A[i] (no cbuffer: shader reads dims via get_width/height).
    bloomPass(c.queue, psoDown, sampLinear, lastSrv, A[i], nullptr, 0, 0);
    // Blur V (A->B): DirY = blurOffset.
    BloomBlurParams bp{};
    bp.Width = (float)lw[i]; bp.Height = (float)lh[i];
    bp.UseMask = 0; bp.MaskInvert = 0; bp.ClampTexture = clampTex ? 1 : 0; bp._padding0 = 0;
    bp.DirX = 0.0f; bp.DirY = blurOffset;
    bloomPass(c.queue, psoBlur, sampLinear, A[i], B[i], &bp, sizeof(bp), BLOOM_BlurParams);
    // Blur H (B->A): DirX = blurOffset.
    bp.DirX = blurOffset; bp.DirY = 0.0f;
    bloomPass(c.queue, psoBlur, sampLinear, B[i], A[i], &bp, sizeof(bp), BLOOM_BlurParams);
    lastSrv = A[i];  // this level's result feeds the next level's downsample
  }

  // 3. Copy base: source (point sampler) -> composite.
  bloomPass(c.queue, psoCopy, sampPoint, srcTex, composite, nullptr, 0, 0);

  // 4. Upsample-add (ADDITIVE One,One) per level high->low: A[i] -> composite.
  for (int i = levels - 1; i >= 0; --i) {
    if (!A[i]) continue;
    BloomCompositeParams cp{};
    float w = (levelIntensities.size() == (size_t)levels) ? levelIntensities[i] : (1.0f / levels);
    cp.PassIntensity = w * intensity;
    // GlowGradient: outer default is a gradient; with no gradient input wired, PassColor = white (the
    // .cs colorGradient==null branch, _ExecuteBloomPasses.cs:247). FORK (named): the GlowGradient
    // per-level color tint is omitted (no Gradient input port on this leaf yet) -> white per level,
    // matching the .cs null-gradient path exactly. A follow-up can add the Gradient input + Sample(k).
    cp.PassColor[0] = 1.0f; cp.PassColor[1] = 1.0f; cp.PassColor[2] = 1.0f; cp.PassColor[3] = 1.0f;
    cp.InvTargetSize[0] = 1.0f / (float)W; cp.InvTargetSize[1] = 1.0f / (float)H;
    cp.InvSourceSize[0] = 1.0f / (float)lw[i]; cp.InvSourceSize[1] = 1.0f / (float)lh[i];
    bloomAddPass(c.queue, psoAdd, sampLinear, A[i], composite, cp);
  }

  // Output = composite. Copy into c.output (the engine's ensureTex target the driver displays).
  {
    MTL::CommandBuffer* cmd = c.queue->commandBuffer();
    MTL::BlitCommandEncoder* blit = cmd->blitCommandEncoder();
    blit->copyFromTexture(composite, 0, 0, MTL::Origin::Make(0, 0, 0),
                          MTL::Size::Make(W, H, 1), c.output, 0, 0, MTL::Origin::Make(0, 0, 0));
    blit->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
  }

  sampLinear->release(); sampPoint->release();  // scratch + PSOs are cache-owned (tex_op_cache)
}

}  // namespace

int runBloomSelfTest(bool injectBug);

// Self-registration (PIXEL leaf, multi-pass executor). Bloom is one Texture2D in + one out, no special
// ports -> registerTexOp auto-routes through cookTexNode on BOTH flat and resident (R-2). Ports 1:1
// with Bloom.cs:10-35; OUTER defaults from Bloom.t3 (NOT the _ExecuteBloomPasses child literals).
// Resolution picks the output texture size (same enum as the other filters; default WindowFollow).
static const ImageFilterOp _reg_bloom{
    {"Bloom", "Bloom",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Bloom.t3 outer defaults: Intensity 6, Threshold 0.25, ColorWeights Rec.601, GainAndBias
      // (0.5,0.5), MaxLevels 10, Blur 1.0, Clamp false. GlowGradient omitted (named fork: no Gradient
      // input port yet -> white per-level, = .cs null-gradient path).
      {"Intensity", "Intensity", "Float", true, 6.0f, 0.0f, 20.0f},
      {"Threshold", "Threshold", "Float", true, 0.25f, 0.0f, 4.0f},
      {"ColorWeights.x", "ColorWeights", "Float", true, 0.299f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"ColorWeights.y", "ColorWeights.y", "Float", true, 0.587f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"ColorWeights.z", "ColorWeights.z", "Float", true, 0.114f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"MaxLevels", "MaxLevels", "Float", true, 10.0f, 1.0f, 10.0f},
      {"Blur", "Blur", "Float", true, 1.0f, 0.0f, 8.0f},
      {"Clamp", "Clamp", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "Bloom", cookBloom, "bloom", runBloomSelfTest};

// --- Bloom STRUCTURAL golden -------------------------------------------------------------------
// Bloom has no single closed-form pixel (a 4N+2-pass pyramid), so we mirror point_ops_blur.cpp's
// STRUCTURAL asserts (energy spreads / center stays lit) plus a numeric invariant + a resident leg.
// Source: a single bright WHITE pixel on black (centered). After Bloom (Threshold default 0.25 so
// the bright pixel passes the brightpass: luminance 1.0 > 0.25):
//   (a) SPREAD: a pixel several px OFF the source becomes lit — energy bled to a previously-black
//       neighbour beyond the source footprint (a no-op leaves it black).
//   (b) CENTER stays lit (composite copies the source, then adds the glow).
//   (c) INVARIANT: Levels=1, Intensity=0 -> output ~= source (only the copy pass contributes; the
//       single upsample-add pass multiplies by intensity 0 -> adds nothing).
//   (d) RESIDENT parity: flat ~= resident (R-2 auto-route holds on the resident gather).
// injectBug: Threshold=10 (nothing passes the brightpass) -> NO spread -> FAIL. The `want` stays
// FIXED (we assert spread is present; the bug removes the spread, it does NOT flip the expected).
static int bloomCountLit(const std::vector<uint8_t>& px, uint32_t W, uint32_t H, int thresh) {
  int n = 0;
  for (size_t i = 0; i < (size_t)W * H; ++i)
    if ((int)px[i * 4] + px[i * 4 + 1] + px[i * 4 + 2] > thresh) ++n;
  return n;
}

int runBloomSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();  // fresh device: drop PSOs/scratch built on a now-released device
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-bloom] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Source: black, opaque, a bright WHITE filled SQUARE (BLK x BLK) centered. A filled block carries
  // enough energy to survive RGBA8 quantization through the multi-level pyramid (a single pixel's glow
  // is too dim to read robustly past a few texels — same reason fastblur's golden uses a square).
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);
  const uint32_t cx = W / 2, cy = H / 2;
  const uint32_t BLK = 12;                  // square side
  const uint32_t SX0 = (W - BLK) / 2, SY0 = (H - BLK) / 2;  // 26
  const uint32_t SX1 = SX0 + BLK, SY1 = SY0 + BLK;          // 38 (exclusive)
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y) {
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      uint8_t v = (x >= SX0 && x < SX1 && y >= SY0 && y < SY1) ? 255 : 0;
      in[i] = v; in[i + 1] = v; in[i + 2] = v; in[i + 3] = 255;  // white square on opaque black
    }
  }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  auto lum = [](const std::vector<uint8_t>& px, uint32_t W2, uint32_t x, uint32_t y) {
    size_t i = ((size_t)y * W2 + x) * 4;
    return (int)px[i] + px[i + 1] + px[i + 2];
  };

  // --- Main bloom run (default params; injectBug raises Threshold so the brightpass passes nothing).
  std::map<std::string, float> params;
  params["MaxLevels"] = 4.0f;        // fixed pyramid -> deterministic spread
  params["Intensity"] = 6.0f;
  params["Threshold"] = injectBug ? 10.0f : 0.25f;
  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookBloom(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  int center = lum(out, W, cx, cy);
  // 4px BEYOND the square's right edge — a previously-black neighbour OUTSIDE the source footprint.
  int off = lum(out, W, SX1 + 4, cy);
  bool centerLit = center > 20;
  bool spread = off > 10;              // glow bled past the hard edge (no-op/threshold-kill leaves 0)

  // --- INVARIANT: Levels=1, Intensity=0 -> output ~= source (copy pass only). Fresh run.
  std::map<std::string, float> pInv;
  pInv["MaxLevels"] = 1.0f;
  pInv["Intensity"] = 0.0f;
  pInv["Threshold"] = 0.25f;
  MTL::Texture* dst2 = dev->newTexture(td);
  TexCookCtx c2;
  c2.dev = dev; c2.lib = lib; c2.queue = q;
  c2.nodeId = 1; c2.inputTexture = src; c2.output = dst2; c2.params = &pInv;
  cookBloom(c2);
  std::vector<uint8_t> out2((size_t)W * H * 4, 0);
  dst2->getBytes(out2.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  // copy pass uses POINT sampling at the same resolution -> exact passthrough; center must match and
  // an off pixel must stay black (intensity 0 adds nothing).
  int invCenter = lum(out2, W, cx, cy);
  int invOff = lum(out2, W, SX1 + 4, cy);  // outside the square; intensity 0 adds nothing -> black
  bool invariant = invCenter > 700 && invOff < 10;  // center white passthrough, neighbour still black

  // --- RESIDENT parity leg (R-2): build RadialPoints->DrawPoints->RenderTarget->Bloom through the
  // canonical resident path and assert the displayed texture is non-empty (Bloom auto-routed through
  // the resident cookTexNode gather). injectBug raises Threshold there too -> far less lit.
  registerBuiltinPointOps();  // RadialPoints + DrawPoints + RenderTarget present for the chain leg
  const uint32_t RW = 128, RH = 128;
  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 128.0f; gen.params["Radius"] = 1.5f; g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  Node rt; rt.id = 3; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)RW; rt.params["CustomH"] = (float)RH;
  g.nodes.push_back(rt);
  Node bl; bl.id = 4; bl.type = "Bloom";
  bl.params["Resolution"] = 4.0f; bl.params["CustomW"] = (float)RW; bl.params["CustomH"] = (float)RH;
  bl.params["MaxLevels"] = 4.0f; bl.params["Intensity"] = 6.0f;
  bl.params["Threshold"] = injectBug ? 10.0f : 0.1f;
  g.nodes.push_back(bl);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});
  g.connections.push_back({103, pinId(3, 1), pinId(4, 0)});

  int flatNonBlack = 0, residentNonBlack = 0;
  {
    PointGraph pg(dev, lib, q, 64, 64);
    int term = pg.defaultDrawTarget(g);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, term);
    MTL::Texture* tex = pg.target();
    if (tex && (uint32_t)tex->width() == RW && (uint32_t)tex->height() == RH) {
      std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
      tex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
      flatNonBlack = bloomCountLit(px, RW, RH, 20);
    }
  }
  {
    PointGraph rpg(dev, lib, q, 64, 64);
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    EvaluationContext rctx{}; rctx.frameIndex = 0; rctx.time = 0.0f; rctx.deltaTime = 1.0f / 60.0f;
    rpg.cookResident(rg, rctx, nullptr, "4");
    MTL::Texture* rtex = rpg.target();
    if (rtex && (uint32_t)rtex->width() == RW && (uint32_t)rtex->height() == RH) {
      std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
      rtex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
      residentNonBlack = bloomCountLit(px, RW, RH, 20);
    }
  }
  // flat ~= resident: both lit, and within a tolerant band of each other (same algorithm/wiring).
  bool bothLit = flatNonBlack > 50 && residentNonBlack > 50;
  int diff = std::abs(flatNonBlack - residentNonBlack);
  int maxNB = std::max(flatNonBlack, residentNonBlack);
  bool residentParity = bothLit && (diff <= maxNB / 4 + 5);  // within ~25%

  bool pass = centerLit && spread && invariant && residentParity;
  printf("[selftest-bloom] center=%d off6=%d spread=%d centerLit=%d | inv(c=%d,off=%d)=%d | "
         "flatNB=%d residNB=%d residParity=%d -> %s\n",
         center, off, spread ? 1 : 0, centerLit ? 1 : 0, invCenter, invOff, invariant ? 1 : 0,
         flatNonBlack, residentNonBlack, residentParity ? 1 : 0, pass ? "PASS" : "FAIL");

  src->release(); dst->release(); dst2->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
