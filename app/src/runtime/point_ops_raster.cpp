// Raster image generator/filter op (Phase C leaf).
// TiXL authority: Operators/Lib/image/generate/pattern/Raster.cs (inputs) +
//   Raster.t3 (defaults: Scale=4, DotSize=0.05333333, LineWidth=0.053333342, LineRatio=0.75,
//   Feather=0.02, MixOriginal=1.0, Stretch=(32,32), Offset=(0,0), Rotate=0, Color=(1,1,1,1),
//   Background=(0,0,0,0), RedToDotSize=0, GreenToLineWidth=0, BlueToLineRatio=0,
//   Wrap=Clamp, GenerateMips=true, Resolution=(0,0)=WindowFollow) +
//   Assets/shaders/img/fx/Raster.hlsl (halftone raster grid: periodic dot+line pattern,
//   optional per-channel image modulation of dot/line/ratio params, alpha-composite over input).
//
// PORTABILITY HARDGATE (Cut-49/55/58 discipline):
//   - Uses _ImageFxShaderSetupStatic (not _multiImageFxSetup/compound). STEP-0 backward-trace
//     of Raster.t3 connections: all 14 scalar cbuffer params flow directly from
//     Vector4Components/Vector2Components decomposers → slot 4ef6f204. Zero math-node
//     intermediates (no Multiply/IntToFloat). Direct 1:1 port confirmed.
//   - Single optional Texture2D input (Image, default null). No multi-image seam.
//     When Image=null (generator mode): dummy 1×1 transparent-black texture used; the
//     R/G/B-Affects branch is dead (all three default to 0.0), and the final composite
//     gives orgColor.a=0 → pattern over transparent → same as TiXL null-image behaviour.
//   - No gradient/curve-LUT/asset-texture/feedback/sim-state dependency.
//   - Wrap=Clamp (TiXL .t3 explicit); sampler uses ClampToEdge.
//
// GENERATOR CONVENTION (Cut 61): null Image input → 1×1 transparent-black dummy texture so
//   raster_fs always has a valid texture2d handle. orgColor=(0,0,0,0) = TiXL null SRV.
//
// HLSL→MSL PORT (full detail in raster.metal):
//   [fork-mod-macro]      HLSL `#define mod(x,y) (x-y*floor(x/y))` → sw_mod2() in metal.
//   [fork-sampler-clamp]  TiXL .t3 Wrap=Clamp → ClampToEdge.
//   [fork-dummy-tex]      null Image → 1×1 transparent-black dummy; R/G/B affects=0 → dead.
//   [fork-output-clamp]   rgb clamped to [0,10000], a clamped to [0,1] (HLSL line 134 verbatim).
//   [fork-rotation-sign]  Rotation formula ported verbatim (imageRotationRad=(-Rotate-90)/180*pi,
//                         double negation for inverse pass).
//
// NodeSpec mirrors Raster.cs: Vec4 inputs decomposed to .r/.g/.b/.a; Vec2 to .x/.y.
// TiXL param name → cbuffer field → port name mapping:
//   Color → Fill (Fill in HLSL cbuffer), Stretch → Size (Size in HLSL), Scale → ScaleFactor.
//
// Self-contained leaf: cookRaster + _reg_raster + runRasterSelfTest.
// CMake CONFIGURE_DEPENDS glob auto-picks point_ops_raster.cpp; no CMake edit needed.
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
#include "runtime/raster_params.h"             // RasterParams/Resolution, RASTER_* bindings
#include "runtime/tex_op_cache.h"              // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration so the file-scope ImageFilterOp registrar can reference runRasterSelfTest.
int runRasterSelfTest(bool injectBug);

namespace {

// Helper: create a 1×1 transparent-black dummy texture for the no-input case.
// [fork-dummy-tex]: Raster is optionally a generator (TiXL Image=null default). We still run
// the shader; a 1×1 dummy gives raster_fs a valid texture2d handle → orgColor=(0,0,0,0).
static MTL::Texture* makeRasterDummyBlackTexture(MTL::Device* dev) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  const uint8_t px[4] = {0, 0, 0, 0};
  t->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, px, 4);
  return t;
}

// Raster texture op: single render pass. Reads c.inputTexture (optional), writes c.output.
// When no upstream wired: 1×1 dummy provides a valid texture handle; all R/G/B Affects=0 by
// default so the image-modulation branch stays dead.
void cookRaster(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "raster_vs", "raster_fs", fmt);
  if (!rps) return;

  // Sampler: linear + Clamp. TiXL Raster.t3 Wrap=Clamp (DX11 TextureAddressMode.Clamp).
  // [fork-sampler-clamp]
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // b0 params — mirror RasterParams field order (= Raster.hlsl cbuffer ParamConstants order).
  // TiXL Raster.t3 defaults used when no param wired.
  RasterParams p{};
  // Fill / Color (Vec4, TiXL Color default (1,1,1,1)) — "Fill" in HLSL cbuffer
  p.FillR = cookParam(c, "Color.r", 1.0f);
  p.FillG = cookParam(c, "Color.g", 1.0f);
  p.FillB = cookParam(c, "Color.b", 1.0f);
  p.FillA = cookParam(c, "Color.a", 1.0f);
  // Background (Vec4, TiXL default (0,0,0,0))
  p.BackgroundR = cookParam(c, "Background.r", 0.0f);
  p.BackgroundG = cookParam(c, "Background.g", 0.0f);
  p.BackgroundB = cookParam(c, "Background.b", 0.0f);
  p.BackgroundA = cookParam(c, "Background.a", 0.0f);
  // Size / Stretch (Vec2, TiXL Stretch default (32,32)) — "Size" in HLSL cbuffer
  p.SizeX = cookParam(c, "Stretch.x", 32.0f);
  p.SizeY = cookParam(c, "Stretch.y", 32.0f);
  // Offset (Vec2, TiXL default (0,0))
  p.OffsetX = cookParam(c, "Offset.x", 0.0f);
  p.OffsetY = cookParam(c, "Offset.y", 0.0f);
  // ScaleFactor / Scale (float, TiXL default 4.0) — "ScaleFactor" in HLSL cbuffer
  p.ScaleFactor = cookParam(c, "Scale", 4.0f);
  // Rotate (float, TiXL default 0.0)
  p.Rotate = cookParam(c, "Rotate", 0.0f);
  // DotSize (float, TiXL default 0.05333333)
  p.DotSize = cookParam(c, "DotSize", 0.05333333f);
  // LineWidth (float, TiXL default 0.053333342)
  p.LineWidth = cookParam(c, "LineWidth", 0.053333342f);
  // LineRatio (float, TiXL default 0.75)
  p.LineRatio = cookParam(c, "LineRatio", 0.75f);
  // RAffects_DotSize / RedToDotSize (float, TiXL default 0.0)
  p.RAffects_DotSize = cookParam(c, "RedToDotSize", 0.0f);
  // GAffects_LineWidth / GreenToLineWidth (float, TiXL default 0.0)
  p.GAffects_LineWidth = cookParam(c, "GreenToLineWidth", 0.0f);
  // BAffects_LineRatio / BlueToLineRatio (float, TiXL default 0.0)
  p.BAffects_LineRatio = cookParam(c, "BlueToLineRatio", 0.0f);
  // MixOriginal (float, TiXL default 1.0)
  p.MixOriginal = cookParam(c, "MixOriginal", 1.0f);
  // Feather (float, TiXL default 0.02)
  p.Feather = cookParam(c, "Feather", 0.02f);

  RasterResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // Bind input texture (or 1×1 black dummy when no upstream is wired).
  // [fork-dummy-tex]: TiXL Image=null default → orgColor=(0,0,0,0) → pattern over transparent.
  MTL::Texture* dummyTex = nullptr;
  const MTL::Texture* inputTex = c.inputTexture;
  if (!inputTex) {
    dummyTex = makeRasterDummyBlackTexture(c.dev);
    inputTex = dummyTex;
  }

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(inputTex), 0);
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p,   sizeof(RasterParams),     RASTER_Params);
  enc->setFragmentBytes(&res, sizeof(RasterResolution), RASTER_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();
  if (dummyTex) dummyTex->release();
}

}  // namespace

// Self-registration: file-scope static ImageFilterOp feeds imageFilterSpecSink() + texReg()
// + imageFilterSelfTests() during pre-main dynamic init. No shared file edited.
static const ImageFilterOp _reg_raster{
    // Raster (TiXL Lib.image.generate.pattern.Raster): periodic halftone raster grid of dots
    // and lines. Optional Texture2D in (Image) drives per-channel param modulation and final
    // alpha-composite. Generator mode (Image=null): pattern over transparent background.
    // Params mirror Raster.cs: Color(Vec4)/Background(Vec4)/Stretch(Vec2)/Scale/Rotate/
    // Offset(Vec2)/DotSize/LineWidth/LineRatio/Feather/MixOriginal/
    // RedToDotSize/GreenToLineWidth/BlueToLineRatio.
    // FORKS: [fork-mod-macro] [fork-sampler-clamp] [fork-dummy-tex] [fork-output-clamp]
    //        [fork-rotation-sign] (all named in raster.metal).
    {"Raster", "Raster",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Color / Fill (Vec4, TiXL default (1,1,1,1))
      {"Color.r", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Color.g", "Color.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Color.b", "Color.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Color.a", "Color.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Background (Vec4, TiXL default (0,0,0,0))
      {"Background.r", "Background", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Background.g", "Background.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.b", "Background.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.a", "Background.a", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // MixOriginal (float, TiXL default 1.0)
      {"MixOriginal", "MixOriginal", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Slider},
      // Scale (float, TiXL default 4.0)
      {"Scale", "Scale", "Float", true, 4.0f, 0.01f, 32.0f, Widget::Slider},
      // Stretch / Size (Vec2, TiXL default (32,32))
      {"Stretch.x", "Stretch", "Float", true, 32.0f, 1.0f, 256.0f, Widget::Vec, {}, true, 2},
      {"Stretch.y", "Stretch.y", "Float", true, 32.0f, 1.0f, 256.0f, Widget::Vec, {}, true, 1},
      // Offset (Vec2, TiXL default (0,0))
      {"Offset.x", "Offset", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Offset.y", "Offset.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Rotate (float, TiXL default 0.0)
      {"Rotate", "Rotate", "Float", true, 0.0f, -180.0f, 180.0f, Widget::Slider},
      // DotSize (float, TiXL default 0.05333333)
      {"DotSize", "DotSize", "Float", true, 0.05333333f, 0.0f, 1.0f, Widget::Slider},
      // LineWidth (float, TiXL default 0.053333342)
      {"LineWidth", "LineWidth", "Float", true, 0.053333342f, 0.0f, 1.0f, Widget::Slider},
      // LineRatio (float, TiXL default 0.75)
      {"LineRatio", "LineRatio", "Float", true, 0.75f, 0.0f, 1.0f, Widget::Slider},
      // Feather (float, TiXL default 0.02)
      {"Feather", "Feather", "Float", true, 0.02f, 0.0f, 0.5f, Widget::Slider},
      // RedToDotSize / RAffects_DotSize (float, TiXL default 0.0)
      {"RedToDotSize", "RedToDotSize", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Slider},
      // GreenToLineWidth / GAffects_LineWidth (float, TiXL default 0.0)
      {"GreenToLineWidth", "GreenToLineWidth", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Slider},
      // BlueToLineRatio / BAffects_LineRatio (float, TiXL default 0.0)
      {"BlueToLineRatio", "BlueToLineRatio", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Slider},
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}}},
    "Raster", cookRaster, "raster", runRasterSelfTest};

// --- Raster MATH golden -----------------------------------------------------------------------
// Ground truth from TiXL Raster.hlsl (hand-derived, independent of the port).
//
// Test setup: 256×256 square output, aspect=1.
//   Fill=(1,0,0,1) red, Background=(0,0,1,1) blue.
//   Scale=4, Stretch=(32,32), Rotate=0, Offset=(0,0), Feather=0.02.
//   DotSize=0.05333333, LineWidth=0.053333342, LineRatio=0.75.
//   All R/G/B Affects=0 → image-modulation branch dead.
//   MixOriginal=1.0, no Image → dummy orgColor=(0,0,0,0).
//
//   divisions = (256/32, 256/32) / 4 = (2, 2)
//   gridSize = (0.5, 0.5)
//   edgeSmooth = 0.02 / 4 = 0.005
//
// PROBE A — dot center at pixel(0,0), uv≈(0.002,0.002):
//   p ≈ (-0.498,-0.498). Rotate=0: imageRotationRad=(-0-90)/180*pi=-pi/2.
//   sina=sin(-(-pi/2)-pi/2)=sin(0)=0, cosa=cos(0)=1 → p unchanged.
//   p1=p. pInCell=mod((-0.498,-0.498),(0.5,0.5))=-0.498-0.5*floor(-0.996)=-0.498+0.5=0.002.
//   pInCell*=(2,2)=(0.004,0.004) ≈ dot corner.
//   pInCellCentered=abs((0.004,0.004)-0.5)-0.5=abs(-0.496)-0.5=-0.004.
//   distanceToCorner=length(-0.004,-0.004)≈0.006.
//   smoothstep(0.05333+0.005, 0.05333-0.005, 0.006) = smoothstep(0.0583,0.0483,0.006).
//   0.006 < 0.0483 → returns 1. col=1 → Fill=red.
//   orgColor.a=0 → a=0*1+1-0=1, rgb=(1-1)*0+1*(1,0,0)=(1,0,0).
//   → pixel(0,0) R=255, B<50 (red plateau). ← PROBE A ASSERTION.
//
// PROBE B — cell center at pixel(64,64), uv=(64.5/256)≈0.252:
//   p≈(-0.248,-0.248). p1=p. pInCell=mod(-0.248,0.5)=0.252. *2=0.504≈0.5 (cell center).
//   pInCellCentered=abs(0.504-0.5)-0.5=0.004-0.5=-0.496.
//   distanceToCorner=length(-0.496,-0.496)≈0.701.
//   smoothstep(0.0583,0.0483,0.701)→0 (far beyond upper bound). No dot.
//   distanceToEdge=abs(-0.496,-0.496)=(0.496,0.496). min=0.496.
//   smoothstep(0.0317,0.0217,0.496)→0. No line. col=0 → Background=blue.
//   orgColor.a=0 → a=1, rgb=(0,0,1).
//   → pixel(64,64) B=255, R<50 (blue plateau). ← PROBE B ASSERTION.
//
// injectBug: negate DotSize → dotSize=-DotSize. At pixel(0,0), distanceToCorner≈0.006:
//   smoothstep(-0.0533+0.005, -0.0533-0.005, 0.006) = smoothstep(-0.0483,-0.0583,0.006).
//   0.006 > -0.0483 → returns 0. No dot. col=0 → Background=blue.
//   PROBE A assertion (R>200 at pixel(0,0)) FAILS. RED. ✓
int runRasterSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;  // square: aspect=1, rotation is no-op centring

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-raster] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(td);

  // Params: Fill=red, Background=blue, Scale=4, Stretch=(32,32), all Affects=0.
  // Generator mode (no Image wired): dummy texture path.
  std::map<std::string, float> params;
  params["Color.r"] = 1.0f; params["Color.g"] = 0.0f;
  params["Color.b"] = 0.0f; params["Color.a"] = 1.0f;
  params["Background.r"] = 0.0f; params["Background.g"] = 0.0f;
  params["Background.b"] = 1.0f; params["Background.a"] = 1.0f;
  params["Scale"]   = 4.0f;
  params["Stretch.x"] = 32.0f; params["Stretch.y"] = 32.0f;
  params["Offset.x"]  = 0.0f;  params["Offset.y"]  = 0.0f;
  params["Rotate"]    = 0.0f;
  // injectBug: negate DotSize → dot plateau probe A flips red→blue
  params["DotSize"]   = injectBug ? -0.05333333f : 0.05333333f;
  params["LineWidth"] = 0.053333342f;
  params["LineRatio"] = 0.75f;
  params["Feather"]   = 0.02f;
  params["RedToDotSize"]    = 0.0f;
  params["GreenToLineWidth"] = 0.0f;
  params["BlueToLineRatio"]  = 0.0f;
  params["MixOriginal"] = 1.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1;
  c.inputTexture = nullptr;  // generator mode: no input, dummy path
  c.output = dst;
  c.params = &params;
  cookRaster(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  auto readPixel = [&](uint32_t x, uint32_t y, int ch) -> int {
    return (int)out[((size_t)y * W + x) * 4 + ch];
  };

  // PROBE A: pixel(0,0) — dot corner, distanceToCorner≈0 → deep inside dot → Fill=red.
  //   Hand-calc: smoothstep(0.0583,0.0483,~0.006)=1, col=1 → (1,0,0).
  //   injectBug: DotSize negated → smoothstep returns 0 → col=0 → Background=blue. FAILS.
  int pAR = readPixel(0, 0, 0);   // R channel
  int pAB = readPixel(0, 0, 2);   // B channel

  // PROBE B: pixel(64,64) — cell center, distanceToCorner≈0.701 → far outside dot/line → bg=blue.
  //   Hand-calc: smoothstep(0.0583,0.0483,0.701)=0, smoothstep lines=0, col=0 → (0,0,1).
  //   injectBug has no effect here (cell center never reaches dot threshold); still blue.
  int pBR = readPixel(64, 64, 0);  // R channel
  int pBB = readPixel(64, 64, 2);  // B channel

  // Assertions
  bool probeA_red  = (pAR > 200 && pAB < 50);   // dot center → Fill=red; FAILS under injectBug
  bool probeB_blue = (pBB > 200 && pBR < 50);   // cell center → Background=blue; stable

  bool pass = probeA_red && probeB_blue;
  printf("[selftest-raster] "
         "probeA(dot-center,0,0) R=%d B=%d(want red) "
         "probeB(cell-center,64,64) R=%d B=%d(want blue) "
         "-> %s\n",
         pAR, pAB, pBR, pBB,
         pass ? "PASS" : "FAIL");

  dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
