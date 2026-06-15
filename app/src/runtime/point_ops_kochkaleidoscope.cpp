// KochKaleidoskope image-filter texture op (image/fx/distort) — fractal Koch-snowflake kaleidoscope.
// TiXL authority:
//   Operators/Lib/image/fx/distort/KochKaleidoskope.cs  ([Input] order:
//     Image(Texture2D)->Offset(Vector2)->Angle(float)->Steps(int)->Rotate(float)->Scale(float)->
//     Center(Vector2)->ShadeSteps(float)->ShadeFolds(float)->Resolution(Int2))
//   KochKaleidoskope.t3 defaults: Angle=3.0, Steps=6, Scale=0, others 0; sampler=Wrap.
//   Operators/Lib/Assets/shaders/img/fx/KochKaleidoscope.hlsl (self-contained single-pass kernel;
//     cbuffer ParamConstants ORDER = Scale,CenterX,CenterY,OffsetX,OffsetY,Angle,Steps,ShadeSteps,
//     ShadeFolds,Rotate — NOTE ≠ .cs [Input] order, the host struct follows the cbuffer).
//
// Single-pass port: cookKochKaleidoscope reads c.inputTexture (the upstream RenderTarget's Texture2D
// via the I1 gather direct-through), runs one fullscreen pass of kochkaleidoscope_vs/_fs (which does
// a 4-tap AA average internally), writes c.output.
//
// FORKS (named — full detail in kochkaleidoscope.metal header):
//  [fork-resolution] KochKaleidoskope.cs Resolution(Int2) picks the output size; TexCookCtx has no
//    resolution seam, so the op writes c.output's EXISTING size. The aspect/AA-offset that the .hlsl
//    derives from the Resolution cbuffer is fed from c.output's actual dims. Resolution is LISTED in
//    the NodeSpec (in .cs [Input] order, as the standard WindowFollow/HD720/... enum used by the
//    other image filters) but the cook uses c.output's real size — honest seam, not an invented knob.
//  [fork-sampler] Fixed Wrap(repeat) sampler — matches KochKaleidoskope.t3 (the kaleidoscope tiles
//    its sampled uv, so repeat addressing is load-bearing, unlike Tint/ConvertColors' clamp).
//  [fork-loop-bound] cbuffer Steps is float; the .hlsl loop `i<Steps` is int — MSL casts (int)Steps.
//
// Self-contained leaf: cookKochKaleidoscope + runKochKaleidoscopeSelfTest + file-scope registrar.
// Shares the PSO+scratch cache seam (tex_op_cache.h) with Tint/Blur/Displace/ConvertColors.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/kochkaleidoscope_params.h"   // KochKaleidoscopeParams, KK_Params
#include "runtime/point_graph.h"               // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"              // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward decl (defined at file end; referenced by the registrar literal). Kept file-scope, NOT in
// point_ops.h — this leaf owns its selftest, no shared header edit.
int runKochKaleidoscopeSelfTest(bool injectBug);

namespace {

// KochKaleidoskope texture op: single pass (the kernel does its own 4-tap AA). Reads c.inputTexture,
// writes c.output. No upstream texture wired: clear output to black — mirrors cookConvertColors.
void cookKochKaleidoscope(TexCookCtx& c) {
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
      cachedTexPSO(c.dev, c.lib, "kochkaleidoscope_vs", "kochkaleidoscope_fs", fmt);
  if (!rps) return;

  // fork-sampler: Wrap(repeat), matching KochKaleidoskope.t3 (the kaleidoscope tiles its sampled uv).
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeRepeat);
  sd->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // Params follow the cbuffer ParamConstants order (see kochkaleidoscope_params.h). The .cs [Input]
  // names map onto cbuffer fields; Vector2 Offset/Center are split into .x/.y Float ports.
  // t3 defaults: Angle=3.0, Steps=6, Scale=0, others 0.
  KochKaleidoscopeParams p{};
  p.Scale      = cookParam(c, "Scale", 0.0f);
  p.CenterX    = cookParam(c, "Center.x", 0.0f);
  p.CenterY    = cookParam(c, "Center.y", 0.0f);
  p.OffsetX    = cookParam(c, "Offset.x", 0.0f);
  p.OffsetY    = cookParam(c, "Offset.y", 0.0f);
  p.Angle      = cookParam(c, "Angle", 3.0f);
  p.Steps      = cookParam(c, "Steps", 6.0f);
  p.ShadeSteps = cookParam(c, "ShadeSteps", 0.0f);
  p.ShadeFolds = cookParam(c, "ShadeFolds", 0.0f);
  p.Rotate     = cookParam(c, "Rotate", 0.0f);
  p.TargetWidth  = (float)c.output->width();   // fork-resolution: c.output's real dims feed aspect/AA
  p.TargetHeight = (float)c.output->height();

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
  enc->setFragmentBytes(&p, sizeof(KochKaleidoscopeParams), KK_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

// Self-registration (replaces node_registry spec + register line + kTable row).
static const ImageFilterOp _reg_kochkaleidoscope{
    // KochKaleidoskope (TiXL Lib.image.fx.distort.KochKaleidoskope): fractal Koch-snowflake
    // kaleidoscope. Ports mirror KochKaleidoskope.cs [Input] order VERBATIM:
    //   Image -> Offset(Vec2) -> Angle -> Steps -> Rotate -> Scale -> Center(Vec2) ->
    //   ShadeSteps -> ShadeFolds -> Resolution.
    // NOTE TiXL op type name is spelled "KochKaleidoskope" (k, not c) — our cookType/file/selftest
    // use "kochkaleidoscope" (c). t3 defaults: Angle=3.0, Steps=6, Scale=0, others 0.
    {"KochKaleidoskope", "KochKaleidoskope",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Offset (Vector2, .cs:12-13) — split into .x/.y Float ports (Widget::Vec head + component).
      {"Offset.x", "Offset", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Offset.y", "Offset.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Angle (float, .cs:15-16, t3 default 3.0).
      {"Angle", "Angle", "Float", true, 3.0f, -180.0f, 180.0f},
      // Steps (int, .cs:18-19, t3 default 6) — int enum-less, carried as a Float port.
      {"Steps", "Steps", "Float", true, 6.0f, 1.0f, 32.0f},
      // Rotate (float, .cs:21-22, t3 default 0).
      {"Rotate", "Rotate", "Float", true, 0.0f, -180.0f, 180.0f},
      // Scale (float, .cs:24-25, t3 default 0).
      {"Scale", "Scale", "Float", true, 0.0f, 0.0f, 10.0f},
      // Center (Vector2, .cs:27-28) — split into .x/.y Float ports.
      {"Center.x", "Center", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Center.y", "Center.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // ShadeSteps (float, .cs:30-31, t3 default 0).
      {"ShadeSteps", "ShadeSteps", "Float", true, 0.0f, -2.0f, 2.0f},
      // ShadeFolds (float, .cs:33-34, t3 default 0).
      {"ShadeFolds", "ShadeFolds", "Float", true, 0.0f, -2.0f, 2.0f},
      // Resolution (Int2, .cs:36-37) — fork-resolution: LISTED as the standard size enum (same as
      // RenderTarget/Blur), but the cook uses c.output's real dims. NO-OP placeholder enum.
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "KochKaleidoskope", cookKochKaleidoscope, "kochkaleidoscope", runKochKaleidoscopeSelfTest};

// --- KochKaleidoskope MATH golden (headless, real GPU assertion) -------------------------------
// The source is a SOLID mid-grey texture, so every tap samples the SAME constant color; the only
// thing the kernel varies per-pixel is the darkening `c.rgb -= foldCount / Steps`. The output center
// pixel therefore equals  grey - mean_over_4_taps(foldCount_tap / Steps).  We compute foldCount for
// each of the 4 AA taps on the CPU with a VERBATIM mirror of the .hlsl fold loop, average, and assert
// the GPU center pixel matches  grey - mean(foldCount/Steps)  (±2/255).
//
// injectBug flips the loop bound to `i <= Steps` (off-by-one extra fold iteration) in the CPU
// expectation — one extra iteration changes foldCount, so the (correct) GPU center no longer matches
// the (now wrong) CPU expectation → FAIL rc=1. This pins the int-vs-float loop-bound parity risk.
//
// PARAM CHOICE: with ShadeFolds at the center the fold side never lands on d<0 (foldCount stays 0 →
// no discrimination). ShadeSteps adds `d * ShadeSteps` EVERY iteration, so the off-by-one ALWAYS
// shifts foldCount. We use a SMALL ShadeSteps (0.004) so the accumulated foldCount/Steps darkening
// lands the center in the mid-range (clean≈104, bug≈54 of 255 — both un-clamped genuine readbacks,
// not 0/clamp artifacts). The off-by-one then moves the center by ~50/255 = a hard bite.
namespace {

struct V2 { float x, y; };
static inline V2 getDir(float a) { return {std::sin(a), std::cos(a)}; }
static inline float dot2(V2 a, V2 b) { return a.x * b.x + a.y * b.y; }

// Verbatim CPU mirror of KochKaleidoscope.hlsl fold loop for the constant-color case: returns
// foldCount for a given starting uv. `extraIter` (injectBug) makes the loop run `i <= Steps`.
float cpuFoldCount(V2 uv, float Scale, float Angle, float Steps, float ShadeSteps, float ShadeFolds,
                   float rotCos, float rotSin, bool extraIter) {
  // rotation: HLSL mul(rotation,uv) = (cos*x - sin*y, sin*x + cos*y).
  V2 r{rotCos * uv.x - rotSin * uv.y, rotSin * uv.x + rotCos * uv.y};
  uv = r;
  uv.x *= Scale; uv.y *= Scale;
  uv.x = std::fabs(uv.x);

  V2 n = getDir((5.0f / 6.0f) * 3.1415f);
  uv.y += std::tan((5.0f / 6.0f) * 3.1415f) * 0.5f;
  float d = dot2({uv.x - 0.5f, uv.y - 0.0f}, n);
  float m = std::max(0.0f, d);
  uv.x -= m * n.x * 2.0f; uv.y -= m * n.y * 2.0f;

  float foldCount = 0.0f;
  n = getDir(Angle * (2.0f / 3.0f) * 3.1415f / 90.0f);
  uv.x += 0.5f;

  int last = (int)Steps + (extraIter ? 1 : 0);  // BUG: i<=Steps  vs  correct i<Steps
  for (int i = 1; i < last; i++) {
    uv.x *= 3.0f; uv.y *= 3.0f;
    uv.x -= 1.5f;
    uv.x = std::fabs(uv.x);
    uv.x -= 0.5f;
    d = dot2(uv, n);
    float foldSideShade = d < 0.0f ? 1.0f : 0.0f;
    foldCount += foldSideShade * ShadeFolds;
    foldCount += d * ShadeSteps;
    float foldFactor = std::min(0.0f, d);
    uv.x -= foldFactor * n.x * 2.0f; uv.y -= foldFactor * n.y * 2.0f;
  }
  return foldCount;
}

}  // namespace

int runKochKaleidoscopeSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();  // fresh device: drop PSOs/scratch built on a now-released device
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-kochkaleidoscope] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  const uint8_t GREY = 128;
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (size_t i = 0; i < (size_t)W * H; ++i) {
    in[i * 4 + 0] = GREY; in[i * 4 + 1] = GREY; in[i * 4 + 2] = GREY; in[i * 4 + 3] = 255;
  }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  // Params: Steps=6, Angle=3, Scale=1, ShadeSteps=0.004 (drives a mid-range darkening so the
  // off-by-one BITES without clamping), ShadeFolds=0, everything else 0. Center=0 → center pixel
  // uv path is deterministic.
  const float kSteps = 6.0f, kAngle = 3.0f, kScale = 1.0f, kShadeFolds = 0.0f, kShadeSteps = 0.004f;
  std::map<std::string, float> params;
  params["Steps"] = kSteps; params["Angle"] = kAngle; params["Scale"] = kScale;
  params["ShadeFolds"] = kShadeFolds; params["ShadeSteps"] = kShadeSteps;
  // (Offset/Center/Rotate default 0.)
  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookKochKaleidoscope(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  size_t ci = ((size_t)(H / 2) * W + (W / 2)) * 4;
  int gR = out[ci], gG = out[ci + 1], gB = out[ci + 2];

  // CPU expectation: mirror psMain for the center pixel.
  //   texCoord at pixel (W/2,H/2) center = ((W/2)+0.5)/W = 0.5 + 0.5/W (≈0.5078 for W=64).
  float tx = ((float)(W / 2) + 0.5f) / (float)W;
  float ty = ((float)(H / 2) + 0.5f) / (float)H;
  float aspect = (float)W / (float)H;  // 1.0 for square
  // rotation = rotate2d(Rotate/180*pi), Rotate=0 → identity.
  float ang = 0.0f / 180.0f * 3.141592f;
  float rotCos = std::cos(ang), rotSin = std::sin(ang);
  // uv -= (CenterX, 1-CenterY); Center=0 → uv -= (0,1).
  V2 uvBase{tx - 0.0f, ty - (1.0f - 0.0f)};
  uvBase.x *= aspect;
  float strength = 0.37f;
  V2 offset{strength / (float)W, strength / (float)H};
  V2 taps[4] = {{uvBase.x + offset.x, uvBase.y}, {uvBase.x - offset.x, uvBase.y},
                {uvBase.x, uvBase.y + offset.y}, {uvBase.x, uvBase.y - offset.y}};
  float foldSum = 0.0f;
  for (int t = 0; t < 4; ++t)
    foldSum += cpuFoldCount(taps[t], kScale, kAngle, kSteps, kShadeSteps, kShadeFolds,
                            rotCos, rotSin, injectBug);
  float meanFold = foldSum / 4.0f;
  // c.rgb -= foldCount/Steps  →  output = grey - meanFold/Steps, then clamp [0,1000]→display [0,1].
  float grey = (float)GREY / 255.0f;
  float expectF = grey - meanFold / kSteps;
  expectF = std::max(0.0f, std::min(1.0f, expectF));
  int e = (int)std::lround(expectF * 255.0f);

  // ±4 tolerance: foldCount accumulates d*3^i over the fold loop (a large intermediate scaled by a
  // small ShadeSteps), so GPU↔CPU float rounding can drift a couple LSBs more than a flat op. The
  // off-by-one delta is ~50/255, so ±4 keeps a wide bite margin while absorbing that drift.
  bool pass = std::abs(gR - e) <= 4 && std::abs(gG - e) <= 4 && std::abs(gB - e) <= 4;
  printf("[selftest-kochkaleidoscope] center gpu=(%d,%d,%d) expect=%d (meanFold=%.4f) -> %s\n",
         gR, gG, gB, e, meanFold, pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
