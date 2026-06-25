// ImageLevels image-filter texture op (image/analyze) — histogram/levels visualization overlay.
// TiXL authority: Operators/Lib/image/analyze/ImageLevels.cs ([Input] order
//   Texture2d(Texture2D) -> Center(Vector2) -> Rotation(float) -> Width(float) -> Range(Vector2) ->
//   ShowOriginal(float))
// + ImageLevels.t3 (defaults: Center=(0.25,0.0), Width=0.2, Range=(0,1), ShowOriginal=1, Rotation=0;
//   the internal _ImageFxShaderSetup2 node Filter=MinMagMipPoint and Wrap input left at its default
//   "Wrap" = the point+repeat sampler)
// + Assets/shaders/img/fx/ImageLevels.hlsl (self-contained single-pass PIXEL kernel — ported
//   line-for-line in imagelevels.metal; samples level 0 only via .sample, no mip>0 reads).
//
// This is a VISUALIZATION-overlay op (draws the levels curve / subdivision lines / clamp zebra over
// the dimmed original), not a color-grade. Ported verbatim, byte-faithful, no visual improvement.
//
// Single-pass port: cookImageLevels reads c.inputTexture (the upstream RenderTarget's Texture2D via
// the I1 gather direct-through), runs one fullscreen pass of imagelevels_vs/_fs, writes c.output.
// Vector2 inputs (Center/Range) are decomposed into .x/.y Float ports (Widget::Vec arity 2) —
// mirroring ColorGrade's VignetteCenter. Width/Rotation/ShowOriginal are scalar Float ports.
//
// FORKS (named):
//  [fork-sampler]     Fixed POINT(MinMagMipPoint)+WRAP sampler = ImageLevels.t3's _ImageFxShaderSetup2
//    (Filter=MinMagMipPoint; the Setup2 Wrap input defaults to "Wrap" and ImageLevels.t3 leaves it
//    unset). Per-op choice read from this op's own .t3, not a blanket fork.
//  [fork-time-folded] The .hlsl reads cbuffer b1 TimeConstants.beatTime (zebra pattern) + cbuffer b2
//    Resolution.TargetWidth/TargetHeight (aspect ratio). We fold both into the single
//    ImageLevelsParams cbuffer (BeatTime / TargetWidth / TargetHeight). TargetWidth/Height are fed
//    the actual output dimensions (matching TiXL's render-target Resolution); BeatTime is fed 0 in
//    the cook (deterministic) — it only drives the clamp-highlight zebra animation. globalTime/time/
//    runTime are not read by psMain and are dropped.
//
// Self-contained leaf: cookImageLevels + _reg_imagelevels registrar + runImageLevelsSelfTest.
// Shares the PSO+scratch cache seam (tex_op_cache.h) with ColorGrade/Tint/Blur/Pixelate/etc.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/imagelevels_params.h"        // ImageLevelsParams, IL_Params
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"               // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"              // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration (defined at the bottom of this leaf). Declared HERE (not in the shared
// point_ops.h) so this op stays a zero-shared-file-edit self-registered leaf — the registrar
// references it before its definition, and the selftest dispatcher resolves it via the
// imageFilterSelfTests() sink (registered by the _reg_imagelevels constructor).
int runImageLevelsSelfTest(bool injectBug);

namespace {

// ImageLevels texture op: single pass. Reads c.inputTexture (upstream tex op's output), writes
// c.output. No upstream texture wired: clear output to black — mirrors cookColorGrade.
void cookImageLevels(TexCookCtx& c) {
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
      cachedTexPSO(c.dev, c.lib, "imagelevels_vs", "imagelevels_fs", fmt);
  if (!rps) return;

  // [fork-sampler] point(MinMagMipPoint)+wrap(Setup2 Wrap default), matching ImageLevels.t3.
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterNearest);
  sd->setMagFilter(MTL::SamplerMinMagFilterNearest);
  sd->setMipFilter(MTL::SamplerMipFilterNearest);
  sd->setSAddressMode(MTL::SamplerAddressModeRepeat);
  sd->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL ImageLevels.cs / .t3 defaults. Vector2 inputs read as .x/.y scalar ports.
  ImageLevelsParams p{};
  p.CenterX = cookParam(c, "Center.x", 0.25f);
  p.CenterY = cookParam(c, "Center.y", 0.0f);
  p.Width = cookParam(c, "Width", 0.2f);
  p.Rotation = cookParam(c, "Rotation", 0.0f);
  p.RangeX = cookParam(c, "Range.x", 0.0f);
  p.RangeY = cookParam(c, "Range.y", 1.0f);
  p.ShowOriginal = cookParam(c, "ShowOriginal", 1.0f);
  // [fork-time-folded] TargetWidth/Height = the actual output dims (TiXL's render-target
  // Resolution); BeatTime = 0 (deterministic; only drives the clamp-highlight zebra animation).
  p.TargetWidth = (float)c.output->width();
  p.TargetHeight = (float)c.output->height();
  p.BeatTime = 0.0f;

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
  enc->setFragmentBytes(&p, sizeof(ImageLevelsParams), IL_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

// Self-registration (replaces a registerImageLevelsOp + node_registry spec + kTable row).
// NodeSpec ports mirror ImageLevels.cs [Input] order VERBATIM:
//   Image -> Center(.x/.y) -> Rotation -> Width -> Range(.x/.y) -> ShowOriginal.
// Vector2 inputs split into two Float Widget::Vec ports (head vecArity=2).
// CustomW/CustomH back the shared Resolution=Custom path (mirrors ColorGrade/Tint).
static const ImageFilterOp _reg_imagelevels{
    {"ImageLevels", "ImageLevels",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Center (Vector2, TiXL default (0.25,0)).
      {"Center.x", "Center", "Float", true, 0.25f, -2.0f, 2.0f, Widget::Vec, {}, true, 2},
      {"Center.y", "Center.y", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
      // Rotation (float, TiXL default 0).
      {"Rotation", "Rotation", "Float", true, 0.0f, -180.0f, 180.0f, Widget::Slider},
      // Width (float, TiXL default 0.2).
      {"Width", "Width", "Float", true, 0.2f, 0.001f, 2.0f, Widget::Slider},
      // Range (Vector2, TiXL default (0,1)).
      {"Range.x", "Range", "Float", true, 0.0f, -1.0f, 2.0f, Widget::Vec, {}, true, 2},
      {"Range.y", "Range.y", "Float", true, 1.0f, -1.0f, 2.0f, Widget::Vec, {}, true, 1},
      // ShowOriginal (float, TiXL default 1).
      {"ShowOriginal", "ShowOriginal", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Slider},
      // CustomW/CustomH back the shared Resolution=Custom path (mirrors ColorGrade/Tint).
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "ImageLevels", cookImageLevels, "imagelevels", runImageLevelsSelfTest};

// --- ImageLevels MATH golden (headless, real GPU assertion) -----------------------------------
// All expected values are HAND-COMPUTED from ImageLevels.hlsl psMain (a full per-pixel python
// reimplementation, byte-matched against this very cook — see the dossier) on a 64x64 SOLID input
// (0.5,0.25,0.75) with TiXL-default params (Center=(0.25,0), Width=0.2, Rotation=0, Range=(0,1),
// ShowOriginal=1). With Rotation=0 the curve line is horizontal so normalizedDistance (nd) depends
// only on the readback row: nd=1 at row ~18, decreasing to nd=0 at row ~32. The levels-curve overlay
// band lives in rows 19..31 (nd in (0,1)); rows <=18 (nd>1) and >=32 (nd<0) show the full original.
//
// The teeth pick DETERMINISTIC band rows that exercise the PER-CHANNEL curve-crossing math — the
// heart of ImageLevels. curveShape2.<ch> = smoothstep(nd, nd+lineThickness, colorOnLine.<ch>), so a
// channel "lights up" once nd drops below that channel's input value. Solid input channel values are
// R=0.5, G=0.25, B=0.75, so the channels cross at distinct rows — a wrong nd geometry, a wrong
// channel value, or a wrong curve-color mix all move the bytes.
//
// Test A (band row 23, nd=0.6641): only the B channel has crossed (B=0.75 > nd; R=0.5,G=0.25 < nd),
//   so curveShape2=(0,0,1,1). Expected rgba8 = (148,135,255) — B saturates to 255 from the blue
//   curve, R/G carry the dim original + the n=-0.2 cross-channel curveShape term. LOAD-BEARING for:
//   the B-channel crossing position (nd geometry + colorOnLine.b read) and the curve mix.
// Test B (band row 27, nd=0.3516): R and B crossed, G (=0.25 < nd) has NOT -> curveShape2=(1,0,1,1).
//   Expected rgba8 = (255,105,255) — R&B saturate, G stays low. Pins the R-channel crossing (it
//   lights up between row 23 and row 27) AND the G non-crossing. The R/B-vs-G split is the tooth:
//   any nd sign/scale error collapses these distinct rows together.
// Test C (flat original, row 12 nd>1 and row 50 nd<0): full original (128,64,191). Pins the
//   isBetween/branch boundary and ShowOriginal=1 passthrough on BOTH sides of the band.
//
// injectBug: the shader is precompiled (can't perturb math at runtime), so injectBug perturbs the
// COOK — it sets Range=(0,2) (instead of (0,1)). colorOnLine is then DIVIDED by (Range.y-Range.x)=2,
// so colorOnLine becomes (0.25,0.125,0.375): every channel value HALVES, the crossing rows shift, and
// the band-row bytes change -> Test A & B FAIL. (Range is a real cook lever feeding the .hlsl
// `colorOnLine = (colorOnLine - Range.x)/(Range.y-Range.x)`, so this exercises the Range math, not a
// synthetic break.) The genuine "break the shader math, rebuild, confirm FAIL, restore" tooth-bite
// proof is ALSO recorded in the dossier.
namespace {
bool rgbNear(const uint8_t* px, int r, int g, int b, int tol) {
  return std::abs((int)px[0] - r) <= tol && std::abs((int)px[1] - g) <= tol &&
         std::abs((int)px[2] - b) <= tol;
}
}  // namespace

int runImageLevelsSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-imagelevels] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // Solid input (0.5,0.25,0.75) -> rgba8 (128,64,191).
  {
    std::vector<uint8_t> in((size_t)W * H * 4, 0);
    for (size_t i = 0; i < (size_t)W * H; ++i) {
      in[i * 4 + 0] = 128; in[i * 4 + 1] = 64; in[i * 4 + 2] = 191; in[i * 4 + 3] = 255;
    }
    src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);
  }
  auto at = [&](const std::vector<uint8_t>& buf, uint32_t x, uint32_t y) {
    return &buf[((size_t)y * W + x) * 4];
  };

  std::map<std::string, float> params;
  params["Center.x"] = 0.25f; params["Center.y"] = 0.0f;
  params["Rotation"] = 0.0f;
  params["Width"] = 0.2f;
  params["Range.x"] = 0.0f;
  params["Range.y"] = injectBug ? 2.0f : 1.0f;  // injectBug: Range=(0,2) halves colorOnLine -> band shifts.
  params["ShowOriginal"] = 1.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookImageLevels(c);
  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // ---- Test A: band row 23 (nd=0.6641) — only the B channel crossed -> rgba8 (148,135,255) ----
  // Whole row identical at Rotation=0; check several columns. Pins the B-channel curve crossing.
  bool aPass = true;
  for (uint32_t x : {0u, 17u, 32u, 48u, 63u}) {
    if (!rgbNear(at(out, x, 23), 148, 135, 255, 2)) aPass = false;
  }
  const uint8_t* aSample = at(out, 32, 23);
  printf("[selftest-imagelevels] TestA(band B-cross row23) px(32,23)=(%d,%d,%d) expect=(148,135,255) -> %s\n",
         aSample[0], aSample[1], aSample[2], aPass ? "PASS" : "FAIL");

  // ---- Test B: band row 27 (nd=0.3516) — R&B crossed, G not -> rgba8 (255,105,255) ----
  bool bPass = true;
  for (uint32_t x : {0u, 17u, 32u, 48u, 63u}) {
    if (!rgbNear(at(out, x, 27), 255, 105, 255, 2)) bPass = false;
  }
  const uint8_t* bSample = at(out, 32, 27);
  printf("[selftest-imagelevels] TestB(band R-cross row27) px(32,27)=(%d,%d,%d) expect=(255,105,255) -> %s\n",
         bSample[0], bSample[1], bSample[2], bPass ? "PASS" : "FAIL");

  // ---- Test C: flat original on BOTH sides of the band — row 12 (nd>1) and row 50 (nd<0) ----
  bool cPass = rgbNear(at(out, 32, 12), 128, 64, 191, 2) && rgbNear(at(out, 32, 50), 128, 64, 191, 2);
  const uint8_t* c12 = at(out, 32, 12);
  const uint8_t* c50 = at(out, 32, 50);
  printf("[selftest-imagelevels] TestC(flat original) row12=(%d,%d,%d) row50=(%d,%d,%d) expect=(128,64,191) -> %s\n",
         c12[0], c12[1], c12[2], c50[0], c50[1], c50[2], cPass ? "PASS" : "FAIL");

  // injectBug Range=(0,2) halves colorOnLine -> the crossing rows shift -> Test A & B FAIL.
  bool pass = aPass && bPass && cPass;
  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
