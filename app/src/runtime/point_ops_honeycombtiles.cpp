// HoneyCombTiles — TiXL-ported hexagonal-tile stylize IMAGE FILTER (lane stylize, image/fx/stylize).
// Wraps HexGridDisplace.hlsl (faithful port: app/shaders/honeycombtiles.metal). ONE fullscreen pass:
// ImageA @ texture(0), an internally-baked 2-row "Effects" curve LUT @ texture(1).
//
// ── STEP-0 (the three vetted risks — all CLEARED, see honeycombtiles_params.h for the full trace) ──
//   (1) CurvesToTexture mask (t1): the .t3 bakes TWO embedded CONSTANT curves through a Horizontal
//       CurvesToTexture -> a 2-row R32_Float LUT. NOT a blocked seam — the curve currency (sw::Curve),
//       the sampler, and the in-cook bake-and-bind technique (MapPointAttributes::makeRowTex) all
//       already exist. This op holds the two curves as embedded sw::Curve constants and bakes the LUT
//       in-cook (no external wire, no driver dependency).
//   (2) "Two-pass": NOT two passes. _multiImageFxSetup (c290c47c) output is UNCONSUMED (a TiXL
//       authoring leftover); only _multiImageFxSetupStatic (4a503a94) drives TextureOutput => ONE pass.
//   (3) FloatsToBuffer routing: clean 1:1 (the 14 FloatParams connection-order maps straight to the
//       cbuffer field order; the Vector4/Vector2Components are identity splitters, no intervening math).
//
// SAMPLER (load-bearing): s0 = the _multiImageFxSetupStatic sampler MultiInput[0] = the WrapMode-driven
// SamplerState. HoneyCombTiles leaves WrapMode unwired -> default "Wrap" => Metal Repeat address;
// Filter default "MinMagPointMipLinear" => Point min/mag (Mip Linear, moot for single-level
// textures). ImageA AND the LUT share this single s0.
//
// TARGET SIZE: HexGridDisplace.hlsl reads TargetWidth/TargetHeight from a SEPARATE cbuffer b1
// (framework-injected render-target size). We inject it from c.output dims at buffer index 1
// (HONEYCOMBTILES_TexSize) — the rings.cpp precedent.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/curve.h"                     // sw::Curve / VDefinition (the embedded LUT curves)
#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/honeycombtiles_params.h"     // HoneyCombTilesParams, HONEYCOMBTILES_*
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"               // TexCookCtx, cookParam
#include "runtime/tex_op_cache.h"              // cachedTexPSO

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// CurvesToTexture.cs default SampleSize (the LUT column count). Horizontal -> width = sampleCount,
// height = curveCount (2). value = curve.sample(i/sampleCount) for i in [0,sampleCount).
constexpr int kLutSampleCount = 256;

// ── The two embedded "Effects" curves (HoneyCombTiles.t3 SampleCurve children, verbatim) ──
// MultiInput connection order into CurvesToTexture b14b3243: [0]=2491a6d0 (row 0), [1]=d466a50a (row 1).
// The shader samples row 0 at float2(value,0) (the value remap) and row 1 at float2(value,0.75)
// (edgeEffect). Each key's interpolation comes from ConvertLegacy(InType, InEditMode)
// (VDefinition.cs:160): Linear->Linear, (Spline,Tangent)->Tangent (Tangent honors stored angles, NOT
// recomputed). updateTangents (auto via addOrUpdate) then recomputes the spline-mode angles and LEAVES
// the Tangent-mode angles untouched — matching TiXL on .t3 load.

// Row 0: keys (0,0)Linear -> (1.0357, 1.72)Tangent[in 0.50118, out -2.64041].
const Curve& effectsCurveRow0() {
  static const Curve c = []() {
    Curve c;
    c.preCurveMapping = OutsideBehavior::Constant;
    c.postCurveMapping = OutsideBehavior::Constant;
    VDefinition k0;
    k0.u = 0.0; k0.value = 0.0;
    k0.inInterpolation = KeyInterpolation::Linear;
    k0.outInterpolation = KeyInterpolation::Linear;
    VDefinition k1;
    k1.u = 1.0357; k1.value = 1.7199996709823608;
    k1.inInterpolation = KeyInterpolation::Tangent;
    k1.outInterpolation = KeyInterpolation::Tangent;
    k1.inTangentAngle = 0.5011849403381348;
    k1.outTangentAngle = -2.6404077132516584;
    c.addOrUpdate(0.0, k0);
    c.addOrUpdate(1.0357, k1);
    return c;
  }();
  return c;
}

// Row 1: keys (0,0)Linear -> (1.0, 0.18169)Tangent[in 0.20686, out -2.93473].
const Curve& effectsCurveRow1() {
  static const Curve c = []() {
    Curve c;
    c.preCurveMapping = OutsideBehavior::Constant;
    c.postCurveMapping = OutsideBehavior::Constant;
    VDefinition k0;
    k0.u = 0.0; k0.value = 0.0;
    k0.inInterpolation = KeyInterpolation::Linear;
    k0.outInterpolation = KeyInterpolation::Linear;
    VDefinition k1;
    k1.u = 1.0; k1.value = 0.18168886005878448;
    k1.inInterpolation = KeyInterpolation::Tangent;
    k1.outInterpolation = KeyInterpolation::Tangent;
    k1.inTangentAngle = 0.20686151087284088;
    k1.outTangentAngle = -2.9347311427169522;
    c.addOrUpdate(0.0, k0);
    c.addOrUpdate(1.0, k1);
    return c;
  }();
  return c;
}

// injectBug hook (golden only): when set, the baked LUT is forced to all-1.0 (instead of the real
// curve samples). At the golden's value=0 input this flips value 0->1 and edgeEffect 0->1, so the
// Background plateau (c==0 -> col==Background) breaks into a position-dependent smoothstep -> RED.
bool g_hctInjectBug = false;

// Allocate a single-row scratch texture (R32_Float), upload `host`. ShaderRead, Shared. Caller
// releases. (= MapPointAttributes::makeRowTex, kept local to avoid a cross-leaf link.)
MTL::Texture* makeR32Tex(MTL::Device* dev, uint32_t width, uint32_t height, const float* host) {
  if (width == 0) width = 1;
  if (height == 0) height = 1;
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatR32Float, width, height, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  t->replaceRegion(MTL::Region::Make2D(0, 0, width, height), 0, host,
                   (NS::UInteger)(width * sizeof(float)));
  return t;
}

// Bake the 2-row Effects LUT (Horizontal CurvesToTexture: row 0 = effectsCurveRow0, row 1 =
// effectsCurveRow1). Row-major: host[row*W + i] = curve.sample(i/sampleCount). (CurvesToTexture.cs:84
// divisor = sampleCount, NOT sampleCount-1.)
MTL::Texture* bakeEffectsLut(MTL::Device* dev) {
  const int W = kLutSampleCount;
  std::vector<float> host((size_t)W * 2, 0.0f);
  if (g_hctInjectBug) {
    std::fill(host.begin(), host.end(), 1.0f);  // tooth: corrupt the LUT
  } else {
    const Curve& r0 = effectsCurveRow0();
    const Curve& r1 = effectsCurveRow1();
    for (int i = 0; i < W; ++i) {
      double t = (double)((float)i / (float)W);
      host[(size_t)0 * W + i] = (float)r0.sample(t);
      host[(size_t)1 * W + i] = (float)r1.sample(t);
    }
  }
  return makeR32Tex(dev, (uint32_t)W, 2, host.data());
}

// Clear `out` to black (no ImageA -> nothing to render; mirrors cookDisplace's empty path).
void clearTexture(MTL::CommandQueue* q, MTL::Texture* out) {
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(out);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = q->commandBuffer();
  cmd->renderCommandEncoder(pass)->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

// HoneyCombTiles cook: bake the Effects LUT, then one fullscreen pass sampling ImageA (t0) + LUT (t1).
void cookHoneyCombTiles(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  const MTL::Texture* imageA = c.inputTextureCount > 0 ? c.inputTextures[0] : c.inputTexture;
  if (!imageA) { clearTexture(c.queue, c.output); return; }  // no ImageA -> nothing to render

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "honeycombtiles_vs", "honeycombtiles_fs", fmt);
  if (!rps) return;

  // Sampler s0 = WrapMode default "Wrap" -> Repeat; Filter "MinMagPointMipLinear" -> Point min/mag
  // (Mip Linear, moot for single-level textures). Shared by ImageA AND the LUT (the .hlsl has a
  // single texSampler:register(s0)) -> Point picks the nearest source texel (blocky, not blended).
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterNearest);
  sd->setMagFilter(MTL::SamplerMinMagFilterNearest);
  sd->setSAddressMode(MTL::SamplerAddressModeRepeat);
  sd->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  MTL::Texture* lut = bakeEffectsLut(c.dev);

  // Params (HoneyCombTiles.t3 defaults). Center -> the shader's Offset field.
  HoneyCombTilesParams p{};
  p.FillR = cookParam(c, "Fill.r", 1.0f);
  p.FillG = cookParam(c, "Fill.g", 1.0f);
  p.FillB = cookParam(c, "Fill.b", 1.0f);
  p.FillA = cookParam(c, "Fill.a", 1.0f);
  p.BackgroundR = cookParam(c, "Background.r", 1.0f);
  p.BackgroundG = cookParam(c, "Background.g", 0.99999f);
  p.BackgroundB = cookParam(c, "Background.b", 0.99999f);
  p.BackgroundA = cookParam(c, "Background.a", 0.804f);
  p.OffsetX = cookParam(c, "Center.x", 0.0f);
  p.OffsetY = cookParam(c, "Center.y", 0.0f);
  p.Divisions = cookParam(c, "Divisions", 20.0f);
  p.LineThickness = cookParam(c, "LineThickness", 0.0f);
  p.MixOriginal = cookParam(c, "MixOriginal", 0.0f);
  p.Rotation = cookParam(c, "Rotation", 0.0f);

  // b1 TimeConstants (framework-injected target size from output dims).
  float texSize[2] = {(float)c.output->width(), (float)c.output->height()};

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(imageA), 0);  // t0 = ImageA
  enc->setFragmentTexture(lut, 1);                                // t1 = Effects LUT
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(HoneyCombTilesParams), HONEYCOMBTILES_Params);
  enc->setFragmentBytes(texSize, sizeof(texSize), HONEYCOMBTILES_TexSize);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();
  lut->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

int runHoneyCombTilesSelfTest(bool injectBug);

// Self-registration. Ports 1:1 from HoneyCombTiles.cs / .t3 defaults. ImageA (one Texture2D input) +
// Fill/Background (Vec4) + Center (Vec2 -> shader Offset) + Divisions/LineThickness/MixOriginal/Rotation
// + Resolution (Int2). FORKS (named in header/shader): the Effects LUT is baked in-cook from two
// embedded constant curves (CurvesToTexture parity); s0 = Repeat/Linear (WrapMode default "Wrap").
static const ImageFilterOp _reg_honeycombtiles{
    {"HoneyCombTiles", "HoneyCombTiles",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Fill (Vec4, TiXL default (1,1,1,1))
      {"Fill.r", "Fill", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Fill.g", "Fill.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Fill.b", "Fill.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Fill.a", "Fill.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Background (Vec4, TiXL default (1, 0.99999, 0.99999, 0.804))
      {"Background.r", "Background", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Background.g", "Background.g", "Float", true, 0.99999f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.b", "Background.b", "Float", true, 0.99999f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.a", "Background.a", "Float", true, 0.804f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Center (Vec2 -> shader Offset, TiXL default (0,0))
      {"Center.x", "Center", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Center.y", "Center.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Divisions (float, TiXL default 20)
      {"Divisions", "Divisions", "Float", true, 20.0f, 1.0f, 200.0f, Widget::Slider},
      // LineThickness (float, TiXL default 0)
      {"LineThickness", "LineThickness", "Float", true, 0.0f, 0.0f, 100.0f, Widget::Slider},
      // MixOriginal (float, TiXL default 0)
      {"MixOriginal", "MixOriginal", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider},
      // Rotation (float, TiXL default 0, degrees)
      {"Rotation", "Rotation", "Float", true, 0.0f, -180.0f, 180.0f},
      // Resolution (Int2 enum, output size selector — rail convention)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "HoneyCombTiles", cookHoneyCombTiles, "honeycombtiles", runHoneyCombTilesSelfTest};


// --- Golden bridge (the EXACT-PIXEL golden lives in point_ops_honeycombtiles_golden.cpp, kept
// separate to satisfy the ≤400-line ratchet). The golden cooks through this thin non-anonymous
// shim so it can drive the (anonymous) cook + flip the injectBug LUT-corruption flag. ---
void hctGoldenCook(TexCookCtx& c, bool injectBug) {
  g_hctInjectBug = injectBug;
  cookHoneyCombTiles(c);
  g_hctInjectBug = false;
}

}  // namespace sw
