// ZollnerPattern image generator op (Phase C leaf).
// TiXL authority: Operators/Lib/image/generate/pattern/ZollnerPattern.cs (inputs/GUIDs) +
// ZollnerPattern.t3 (defaults: Stretch=(0.5,1.0), Offset=(0,0), Scale=1.0, Rotate=45.0,
// Feather=0.02, BarWidth=0.2, HookRotation=60.0, HookLength=0.7, HookWidth=0.33,
// RowSwift=0.0, RAffects_BarWidth=0.0, GAffects_HookLength=0.0, BAffects_HookRotation=0.0,
// AmplifyIllusion=0.0, Background=(1,1,1,1), Fill=(0,0,0,1)) +
// Assets/shaders/img/fx/ZollnerGrid.hlsl (single-pass Zöllner optical-illusion kernel;
// uses _ImageFxShaderSetupStatic with Wrap=Clamp).
//
// PORTABILITY HARDGATE (STEP-0 backward-trace, Cut-55/58/61 discipline):
//   - ZollnerPattern.t3 uses `_ImageFxShaderSetupStatic` (NOT _multiImageFxSetup).
//     No FloatsToBuffer routing trap — direct 1:1 connection order.
//   - Image input (b68794e6) is OPTIONAL (TiXL default null = no wired image).
//     The shader uses imgColorForCel.r/g/b only to modulate barWidth/hookLength/hookRotation
//     via the affect params. With Image=null, we bind a 1×1 transparent-black dummy (same
//     convention as Blob/SinForm, Cut 61) → all affect contributions = 0 → clean generator.
//   - No gradient / asset-texture / mip / multi-image / feedback seam dependency.
//   - _ImageFxShaderSetupStatic has Wrap=Clamp in ZollnerPattern.t3 (explicit fork named below).
//
// HLSL→MSL port forks (named):
//   fork[mod-macro]: HLSL `#define mod(x,y)((x)-(y*floor(x/y)))` → sw_mod() in .metal.
//   fork[pi-literal]: TiXL uses 3.141578f (approximate pi). Kept verbatim.
//   fork[cellAspect-dead]: rotateDeg()/cellAspect static never called in psMain — omitted.
//   fork[sampler-clamp]: TiXL ZollnerPattern.t3 sets Wrap=Clamp explicitly (unlike most ops
//     that use Repeat/Wrap). Our sampler uses MTL::SamplerAddressModeClampToEdge.
//   fork[dummy-1x1]: Image=null → bind 1×1 black dummy (IsTextureValid not used in this
//     shader; imgColorForCel is always sampled but contributes 0 when dummy).
//   fork[GAffects-uses-r]: TiXL ZollnerGrid.hlsl line 87 reads imgColorForCel.r for
//     GAffects_HookLength (not .g as the param name suggests). Kept verbatim, noted in .metal.
//
// HEADLESS GOLDEN (against TiXL formula — independent double-precision Python reference):
//   Test config: 256×256 square (aspect=1), no Image (dummy), Rotate=0, Stretch=(1,1),
//   Scale=1.0, Offset=(0,0), Feather=0.02, BarWidth=0.2, HookRotation=60, HookLength=0.7,
//   HookWidth=0.33, all affects=0, AmplifyIllusion=0.
//   Fill=(1,0,0,1) RED, Background=(1,1,1,1) WHITE.
//
//   ★ Cut62-63 probe discipline: probe at SATURATED PLATEAUS (s=1 inside the bar/hook, s=0 in
//     background) — NOT at smoothstep half-decay edges or dead background corners. The earlier
//     version probed pixel(4,4), which the refuter flagged as landing in PURE BACKGROUND with
//     all SDF thresholds missed (abs(pInCell.y-0.5)=0.4297, outside hookLength/barWidth) — so it
//     never witnessed the pattern geometry actually rendering. These pins fix that.
//
//   A full Python reference (zollner_s, replicating ZollnerGrid.hlsl psMain incl. sw_mod floor
//   semantics + the 3.141578 pi literal + hookRotation shear) scanned all 256×256 pixels for
//   robust plateaus:
//
//   PIN_FILL pixel(141,133): s=1.0 — DEEP INSIDE a hook/bar (entire 5×5 ±2 neighborhood s≥0.99).
//     This is the load-bearing probe: it positively proves the Zöllner geometry renders.
//     result = mix(Background=WHITE, Fill=RED, 1.0) = RED → RGBA8 (255, 0, 0, 255).
//     Assertion: R>200 && G<60 && B<60 (saturated Fill).
//
//   PIN_BG pixel(128,128): s=0.0 — center, deep background (5×5 neighborhood all s=0).
//     result = mix(WHITE, RED, 0.0) = WHITE → RGBA8 (255,255,255,255).
//     Assertion: R>200 && G>200 && B>200 (saturated Background) — anchors the background path.
//
//   injectBug: Scale=4.0 (was 1.0). Scale enters `divisions = float2(aspect,1)*4/(Scale*Stretch)`,
//     shifting which cell each pixel falls in. At PIN_FILL(141,133) the bar/hook moves away →
//     s drops from 1.0 to 0.0 (neighborhood max=0.0) → result WHITE → "G<60" assertion FAILS.
//     This proves Scale routing + the whole raster/cell/SDF chain is alive. (PIN_BG stays WHITE
//     under Scale=4 — it remains a valid background-parity anchor either way.)
//
// Self-contained leaf: cookZollnerPattern + ImageFilterOp self-registration +
// runZollnerPatternSelfTest. CMake GLOB auto-picks (CMakeLists.txt CONFIGURE_DEPENDS
// point_ops_*.cpp). No shared file edited (imageFilterSpecSink self-registration).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"               // TexCookCtx, cookParam
#include "runtime/tex_op_cache.h"              // cachedTexPSO (PSO reuse)
#include "runtime/zollnerpattern_params.h"     // ZollnerPatternParams, ZollnerPatternResolution

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration so the file-scope ImageFilterOp registrar can reference the selftest.
int runZollnerPatternSelfTest(bool injectBug);

namespace {

// Helper: create a 1×1 transparent-black dummy texture for the no-Image case.
// ZollnerPattern: Image input is optional (TiXL default null). Bind dummy so the Metal shader
// sees a valid texture2d handle at t0 (imgColorForCel samples to (0,0,0,0) → all affect = 0).
// fork[dummy-1x1]: same convention as Blob/SinForm (Cut 61 generator convention).
static MTL::Texture* makeDummyBlackTexture(MTL::Device* dev) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  const uint8_t px[4] = {0, 0, 0, 0};
  t->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, px, 4);
  return t;
}

// ZollnerPattern texture op: single render pass. Optional Image input (may be null → dummy).
void cookZollnerPattern(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "zollnerpattern_vs", "zollnerpattern_fs", fmt);
  if (!rps) return;

  // Sampler: Clamp address mode (TiXL ZollnerPattern.t3 explicitly sets Wrap=Clamp).
  // fork[sampler-clamp]: unlike most image-filter ops that use Repeat, this op clamps.
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // Build ZollnerPatternParams from cookParam (TiXL ZollnerPattern.t3 defaults).
  // cbuffer b0 field order matches ZollnerGrid.hlsl cbuffer ParamConstants exactly (STEP-0 traced).
  ZollnerPatternParams p{};
  // Fill (Vec4, TiXL default (0,0,0,1) black)
  p.FillR = cookParam(c, "Fill.r", 0.0f);
  p.FillG = cookParam(c, "Fill.g", 0.0f);
  p.FillB = cookParam(c, "Fill.b", 0.0f);
  p.FillA = cookParam(c, "Fill.a", 1.0f);
  // Background (Vec4, TiXL default (1,1,1,1) white)
  p.BgR = cookParam(c, "Background.r", 1.0f);
  p.BgG = cookParam(c, "Background.g", 1.0f);
  p.BgB = cookParam(c, "Background.b", 1.0f);
  p.BgA = cookParam(c, "Background.a", 1.0f);
  // Stretch (Vec2, TiXL default (0.5, 1.0))
  p.StretchX = cookParam(c, "Stretch.x", 0.5f);
  p.StretchY = cookParam(c, "Stretch.y", 1.0f);
  // Offset (Vec2, TiXL default (0,0))
  p.OffsetX = cookParam(c, "Offset.x", 0.0f);
  p.OffsetY = cookParam(c, "Offset.y", 0.0f);
  // Scale (float, TiXL default 1.0)
  p.ScaleFactor = cookParam(c, "Scale", 1.0f);
  // Rotate (float, TiXL default 45.0 degrees)
  p.Rotate = cookParam(c, "Rotate", 45.0f);
  // Feather (float, TiXL default 0.02)
  p.Feather = cookParam(c, "Feather", 0.02f);
  // HookRotation (float, TiXL default 60.0 degrees)
  p.HookRotation = cookParam(c, "HookRotation", 60.0f);
  // HookLength (float, TiXL default 0.7)
  p.HookLength = cookParam(c, "HookLength", 0.7f);
  // HookWidth (float, TiXL default 0.33)
  p.HookWidth = cookParam(c, "HookWidth", 0.33f);
  // BarWidth (float, TiXL default 0.2)
  p.BarWidth = cookParam(c, "BarWidth", 0.2f);
  // RowSwift (float, TiXL default 0.0)
  p.RowSwift = cookParam(c, "RowSwift", 0.0f);
  // Affect params (float, TiXL default 0.0 — no image modulation)
  p.RAffects_BarWidth     = cookParam(c, "RAffects_BarWidth",     0.0f);
  p.GAffects_HookLength   = cookParam(c, "GAffects_HookLength",   0.0f);
  p.BAffects_HookRotation = cookParam(c, "BAffects_HookRotation", 0.0f);
  // AmplifyIllusion (float, TiXL default 0.0)
  p.AmplifyIllusion = cookParam(c, "AmplifyIllusion", 0.0f);

  // Resolution cbuffer (b1).
  ZollnerPatternResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // Bind input texture or 1×1 black dummy (generator: Image=null by default).
  // fork[dummy-1x1]: dummy gives zollnerpattern_fs a valid texture2d binding (Metal requires
  // all declared texture slots to be bound). imgColorForCel → (0,0,0,0) → affects = 0.
  MTL::Texture* dummyTex = nullptr;
  const MTL::Texture* inputTex = c.inputTexture;
  if (!inputTex) {
    dummyTex = makeDummyBlackTexture(c.dev);
    inputTex = dummyTex;
  }

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(inputTex), ZOLLNER_Texture);
  enc->setFragmentSamplerState(samp, ZOLLNER_Sampler);
  enc->setFragmentBytes(&p,   sizeof(ZollnerPatternParams),    ZOLLNER_Params);
  enc->setFragmentBytes(&res, sizeof(ZollnerPatternResolution), ZOLLNER_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
  if (dummyTex) dummyTex->release();
}

}  // namespace

// Self-registration. File-scope static ImageFilterOp feeds imageFilterSpecSink() + texReg()
// + imageFilterSelfTests() during pre-main dynamic init. No shared file edited.
static const ImageFilterOp _reg_zollnerpattern{
    // ZollnerPattern (TiXL Lib.image.generate.pattern.ZollnerPattern):
    // Zöllner optical-illusion pattern — horizontal bars with angled hooks.
    // Optional Image input modulates bar/hook params via R/G/B channels.
    // Params mirror ZollnerPattern.cs/.t3 defaults.
    // FORKS (named): mod-macro, pi-literal, cellAspect-dead, sampler-clamp, dummy-1x1,
    //   GAffects-uses-r.
    {"ZollnerPattern", "ZollnerPattern",
     {// Optional Image input (TiXL default null; wired → modulates bar/hook params).
      {"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Fill (Vec4, TiXL default (0,0,0,1) black)
      {"Fill.r", "Fill",   "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Fill.g", "Fill.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Fill.b", "Fill.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Fill.a", "Fill.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Background (Vec4, TiXL default (1,1,1,1) white)
      {"Background.r", "Background",   "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Background.g", "Background.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.b", "Background.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.a", "Background.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Stretch (Vec2, TiXL default (0.5, 1.0))
      {"Stretch.x", "Stretch",   "Float", true, 0.5f, 0.01f, 4.0f, Widget::Vec, {}, true, 2},
      {"Stretch.y", "Stretch.y", "Float", true, 1.0f, 0.01f, 4.0f, Widget::Vec, {}, true, 1},
      // Offset (Vec2, TiXL default (0,0))
      {"Offset.x", "Offset",   "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Offset.y", "Offset.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Scale (float, TiXL default 1.0)
      {"Scale", "Scale", "Float", true, 1.0f, 0.01f, 8.0f, Widget::Slider},
      // Rotate (float, TiXL default 45.0 degrees)
      {"Rotate", "Rotate", "Float", true, 45.0f, -180.0f, 180.0f, Widget::Slider},
      // Feather (float, TiXL default 0.02)
      {"Feather", "Feather", "Float", true, 0.02f, 0.0f, 1.0f, Widget::Slider},
      // BarWidth (float, TiXL default 0.2)
      {"BarWidth", "BarWidth", "Float", true, 0.2f, 0.0f, 1.0f, Widget::Slider},
      // HookRotation (float, TiXL default 60.0 degrees)
      {"HookRotation", "HookRotation", "Float", true, 60.0f, -180.0f, 180.0f, Widget::Slider},
      // HookLength (float, TiXL default 0.7)
      {"HookLength", "HookLength", "Float", true, 0.7f, 0.0f, 1.0f, Widget::Slider},
      // HookWidth (float, TiXL default 0.33)
      {"HookWidth", "HookWidth", "Float", true, 0.33f, 0.0f, 1.0f, Widget::Slider},
      // RowSwift (float, TiXL default 0.0)
      {"RowSwift", "RowSwift", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Slider},
      // Color-channel affect params (float, TiXL default 0.0)
      {"RAffects_BarWidth",     "RAffects_BarWidth",     "Float", true, 0.0f, -1.0f, 1.0f, Widget::Slider},
      {"GAffects_HookLength",   "GAffects_HookLength",   "Float", true, 0.0f, -1.0f, 1.0f, Widget::Slider},
      {"BAffects_HookRotation", "BAffects_HookRotation", "Float", true, 0.0f, -180.0f, 180.0f, Widget::Slider},
      // AmplifyIllusion (float, TiXL default 0.0)
      {"AmplifyIllusion", "AmplifyIllusion", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Slider},
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "ZollnerPattern", cookZollnerPattern, "zollnerpattern", runZollnerPatternSelfTest};

// --- ZollnerPattern MATH golden ---------------------------------------------------------------
// Reference: ZollnerGrid.hlsl psMain (independent double-precision Python derivation).
//
// Two probes (see header for the full plateau-scan rationale):
//   PIN_FILL pixel(141,133): s=1.0 (deep inside hook/bar) → RED. The load-bearing probe.
//   PIN_BG   pixel(128,128): s=0.0 (deep background)       → WHITE. Background anchor.
//
// injectBug: Scale=4.0 → cell raster shifts → PIN_FILL bar/hook moves away → s=0 → WHITE →
//   "PIN_FILL G<60" assertion FAILS. Proves Scale routing + raster/cell/SDF chain is alive.
int runZollnerPatternSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-zollnerpattern] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // RGBA8Unorm output texture (256×256, shared memory for readback).
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(td);

  // Params: Rotate=0, Stretch=(1,1), Scale=1, Feather=0.02, BarWidth=0.2 (or 10 bug).
  // Fill=RED, Background=WHITE for clear visual separation.
  std::map<std::string, float> params;
  params["Fill.r"] = 1.0f; params["Fill.g"] = 0.0f;
  params["Fill.b"] = 0.0f; params["Fill.a"] = 1.0f;
  params["Background.r"] = 1.0f; params["Background.g"] = 1.0f;
  params["Background.b"] = 1.0f; params["Background.a"] = 1.0f;
  params["Stretch.x"] = 1.0f; params["Stretch.y"] = 1.0f;
  params["Offset.x"]  = 0.0f; params["Offset.y"]  = 0.0f;
  // injectBug: Scale=4 → cell raster shifts → PIN_FILL bar/hook moves away → s=0 → FAIL
  params["Scale"]    = injectBug ? 4.0f : 1.0f;
  params["Rotate"]   = 0.0f;       // simplified for golden (not TiXL default 45)
  params["Feather"]  = 0.02f;
  params["BarWidth"]     = 0.2f;
  params["HookRotation"] = 60.0f;
  params["HookLength"]   = 0.7f;
  params["HookWidth"]    = 0.33f;
  params["RowSwift"]     = 0.0f;
  params["RAffects_BarWidth"]     = 0.0f;
  params["GAffects_HookLength"]   = 0.0f;
  params["BAffects_HookRotation"] = 0.0f;
  params["AmplifyIllusion"]       = 0.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1;
  c.inputTexture = nullptr;  // GENERATOR MODE: no Image wired → dummy used in cook
  c.output = dst;
  c.params = &params;
  cookZollnerPattern(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // PIN_FILL: pixel(141,133) — DEEP INSIDE a hook/bar (s=1.0 plateau, 5×5 nbhd s≥0.99).
  //   Baseline: result = mix(WHITE, RED, 1.0) = RED → (255, 0, 0). Asserts the geometry renders.
  //   injectBug (Scale=4): bar/hook shifts away → s=0 → WHITE → "G<60" FAILS. ← path alive.
  uint32_t pfx = 141, pfy = 133;
  size_t ifill = ((size_t)pfy * W + pfx) * 4;
  int fR = (int)out[ifill+0];
  int fG = (int)out[ifill+1];
  int fB = (int)out[ifill+2];
  bool pinFill = (fR > 200 && fG < 60 && fB < 60);  // saturated Fill = RED

  // PIN_BG: pixel(128,128) — center, deep background (s=0.0 plateau).
  //   result = mix(WHITE, RED, 0.0) = WHITE → (255,255,255). Background-parity anchor.
  uint32_t pbx = 128, pby = 128;
  size_t ibg = ((size_t)pby * W + pbx) * 4;
  int bR = (int)out[ibg+0];
  int bG = (int)out[ibg+1];
  int bB = (int)out[ibg+2];
  bool pinBg = (bR > 200 && bG > 200 && bB > 200);  // saturated Background = WHITE

  printf("[selftest-zollnerpattern] "
         "PIN_FILL(%d,%d) RGB=(%d,%d,%d) want_RED(R>200,G<60,B<60) -> %s%s\n",
         pfx, pfy, fR, fG, fB, pinFill ? "PASS" : "FAIL",
         injectBug ? " [inject=Scale4]" : "");
  printf("[selftest-zollnerpattern] "
         "PIN_BG(%d,%d) RGB=(%d,%d,%d) want_WHITE(>200) -> %s\n",
         pbx, pby, bR, bG, bB, pinBg ? "PASS" : "FAIL");

  bool pass = pinFill && pinBg;
  printf("[selftest-zollnerpattern] -> %s\n", pass ? "PASS" : "FAIL");

  dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
