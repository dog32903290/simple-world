// RyojiPattern2 image op (lane image_filter) — recursive-subdivision pattern generator.
// TiXL authority: Operators/Lib/image/generate/pattern/RyojiPattern2.cs (Image/Background/Foreground/
// MixOriginal/Contrast/ForgroundRatio/Highlight/HighlightProbability/HighlightSeed/Splits/SplitB/
// SplitC/SplitProbability/ScrollSpeed/ScrollProbability/ScrollOffset/Padding/Seed/Resolution inputs)
// + RyojiPattern2.t3 (defaults below) + Assets/shaders/img/generate/RyojiPattern2.hlsl (the single
// pixel pass: subdivide the unit cell 6x, hash-color each cel, random Highlight overlay).
//
// Single Texture2D input (ImageA, t0) + a 1:1 cbuffer (no FloatsToBuffer recompute — the .t3 wires
// each component straight into _ImageFxShaderSetup2's float list in cbuffer order; HighlightSeed goes
// through a plain IntToFloat cast). cookRyojiPattern2 reads c.inputTexture, runs one fullscreen pass
// of ryojipattern2_vs/ryojipattern2_fs, writes c.output. Binds b0 = RyojiPattern2Params (the whole
// ParamConstants block), b1 = RyojiPattern2Resolution (output dims, informational).
//
// FORK (named — beatTime): see ryojipattern2.metal / ryojipattern2_params.h. b1 TimeConstants.beatTime
// is host-supplied (default 0 -> static), same fork class as Grain's Time.
//
// Self-contained leaf: cookRyojiPattern2 + the ImageFilterOp registrar + runRyojiPattern2SelfTest.
// Shares the D2-2 PSO+scratch cache seam (tex_op_cache.h) with Pixelate/Blur/Displace/etc.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"                // TexCookCtx, cookParam, registerTexOp
#include "runtime/ryojipattern2_params.h"       // RyojiPattern2Params/Resolution, *_Params/Resolution
#include "runtime/tex_op_cache.h"               // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration so the file-scope registrar below can reference the golden defined later in
// THIS TU (kept local — no shared point_ops.h edit; the selftest self-registers via ImageFilterOp).
int runRyojiPattern2SelfTest(bool injectBug);

namespace {

// RyojiPattern2 texture op: single pass. Reads c.inputTexture (ImageA), writes c.output.
// The op is a GENERATOR — it produces a full pattern even with no upstream texture (it samples ImageA
// only for the Highlight/MixOriginal terms). When no input is wired we bind a 1x1 transparent-black
// dummy so the sample is well-defined (matches TiXL Image=null -> default texture).
void cookRyojiPattern2(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "ryojipattern2_vs", "ryojipattern2_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  // Sampler: linear + REPEAT. RyojiPattern2.t3 leaves _ImageFxShaderSetup2.Wrap at its default "Wrap"
  // (TextureAddressMode.Wrap = repeat), Filter at MinMagMipLinear.
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeRepeat);  // RyojiPattern2.t3 Wrap=Wrap (default)
  sd->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // 1x1 transparent-black dummy when ImageA is unwired (TiXL Image=null default texture).
  MTL::Texture* dummy = nullptr;
  const MTL::Texture* img = c.inputTexture;
  if (!img) {
    MTL::TextureDescriptor* dd =
        MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
    dd->setUsage(MTL::TextureUsageShaderRead);
    dd->setStorageMode(MTL::StorageModeShared);
    dummy = c.dev->newTexture(dd);
    uint8_t zero[4] = {0, 0, 0, 0};
    dummy->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, zero, 4);
    img = dummy;
  }

  // TiXL params: RyojiPattern2.t3 defaults (verbatim).
  RyojiPattern2Params p{};
  // Background default (1e-6,1e-6,1e-6,0) ~ transparent black.
  p.BackgroundR = cookParam(c, "Background.r", 1e-6f);
  p.BackgroundG = cookParam(c, "Background.g", 1e-6f);
  p.BackgroundB = cookParam(c, "Background.b", 1e-6f);
  p.BackgroundA = cookParam(c, "Background.a", 0.0f);
  p.ForegroundR = cookParam(c, "Foreground.r", 1.0f);
  p.ForegroundG = cookParam(c, "Foreground.g", 1.0f);
  p.ForegroundB = cookParam(c, "Foreground.b", 1.0f);
  p.ForegroundA = cookParam(c, "Foreground.a", 1.0f);
  p.HighlightR = cookParam(c, "Highlight.r", 1.0f);
  p.HighlightG = cookParam(c, "Highlight.g", 0.0f);
  p.HighlightB = cookParam(c, "Highlight.b", 0.0f);
  p.HighlightA = cookParam(c, "Highlight.a", 1.0f);
  p.SplitAX = cookParam(c, "Splits.x", 14.0f);
  p.SplitAY = cookParam(c, "Splits.y", 4.0f);
  p.SplitBX = cookParam(c, "SplitB.x", 1.0f);
  p.SplitBY = cookParam(c, "SplitB.y", 3.0f);
  p.SplitCX = cookParam(c, "SplitC.x", 1.0f);
  p.SplitCY = cookParam(c, "SplitC.y", 10.0f);
  p.SplitProbabilityX = cookParam(c, "SplitProbability.x", 0.1f);
  p.SplitProbabilityY = cookParam(c, "SplitProbability.y", 0.5f);
  p.ScrollSpeedX = cookParam(c, "ScrollSpeed.x", 0.04f);
  p.ScrollSpeedY = cookParam(c, "ScrollSpeed.y", 0.5f);
  p.ScrollProbabilityX = cookParam(c, "ScrollProbability.x", 0.0f);
  p.ScrollProbabilityY = cookParam(c, "ScrollProbability.y", 0.5f);
  p.PaddingX = cookParam(c, "Padding.x", 0.02f);
  p.PaddingY = cookParam(c, "Padding.y", 0.02f);
  p.Contrast = cookParam(c, "Contrast", 0.5f);
  p.Seed = cookParam(c, "Seed", 42.0f);
  p.ForegroundRatio = cookParam(c, "ForegroundRatio", 0.50333333f);
  p.HighlightProbability = cookParam(c, "HighlightProbability", 0.01f);
  p.MixOriginal = cookParam(c, "MixOriginal", 0.0f);
  p.ScrollOffset = cookParam(c, "ScrollOffset", 0.0f);
  p.HighlightSeed = cookParam(c, "HighlightSeed", 0.0f);
  // FORK (named): beatTime from the seconds clock (ctx->time), 0 headless. Only scales the cel scroll.
  p.BeatTime = c.ctx ? c.ctx->time : 0.0f;

  RyojiPattern2Resolution res{};
  res.TargetWidth = (float)c.output->width();
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
  enc->setFragmentTexture(const_cast<MTL::Texture*>(img), 0);
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(RyojiPattern2Params), RYOJIPATTERN2_Params);
  enc->setFragmentBytes(&res, sizeof(RyojiPattern2Resolution), RYOJIPATTERN2_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
  if (dummy) dummy->release();
}

}  // namespace

// Self-registration. NodeSpec mirrors RyojiPattern2.cs/.t3 (defaults verbatim). Color/Vec inputs use
// Widget::Vec component ports (Pixelate precedent). HighlightSeed modeled as Float (int->float cast,
// same fork class as Pixelate's Divisor).
static const ImageFilterOp _reg_ryojipattern2{
    {"RyojiPattern2", "RyojiPattern2",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Background (Vec4, t3 default ~ (1e-6,1e-6,1e-6,0) = transparent black).
      {"Background.r", "Background", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Background.g", "Background.g", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.b", "Background.b", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.a", "Background.a", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Foreground (Vec4, t3 default (1,1,1,1)).
      {"Foreground.r", "Foreground", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Foreground.g", "Foreground.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Foreground.b", "Foreground.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Foreground.a", "Foreground.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // MixOriginal (Float, t3 default 0).
      {"MixOriginal", "MixOriginal", "Float", true, 0.0f, 0.0f, 1.0f},
      // Contrast (Float, t3 default 0.5).
      {"Contrast", "Contrast", "Float", true, 0.5f, 0.0f, 1.0f},
      // ForegroundRatio (Float, t3 default 0.50333333; .cs spelling "ForgroundRatio").
      {"ForegroundRatio", "ForegroundRatio", "Float", true, 0.50333333f, 0.0f, 1.0f},
      // Highlight (Vec4, t3 default (1,0,0,1)).
      {"Highlight.r", "Highlight", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Highlight.g", "Highlight.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Highlight.b", "Highlight.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Highlight.a", "Highlight.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // HighlightProbability (Float, t3 default 0.01).
      {"HighlightProbability", "HighlightProbability", "Float", true, 0.01f, 0.0f, 1.0f},
      // HighlightSeed (Int -> Float fork, t3 default 0).
      {"HighlightSeed", "HighlightSeed", "Float", true, 0.0f, 0.0f, 1000.0f},
      // Splits (Int2 -> SplitA, t3 default (14,4)).
      {"Splits.x", "Splits", "Float", true, 14.0f, 1.0f, 64.0f, Widget::Vec, {}, true, 2},
      {"Splits.y", "Splits.y", "Float", true, 4.0f, 1.0f, 64.0f, Widget::Vec, {}, true, 1},
      // SplitB (Int2, t3 default (1,3)).
      {"SplitB.x", "SplitB", "Float", true, 1.0f, 1.0f, 64.0f, Widget::Vec, {}, true, 2},
      {"SplitB.y", "SplitB.y", "Float", true, 3.0f, 1.0f, 64.0f, Widget::Vec, {}, true, 1},
      // SplitC (Int2, t3 default (1,10)).
      {"SplitC.x", "SplitC", "Float", true, 1.0f, 1.0f, 64.0f, Widget::Vec, {}, true, 2},
      {"SplitC.y", "SplitC.y", "Float", true, 10.0f, 1.0f, 64.0f, Widget::Vec, {}, true, 1},
      // SplitProbability (Vec2, t3 default (0.1,0.5)).
      {"SplitProbability.x", "SplitProbability", "Float", true, 0.1f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"SplitProbability.y", "SplitProbability.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // ScrollSpeed (Vec2, t3 default (0.04,0.5)).
      {"ScrollSpeed.x", "ScrollSpeed", "Float", true, 0.04f, -4.0f, 4.0f, Widget::Vec, {}, true, 2},
      {"ScrollSpeed.y", "ScrollSpeed.y", "Float", true, 0.5f, -4.0f, 4.0f, Widget::Vec, {}, true, 1},
      // ScrollProbability (Vec2, t3 default (0,0.5)).
      {"ScrollProbability.x", "ScrollProbability", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"ScrollProbability.y", "ScrollProbability.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // ScrollOffset (Float, t3 default 0).
      {"ScrollOffset", "ScrollOffset", "Float", true, 0.0f, -10.0f, 10.0f},
      // Padding (Vec2, t3 default (0.02,0.02)).
      {"Padding.x", "Padding", "Float", true, 0.02f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Padding.y", "Padding.y", "Float", true, 0.02f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Seed (Float, t3 default 42).
      {"Seed", "Seed", "Float", true, 42.0f, 0.0f, 1000.0f},
      // Resolution selector (image-filter convention; t3 default Int2 (0,0) = follow window).
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "RyojiPattern2", cookRyojiPattern2, "ryojipattern2", runRyojiPattern2SelfTest};

// --- RyojiPattern2 MATH golden ----------------------------------------------------------------
// RyojiPattern2 subdivides a unit cell, hash-colors each cel, and (optionally) mixes in ImageA. To
// get a hand-derivable plateau we DEGENERATE the subdivision and time terms with deterministic params:
//   SplitProbability=(0,0)  -> in subDivideCel2 the guard `hash.x>0 && hash.y>0` is TRUE for the
//                              hash22 outputs (in [0,1)) -> every call returns cel unchanged -> cel
//                              stays (0,0,1,1) for the whole frame (no subdivision).
//   ScrollProbability=(0,0) -> scrollFactor=(hash>0?0:1)=0 -> randomShift=0 -> P stays = texCoord.
//   Padding=(0,0)           -> posInCel test `<0` is false -> skip the Background gutter branch.
// Then for EVERY pixel: cel=(0,0,1,1), so
//   hashForCel1 = hash22((0,0)+(1,1)/2) = hash22(0.5,0.5)         [a single constant]
//   hashForCel  = hash12((0,0)+hashForCel1) = hash12(hashForCel1) [a single constant]
// With Contrast=0 -> gray = 1 - hashForCel (a constant); MixOriginal chooses Foreground vs ImageA.
//
// Reference (host float, frac/dot matching the .hlsl hash22/hash12; see /tmp derivation in dossier):
//   hashForCel1 = (0.30428696, 0.31886292), hashForCel = 0.75957108, gray = 0.24042892.
//   HighlightProbability=0 keeps the highlight off (hashForCel1.x=0.3043 >= 0).
//
// CASE A (Background/Foreground ramp): Background=(0,0,0,1), Foreground=(1,1,1,1), MixOriginal=0 ->
//   color = mix(black, white, gray) = (gray,gray,gray,1) -> u8 ~ (61,61,61,255). EVERY pixel equal.
// CASE B (Image mix proof): MixOriginal=1, input texture solid (200,100,50) ->
//   color = mix(black, image, gray) = image*gray -> u8 ~ (48,24,12). Proves ImageA is genuinely
//   sampled and lerped (the degenerate config keeps subdivision off, so this isolates the mix path).
//
// injectBug: scale gray by 0.5 in the shader-equivalent by passing Contrast=1 with ForegroundRatio=0
// -> gray = (hashForCel>0?0:1) = 0 -> Case A output flips to BLACK (~0) instead of ~61 -> the value
// assertion FAILS. (No -bug shader flag exists; we drive the tooth via a param that changes the
// closed-form value, AND the dossier documents the manual shader-break proof.)
int runRyojiPattern2SelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-ryojipattern2] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // Solid input texture (200,100,50,255) for Case B.
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (size_t i = 0; i < (size_t)W * H; ++i) {
    in[i * 4 + 0] = 200; in[i * 4 + 1] = 100; in[i * 4 + 2] = 50; in[i * 4 + 3] = 255;
  }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  // Shared degenerate params (subdivision/time off) + deterministic coloring.
  auto baseParams = [](std::map<std::string, float>& pp) {
    pp["Background.r"] = 0.0f; pp["Background.g"] = 0.0f; pp["Background.b"] = 0.0f; pp["Background.a"] = 1.0f;
    pp["Foreground.r"] = 1.0f; pp["Foreground.g"] = 1.0f; pp["Foreground.b"] = 1.0f; pp["Foreground.a"] = 1.0f;
    // Splits.x=1 makes the FIRST subdivision call (hardcoded splitProbability=(1,0)) an identity:
    // subdiv=(splitA.x,1)=(1,1) -> cel.zw unchanged, floor(P)*1=0 -> cel.xy unchanged. Combined with
    // SplitProbability=(0,0) below (calls 2-6 return early), cel stays (0,0,1,1) for every pixel.
    pp["Splits.x"] = 1.0f; pp["Splits.y"] = 4.0f;
    pp["SplitB.x"] = 1.0f;  pp["SplitB.y"] = 3.0f;
    pp["SplitC.x"] = 1.0f;  pp["SplitC.y"] = 10.0f;
    pp["SplitProbability.x"] = 0.0f; pp["SplitProbability.y"] = 0.0f;     // no subdivision
    pp["ScrollProbability.x"] = 0.0f; pp["ScrollProbability.y"] = 0.0f;   // no scroll
    pp["ScrollSpeed.x"] = 0.0f; pp["ScrollSpeed.y"] = 0.0f;
    pp["ScrollOffset"] = 0.0f;
    pp["Padding.x"] = 0.0f; pp["Padding.y"] = 0.0f;                       // no gutter
    pp["Seed"] = 42.0f;
    pp["HighlightProbability"] = 0.0f;                                    // no highlight
    pp["HighlightSeed"] = 0.0f;
  };

  // CASE A: Contrast=0 -> gray=1-hashForCel ~ 0.2404; MixOriginal=0 -> Foreground=white.
  // injectBug: Contrast=1 + ForegroundRatio=0 -> gray=(hashForCel>0?0:1)=0 -> black.
  std::vector<uint8_t> outA((size_t)W * H * 4, 0);
  {
    std::map<std::string, float> pp; baseParams(pp);
    pp["MixOriginal"] = 0.0f;
    pp["Contrast"] = injectBug ? 1.0f : 0.0f;
    pp["ForegroundRatio"] = injectBug ? 0.0f : 0.50333333f;
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = nullptr; c.output = dst;  // generator: no input
    c.params = &pp;
    cookRyojiPattern2(c);
    dst->getBytes(outA.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  }

  // CASE B: MixOriginal=1, Contrast=0 -> color = mix(black, image, gray) = image*gray ~ (48,24,12).
  std::vector<uint8_t> outB((size_t)W * H * 4, 0);
  {
    std::map<std::string, float> pp; baseParams(pp);
    pp["MixOriginal"] = 1.0f;
    pp["Contrast"] = 0.0f;
    pp["ForegroundRatio"] = 0.50333333f;
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = src; c.output = dst;
    c.params = &pp;
    cookRyojiPattern2(c);
    dst->getBytes(outB.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  }

  // Probe two distant pixels per case -> flatness; assert closed-form values.
  auto px = [&](const std::vector<uint8_t>& v, uint32_t x, uint32_t y, int ch) {
    return (int)v[((size_t)y * W + x) * 4 + ch];
  };
  const int EXP_A = 61;  // gray*255 ~ 0.24042892*255 = 61.3
  const int EXP_BR = 48, EXP_BG = 24, EXP_BB = 12;  // image*gray
  const int TOL = 3;

  // Case A: flat + ~61 grey (or ~0 black when buggy).
  int a00r = px(outA, 5, 5, 0), a00g = px(outA, 5, 5, 1), a00b = px(outA, 5, 5, 2);
  int a11r = px(outA, 50, 40, 0);
  bool aFlat = std::abs(a00r - a11r) <= TOL;
  bool aGrey = std::abs(a00r - a00g) <= TOL && std::abs(a00g - a00b) <= TOL;
  bool aValue = std::abs(a00r - EXP_A) <= TOL;

  // Case B: flat + image*gray.
  int b00r = px(outB, 5, 5, 0), b00g = px(outB, 5, 5, 1), b00b = px(outB, 5, 5, 2);
  int b11r = px(outB, 50, 40, 0);
  bool bFlat = std::abs(b00r - b11r) <= TOL;
  bool bValue = std::abs(b00r - EXP_BR) <= TOL && std::abs(b00g - EXP_BG) <= TOL &&
                std::abs(b00b - EXP_BB) <= TOL;

  bool pass = aFlat && aGrey && aValue && bFlat && bValue;
  printf("[selftest-ryojipattern2] A(R=%d,G=%d,B=%d exp~%d flat=%d grey=%d val=%d) "
         "B(R=%d,G=%d,B=%d exp~(%d,%d,%d) flat=%d val=%d) -> %s\n",
         a00r, a00g, a00b, EXP_A, aFlat ? 1 : 0, aGrey ? 1 : 0, aValue ? 1 : 0,
         b00r, b00g, b00b, EXP_BR, EXP_BG, EXP_BB, bFlat ? 1 : 0, bValue ? 1 : 0,
         pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
