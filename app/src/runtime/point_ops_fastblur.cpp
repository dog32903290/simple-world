// FastBlur image-filter texture op (lane image_filter) — the FIRST MULTI-PASS COMPUTE leaf, and the
// first real consumer of the multi-pass scratch seam (cachedScratchTex shaderWrite=true).
//
// TiXL authority:
//   Operators/Lib/image/fx/blur/FastBlur.cs        — the user-facing op: ONLY two ports,
//       Image (Texture2D) + MaxLevels (int). Result (Texture2D).
//   Operators/Lib/image/fx/blur/FastBlur.t3        — a Layer2d-wrapped compound that wires Image +
//       MaxLevels + the two precompiled pixel shaders + a linear sampler into _ExecuteFastBlurPasses.
//       MaxLevels (01d8a4a8) connects DIRECTLY to _ExecuteFastBlurPasses.Steps (7f79b69a) — no math.
//   Operators/Lib/image/fx/_/_ExecuteFastBlurPasses.cs — the Dual Kawase++ pass executor:
//       ResolveSteps, the half-res pyramid _levels[], the down loop (4-tap box), the up loop (9-tap
//       tent with FillUpsampleKernel's per-stage normalized weights), DisabledBlendState (the up pass
//       OVERWRITES, never additively blends — line 80).
//   Assets/shaders/img/blur/FastBlur-DownsamplePS.hlsl / -UpsampleAcculuatePS.hlsl — the two kernels
//       (ported 1:1 into fastblur.metal as fastblur_down_cs / fastblur_up_cs).
//
// FORK (named): "我方 multi-pass+compute leaf vs TiXL .t3 Layer2d compound — SAME Dual Kawase++
// pyramid-blur algorithm; the leaf avoids the compound machinery." The .t3 Layer2d wrapper +
// _ExecuteFastBlurPasses Instance are INCIDENTAL plumbing: the only knobs the compound exposes to the
// user are Image + MaxLevels, the combine is a FIXED non-user-configurable overwrite (DisabledBlend),
// and the shaders/sampler are constants the compound feeds in. So the whole pyramid collapses into one
// compute leaf issuing N down + N up dispatches over per-level scratch textures — exactly the
// multi-pass idiom particle_system.cpp uses for its 3 compute passes. NO Layer2d/Execute seam needed.
//
// SECOND FORK (named): TiXL's pass executor hardcodes Steps=0 => auto and the down/up OffsetPx=1.0 and
// the FillUpsampleKernel wide/tight weight schedule; FastBlur.cs's only port is MaxLevels (=Steps).
// We expose MaxLevels 1:1 (default 0 = auto, same ResolveSteps clamp 1..12) and bake the same
// OffsetPx + weight schedule verbatim — inventing NO extra knobs.
//
// THIRD FORK (named): the Dual Kawase _levels[] are DISCRETE half-res render targets, NOT mip levels
// of one texture. So FastBlur consumes the MULTI-PASS scratch seam (cachedScratchTex shaderWrite),
// NOT the mip-gen seam — mip-sampling is not part of this algorithm. (Reported to orchestrator.)
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/fastblur_params.h"            // FastBlurDownParams/UpParams, FASTBLUR_* bindings
#include "runtime/image_filter_op_registry.h"   // ImageFilterComputeOp self-registration
#include "runtime/point_graph.h"                // TexCookCtx, cookParam
#include "runtime/tex_op_cache.h"               // cachedComputePSO, cachedScratchTex (multi-pass seam)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

inline uint32_t ceilDiv(uint32_t a, uint32_t b) { return (a + b - 1) / b; }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// injectBug hook (golden only): when set, cookFastBlur SKIPS the final-up dispatch that writes
// c.output from level0 — a real dropped-pass wiring perturbation (not an assertion flip). Default
// false in production; the selftest toggles it. File-scope so the golden (below) can set it.
bool g_fastblurSkipFinal = false;

// _ExecuteFastBlurPasses.cs:189-201 ResolveSteps. stepsIn>0 => clamp(1,12). Else auto from the min
// dimension: floor(log2(minDim)) - 2, clamped (1,12), with a 1x1 tail guard.
int resolveSteps(int stepsIn, uint32_t w, uint32_t h) {
  if (stepsIn > 0) return std::clamp(stepsIn, 1, 12);
  uint32_t minDim = std::min(w, h);
  if (minDim <= 1) return 1;
  int autoSteps = (int)std::floor(std::log2((double)minDim)) - 2;
  return std::clamp(std::max(1, autoSteps), 1, 12);
}

// _ExecuteFastBlurPasses.cs:204-228 FillUpsampleKernel. stageIndex 0..stageCount-1 maps deep mip ->
// final via t in [0,1]; lerp wide(2,2,2) -> tight(8,2,1) then NORMALIZE so the 9 taps sum to 1.
void fillUpParams(FastBlurUpParams& up, int stageIndex, int stageCount, uint32_t lowW, uint32_t lowH) {
  float t = (stageCount <= 1) ? 1.0f : (float)stageIndex / (float)(stageCount - 1);
  const float wideC = 2.0f, wideCard = 2.0f, wideDiag = 2.0f;
  const float tightC = 8.0f, tightCard = 2.0f, tightDiag = 1.0f;
  float c = lerpf(wideC, tightC, t);
  float card = lerpf(wideCard, tightCard, t);
  float diag = lerpf(wideDiag, tightDiag, t);
  float sum = c + 4.0f * card + 4.0f * diag;
  float inv = (sum > 1e-8f) ? 1.0f / sum : 1.0f;
  up.InvLowSize[0] = 1.0f / (float)std::max(1u, lowW);
  up.InvLowSize[1] = 1.0f / (float)std::max(1u, lowH);
  up.OffsetPx = 1.0f;  // TiXL bakes 1.0 per stage
  up.WCenter = c * inv;
  up.WCard = card * inv;
  up.WDiag = diag * inv;
}

// One compute dispatch: bind src@0, dst@1, params@buffer0, sampler@0; ceil-div over dst extent.
void dispatchPass(MTL::CommandQueue* q, MTL::ComputePipelineState* pso, MTL::SamplerState* samp,
                  const MTL::Texture* src, MTL::Texture* dst, const void* params, size_t paramsSize) {
  const uint32_t ow = (uint32_t)dst->width(), oh = (uint32_t)dst->height();
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::ComputeCommandEncoder* enc = cmd->computeCommandEncoder();
  enc->setComputePipelineState(pso);
  enc->setTexture(const_cast<MTL::Texture*>(src), FASTBLUR_Src);   // in  @ texture(0)
  enc->setTexture(dst, FASTBLUR_Result);                           // out @ texture(1)
  enc->setSamplerState(samp, FASTBLUR_Sampler);                    // linear clamp @ sampler(0)
  enc->setBytes(params, paramsSize, FASTBLUR_Params);              // cbuffer @ buffer(0)
  MTL::Size tg = MTL::Size::Make(FASTBLUR_TGX, FASTBLUR_TGY, 1);
  MTL::Size grid = MTL::Size::Make(ceilDiv(ow, FASTBLUR_TGX), ceilDiv(oh, FASTBLUR_TGY), 1);
  enc->dispatchThreadgroups(grid, tg);
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

// FastBlur cook: Dual Kawase++ over per-level scratch textures (the multi-pass scratch seam).
// Output is full-res (same dims as the Resolution pin); no SizeFn override.
void cookFastBlur(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();
  const uint32_t W = (uint32_t)c.output->width(), H = (uint32_t)c.output->height();

  // No upstream texture: nothing to blur — clear output (parity with the other leaves' no-input path).
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

  MTL::ComputePipelineState* psoDown = cachedComputePSO(c.dev, c.lib, "fastblur_down_cs");
  MTL::ComputePipelineState* psoUp = cachedComputePSO(c.dev, c.lib, "fastblur_up_cs");
  if (!psoDown || !psoUp) return;

  // Linear, clamp-to-edge sampler (the TiXL LinearSampler input). Bilinear taps need linear filter.
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // Resolve the pyramid depth (MaxLevels port 1:1 with TiXL FastBlur.cs / Steps).
  int stepsIn = (int)std::lround(cookParam(c, "MaxLevels", 0.0f));
  int steps = resolveSteps(stepsIn, W, H);

  // Per-level half-res scratch textures (the Dual Kawase _levels[]). Each is a cached scratch slot
  // (multi-pass seam: shaderWrite=true so a compute kernel can WRITE it; later read as Src). Level i
  // dims = (W,H) >> (i+1), min 1. The final-up destination is c.output (full res).
  uint32_t levW[12], levH[12];
  MTL::Texture* levels[12] = {nullptr};
  {
    uint32_t cw = W, ch = H;
    for (int i = 0; i < steps; ++i) {
      cw = std::max(1u, cw / 2);
      ch = std::max(1u, ch / 2);
      levW[i] = cw; levH[i] = ch;
      char key[32];
      std::snprintf(key, sizeof(key), "fastblur.l%d", i);
      levels[i] = cachedScratchTex(c.dev, (uint64_t)fmt, cw, ch, key, /*shaderWrite=*/true);
      if (!levels[i]) { samp->release(); return; }
    }
  }

  // ---- Downsample + blur: source -> level0 -> level1 -> ... -> level(steps-1) ----
  const MTL::Texture* lastSrc = c.inputTexture;
  uint32_t lastW = W, lastH = H;
  for (int level = 0; level < steps; ++level) {
    FastBlurDownParams dp{};
    dp.InvSrcSize[0] = 1.0f / (float)std::max(1u, lastW);
    dp.InvSrcSize[1] = 1.0f / (float)std::max(1u, lastH);
    dp.OffsetPx = 1.0f;  // TiXL _ExecuteFastBlurPasses.cs:111 conservative default
    dispatchPass(c.queue, psoDown, samp, lastSrc, levels[level], &dp, sizeof(dp));
    lastSrc = levels[level];
    lastW = levW[level]; lastH = levH[level];
  }

  // ---- Upsample + blur: deepest -> ... -> level0 (each writes its OWN level, overwrite) ----
  // TiXL up loop: for level = steps-2 .. 0, dst = _levels[level], low = _levels[level+1].
  for (int level = steps - 2; level >= 0; --level) {
    FastBlurUpParams up{};
    fillUpParams(up, /*stageIndex=*/(steps - 2) - level, /*stageCount=*/steps, levW[level + 1],
                 levH[level + 1]);
    dispatchPass(c.queue, psoUp, samp, levels[level + 1], levels[level], &up, sizeof(up));
  }

  // ---- Final up to full resolution: low = level0 -> c.output ----
  // injectBug (golden): skip this dispatch -> c.output is never written by the kernel.
  if (!g_fastblurSkipFinal) {
    FastBlurUpParams up{};
    fillUpParams(up, /*stageIndex=*/steps - 1, /*stageCount=*/steps, levW[0], levH[0]);
    dispatchPass(c.queue, psoUp, samp, levels[0], c.output, &up, sizeof(up));
  }

  samp->release();  // PSOs + scratch are cache-owned (tex_op_cache), not released here.
}

// --- EXACT-PIXEL tooth (closed-form, MaxLevels=1) -------------------------------------------------
// The energy/spread/edge/coverage checks above are ALL invariant under WRONG KERNEL WEIGHTS (a
// refuter proved: down box 0.25->0.30 stays inside the energy band; up tightDiag 1.0->4.0 leaves
// every probe byte-identical because FillUpsampleKernel RE-NORMALIZES the 9 taps). So those checks
// catch a DROPPED pass but NOT a mis-WEIGHTED tap. This tooth pins specific output texels to values
// hand-computed from the CORRECT weights, tight enough (+/-2 LSB) that a mis-weighting reddens it.
//
// Closed-form setup (no deep pyramid): drive FastBlur with MaxLevels=1, so the pipeline is exactly
// down-once -> up-once (the up loop `for level=steps-2..0` is empty when steps==1; only the final-up
// dispatch runs). Input is a 16x16 vertical HALF-PLANE (x<8 black=0, x>=8 white=255), CONSTANT in Y,
// so the whole thing collapses to 1D in X and is hand-computable in closed form:
//   DOWN (4-tap box *0.25, level0 is 8 wide; Y-constant -> the +/-Y tap pairs coincide):
//     level0[gx] = 0.5*(0.5*(src[2gx-1]+src[2gx]) + 0.5*(src[2gx+1]+src[2gx+2]))   (clamp-to-edge)
//     -> ideal [0,0,0,63.75,191.25,255,255,255]  -> RGBA8-quantized [0,0,0,64,191,255,255,255].
//   UP (final, stageCount=1 -> t=1 -> TIGHT weights c=8,card=2,diag=1, NORMALIZED /20:
//     WCenter=0.4 WCard=0.1 WDiag=0.05). Output 16 wide, low=level0 (8). Per output texel gx the low
//     sample coord is p=gx/2-0.25; Y-constant collapses the 9 taps to three columns {p-1,p,p+1} with
//     effective weights {0.2, 0.6, 0.2}. Bilinear-sample the quantized level0 and round:
//       out[6] = 0.6*L(2.75)+0.2*L(1.75)+0.2*L(3.75) = 0.6*48 +0.2*0  +0.2*159.25 = 60.65 -> 61
//       out[8] = 0.6*L(3.75)+0.2*L(2.75)+0.2*L(4.75) = 0.6*159.25+0.2*48+0.2*239.0 = 152.95-> 153
//     (full hand-derived row 8: x5=29 x6=61 x7=102 x8=153 x9=194 — verified to MATCH the unmodified
//     correct-weight GPU output, so these ARE the correct values, not a re-fit threshold.)
// Why it bites: down 0.25->0.30 scales level0 by 1.2 (pre-quant) -> e.g. out[6] 61->73 (+12);
// up tightDiag 1.0->4.0 re-weights the taps (0.6/0.2/0.2 -> 0.375/0.3125/0.3125) -> out[6] 61->68,
// out[5] 29->36, out[9] 194->187 — all beyond +/-2. Asserting 5 named pixels makes BOTH perturbations
// move at least one past tolerance.
bool fastblurExactPixelTooth() {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 16, H = 16;
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-fastblur:tooth] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return false;
  }

  // 16x16 vertical half-plane: x<8 black, x>=8 white; constant in Y. opaque.
  MTL::TextureDescriptor* tdSrc =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  tdSrc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  tdSrc->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(tdSrc);
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      uint8_t v = (x >= 8) ? 255 : 0;
      in[i] = v; in[i + 1] = v; in[i + 2] = v; in[i + 3] = 255;
    }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  MTL::TextureDescriptor* tdDst =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  tdDst->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead |
                  MTL::TextureUsageShaderWrite);
  tdDst->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(tdDst);

  std::map<std::string, float> params;
  params["MaxLevels"] = 1.0f;  // forced 1-level => down-once -> up-once, closed-form
  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookFastBlur(c);  // g_fastblurSkipFinal is false here (the caller resets it before this tooth)

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  auto R = [&](uint32_t x, uint32_t y) { return (int)out[((size_t)y * W + x) * 4]; };

  // Hand-computed CORRECT-weight values at row 8 (see derivation above). Tol +/-2 LSB.
  struct Pin { uint32_t x; int want; };
  const Pin pins[] = {{5, 29}, {6, 61}, {7, 102}, {8, 153}, {9, 194}};
  const int kTol = 2;
  bool ok = true;
  for (const Pin& p : pins) {
    int got = R(p.x, 8);
    bool hit = std::abs(got - p.want) <= kTol;
    if (!hit) ok = false;
    printf("[selftest-fastblur:tooth] px(%u,8)=%d want %d(+/-%d) %s\n", p.x, got, p.want, kTol,
           hit ? "ok" : "MISS");
  }

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return ok;
}

}  // namespace

int runFastBlurSelfTest(bool injectBug);

// Self-registration (COMPUTE leaf): ImageFilterComputeOp marks "FastBlur" as ShaderWrite on its
// output, no SizeFn (output is full-res = Resolution pin), and registers --selftest-fastblur[-bug].
// Ports 1:1 with FastBlur.cs: Image (Texture2D) + MaxLevels (Int, default 0=auto). Resolution picks
// the output texture size (same enum as the other filters; default WindowFollow).
static const ImageFilterComputeOp _reg_fastblur{
    {"FastBlur", "FastBlur",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // MaxLevels (TiXL Int, default 0 => auto; ResolveSteps clamps 1..12). Float Widget::Drag.
      {"MaxLevels", "MaxLevels", "Float", true, 0.0f, 0.0f, 12.0f},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "FastBlur", cookFastBlur, /*sizeFn=*/nullptr, "fastblur", runFastBlurSelfTest};

// --- FastBlur MULTI-PASS COMPUTE golden -------------------------------------------------------
// A blur SPREADS energy across a known radius and CONSERVES total energy (every pass has DC gain 1:
// down=4-tap*0.25, up=normalized 9-tap). Source: a bright WHITE filled SQUARE (BLK x BLK) centered on
// a black field. A filled block (not a single pixel) carries enough energy to survive RGBA8
// quantization through a multi-level pyramid, so the spread is measurable + hand-bounded. We assert
// (closed-form bounds — not exact pixels, since the pyramid spreads over many texels, but bounded
// deterministically by energy conservation):
//   (1) SPREAD OUTSIDE the original square: a pixel several px BEYOND the square's edge becomes
//       non-black — energy bled past the hard edge. A no-op / single-pass-only / dropped-level
//       pipeline leaves the region outside the square black (or a hard step, not a soft bleed).
//   (2) EDGE SOFTENED: a pixel just INSIDE the original edge is DIMMER than the solid interior — the
//       blur pulled energy out across the boundary (a passthrough keeps the edge a hard 255 step).
//   (3) ENERGY ~CONSERVED: total output luminance stays within a band of the input's. Input total =
//       BLK*BLK*765. DC gain 1 conserves it (±clamp/rounding); a dropped/duplicated pass breaks it.
//   (4) COVERAGE: sentinel pre-fill -> after cook NO pixel still holds the sentinel (every output
//       texel written by the final-up dispatch; a ceil->floor-div regression drops remainder rows).
// injectBug: SKIP the final-up pass (the dispatch that writes c.output from level0). Then c.output is
// never written by the kernel -> stays sentinel -> coverage FAILS, and the spread probe is sentinel,
// not blurred energy -> spread FAILS. A real wiring perturbation (a dropped pass), not an assert flip.
int runFastBlurSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  // Non-8-divisible on purpose (risk-trap): 100x100 output so ceil-div remainder rows are exercised.
  const uint32_t W = 100, H = 100;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-fastblur] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Source: black field, opaque, a bright WHITE filled square (BLK x BLK) centered on the field.
  MTL::TextureDescriptor* tdSrc =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  tdSrc->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  tdSrc->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(tdSrc);

  const uint32_t BLK = 20;               // square side
  const uint32_t SX0 = (W - BLK) / 2;    // 40
  const uint32_t SY0 = (H - BLK) / 2;    // 40
  const uint32_t SX1 = SX0 + BLK;        // 60 (exclusive)
  const uint32_t SY1 = SY0 + BLK;        // 60
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y) {
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      bool inSq = (x >= SX0 && x < SX1 && y >= SY0 && y < SY1);
      uint8_t v = inSq ? 255 : 0;
      in[i + 0] = v; in[i + 1] = v; in[i + 2] = v; in[i + 3] = 255;  // white square on opaque black
    }
  }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  // Output: RenderTarget|ShaderRead|ShaderWrite (compute leaf writes via RWTexture2D).
  MTL::TextureDescriptor* tdDst =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  tdDst->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead |
                  MTL::TextureUsageShaderWrite);
  tdDst->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(tdDst);

  // Sentinel pre-fill: a color the kernel can NEVER emit from a white-on-black source (it only
  // produces grays). After cook, any pixel still holding the sentinel was NOT written -> a hole.
  const uint8_t SR = 7, SG = 200, SB = 13, SA = 200;
  std::vector<uint8_t> stale((size_t)W * H * 4);
  for (size_t i = 0; i < (size_t)W * H; ++i) {
    stale[i * 4 + 0] = SR; stale[i * 4 + 1] = SG; stale[i * 4 + 2] = SB; stale[i * 4 + 3] = SA;
  }
  dst->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, stale.data(), W * 4);

  std::map<std::string, float> params;
  params["MaxLevels"] = 4.0f;  // fixed 4-level pyramid -> deterministic spread (not auto)
  g_fastblurSkipFinal = injectBug;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookFastBlur(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  auto lum = [&](uint32_t x, uint32_t y) {
    size_t i = ((size_t)y * W + x) * 4;
    return (int)out[i] + out[i + 1] + out[i + 2];
  };

  // (1) SPREAD OUTSIDE: a pixel 5px BEYOND the square's right edge (well outside the original solid
  // region) must be lit by energy that bled past the hard edge. Black/passthrough leaves it 0.
  int spreadLum = lum(SX1 + 5, (SY0 + SY1) / 2);
  bool spread = spreadLum > 10;

  // (2) EDGE SOFTENED: a pixel just INSIDE the original right edge is dimmer than a solid 255*3 — the
  // blur pulled energy across the boundary. A hard-edge passthrough keeps it ~765.
  int edgeLum = lum(SX1 - 2, (SY0 + SY1) / 2);
  bool edgeSoftened = edgeLum < 700;

  // (3) ENERGY ~CONSERVED: total output luminance within a band of the input's. Input total =
  // BLK*BLK*765 = 306000. DC gain 1 conserves it; allow a wide band [0.4x, 1.8x] to absorb RGBA8
  // rounding + clamp-to-edge edge gain while still catching a dropped/duplicated pass (which would
  // zero it or push it far out of band — the all-black GREEN-with-1px-marker failure read total=0).
  long total = 0;
  for (size_t i = 0; i < (size_t)W * H; ++i)
    total += (long)out[i * 4 + 0] + out[i * 4 + 1] + out[i * 4 + 2];
  const long inTotal = (long)BLK * BLK * 765;  // 306000
  bool energyOk = (total > inTotal * 4 / 10 && total < inTotal * 18 / 10);

  // (4) COVERAGE: no pixel still holds the sentinel (every output texel written by the final-up).
  int unwritten = 0;
  for (size_t i = 0; i < (size_t)W * H; ++i) {
    if (out[i * 4 + 0] == SR && out[i * 4 + 1] == SG && out[i * 4 + 2] == SB &&
        out[i * 4 + 3] == SA) {
      ++unwritten;
    }
  }
  bool covered = (unwritten == 0);

  // EXACT-PIXEL tooth: closed-form 1-level case pinning correct-weight pixels (catches a
  // mis-WEIGHTED tap that the energy/spread/edge/coverage band-checks above let through).
  // Reset the dropped-pass injectBug flag first so the tooth always cooks with the CORRECT wiring —
  // it tests kernel WEIGHTS, an orthogonal axis to the injectBug dropped-pass perturbation.
  g_fastblurSkipFinal = false;
  bool exactPix = fastblurExactPixelTooth();

  bool pass = spread && edgeSoftened && energyOk && covered && exactPix;
  printf("[selftest-fastblur] spreadOut=%d(>10:%d) edge=%d(soft<700:%d) total=%ld(in=%ld,band:%d) "
         "unwritten=%d covered=%d exactPix=%d -> %s\n",
         spreadLum, spread ? 1 : 0, edgeLum, edgeSoftened ? 1 : 0, total, inTotal, energyOk ? 1 : 0,
         unwritten, covered ? 1 : 0, exactPix ? 1 : 0, pass ? "PASS" : "FAIL");

  g_fastblurSkipFinal = false;
  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
