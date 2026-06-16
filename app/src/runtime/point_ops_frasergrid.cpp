// FraserGrid image generator op (Phase C leaf, C-FraserGrid).
// TiXL authority: Operators/Lib/image/generate/pattern/FraserGrid.cs (ports + defaults) +
// FraserGrid.t3 (defaults: Fill=(0,0,0,1) black, FillB=(1,1,1,1) white,
//   Background=(0.67475104,0.67498636,0.67569184,1) grey, Feather=0.015, Size=(32,16), Offset=(0,0),
//   Scale=4.0, Rotate=0.0, RotateShapes=45.0, ShapeSize=0.22, BarWidth=0.035, BorderWidth=0.06,
//   RowSwift=0.0, RAffects_BarWidth=0.0, GAffects_ShapeSize=0.0, BAffects_LineRatio=0.0,
//   Image=null) +
// Assets/shaders/img/fx/FraserGrid.hlsl (psMain: per-cell Fraser-grid pattern with rotated diamond
// shapes and center/gap bars).
//
// PORTABILITY HARDGATE (Cut-49/55/58 discipline):
//   - Single optional texture input (Image, t0). When unwired, host binds 1x1 transparent-black
//     dummy so the shader always has a valid texture2d handle (generator convention, same as SinForm).
//   - FloatsToBuffer routing: STEP-0 backward-trace of FraserGrid.t3 confirmed ZERO math-node
//     intermediates. Params flow: FillA(Vec4Components) -> FillB(Vec4Components) ->
//     Background(Vec4Components) -> Size(Vec2) -> Offset(Vec2) -> Scale(direct) -> Rotate ->
//     Feather -> RotateShapes -> ShapeSize -> BarWidth -> BorderWidth -> RowSwift ->
//     RAffects_BarWidth -> GAffects_ShapeSize -> BAffects_LineRatio. Total 24 floats = 96 bytes
//     (wait: 4+4+4+2+2+1+1+1+1+1+1+1+1+1+1+1 = 26 floats... recount):
//     FillA=4 + FillB=4 + Background=4 + Size=2 + Offset=2 + Scale=1 + Rotate=1 + Feather=1 +
//     RotateShapes=1 + ShapeSize=1 + BarWidth=1 + BorderWidth=1 + RowSwift=1 +
//     RAffects_BarWidth=1 + GAffects_ShapeSize=1 + BAffects_LineRatio=1 = 26 floats = 104 bytes.
//     cbuffer b0 = FraserGridParams (112 bytes padded); b1 = FraserGridResolution (16 bytes).
//   - No gradient / mip / asset-texture seam dependency.
//   - _ImageFxShaderSetupStatic class: Source="Lib:shaders/img/fx/FraserGrid.hlsl", Wrap=Clamp,
//     GenerateMips=true (from FraserGrid.t3). Our cook uses the standard RGBA8Unorm output.
//   - No temporal-random / feedback / cross-frame state.
//
// HLSL->MSL port: frasergrid.metal — faithful translation. Key fork notes:
//   [mod-macro] HLSL #define mod(x,y)=floor-based; MSL uses fg_mod() helper (NOT fmod/%).
//   [pi-approx] HLSL uses 3.141578 (TiXL verbatim, slightly off from exact pi) — preserved.
//   [asin-clamp] HLSL asin(barWidth*4+edgeSmooth/4) — clamped to [-1,1] in MSL to avoid NaN.
//   [s2-unused] HLSL computes s2 (smoothstep) but final expr only uses s2border — s2 silenced.
//   [sampler] TiXL .t3 Wrap=Clamp -> ClampToEdge.
//
// GOLDEN ASSERTIONS (default params, 128x128, no input texture — generator mode):
//
//   (A) Pixel (64, 64) — UV center — is in the BACKGROUND plateau:
//     With Rotate=0, p=(0,0), pInCell=(0,0) (first cell corner), cellAspect=(2,1).
//     s1a_raw = box(rotateDeg((0,0)-(0,0.5),45)) = 0.3536; >> shapeSize=0.22 -> s1a=1, s1aBorder=1.
//     s1b, s2: far away -> s1bBorder=1, s2border=1. background = 1 - 1*1*1 = 0.
//     centerBar: pcb at pInCell=(0,0), rotate((-0.5,-0.5),ta); abs(pcb.x)>>BarWidth -> centerBar=1.
//     fillA = 1*lerp(1,1,gapBarA)*lerp(1,1,gapBarB)*1 = 1.
//     cBorderOrBackground = lerp(Background, FillB, 0) = Background.
//     cFill = lerp(FillA, Background, 1) = Background = (0.675,0.675,0.676,1) -> RGBA8 ~ (172,172,172,255).
//     ASSERTION A: pixel(64,64) R in [155,190], G in [155,190], B in [155,190], A in [240,255].
//     (tolerance ±17 around 172 to allow LSB drift; plateau pixel so no smoothstep edge risk.)
//
//   (B) Pixel (64, 90) — ON the center bar — is FillA = black:
//     pInCell=(0, 0.4272): pcb.x ≈ 0 -> centerBar=0 -> fillA=0.
//     s1a_raw=0.0515 << shapeSize -> s1aBorder=0 -> background=1.
//     cFill = lerp(FillA=(0,0,0,1), lerp(Background,FillB,1)=(1,1,1,1), 0) = FillA = (0,0,0,1).
//     RGBA8: R=0, G=0, B=0, A=255.
//     ASSERTION B: pixel(64,90) R<20, G<20, B<20, A>240.
//
//   injectBug: set FillA = (1,1,1,1) white -> on-bar pixel (64,90) becomes white (R>200) -> B FAILS.
//
// Self-contained leaf: cookFraserGrid + ImageFilterOp self-registration + runFraserGridSelfTest.
// CMake glob auto-picks this file (CMakeLists.txt CONFIGURE_DEPENDS point_ops_*.cpp).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/frasergrid_params.h"          // FraserGridParams/Resolution, FRASERGRID_* bindings
#include "runtime/image_filter_op_registry.h"   // ImageFilterOp self-registration
#include "runtime/point_graph.h"                // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"               // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration so the file-scope ImageFilterOp registrar can reference runFraserGridSelfTest.
int runFraserGridSelfTest(bool injectBug);

namespace {

// Helper: create a 1x1 transparent-black dummy texture for the no-input case.
// FraserGrid is a generator (TiXL Image=null default). Dummy provides a valid texture2d handle
// so the shader runs; imgColorForCel = (0,0,0,0) -> R/G/BAffects have no effect.
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

// FraserGrid texture op: single render pass. Optionally reads c.inputTexture (if wired), always
// writes c.output. When no upstream: draws the Fraser-grid pattern over a transparent-black dummy.
void cookFraserGrid(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "frasergrid_vs", "frasergrid_fs", fmt);
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // TiXL .t3 Wrap=Clamp
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL FraserGrid.t3 defaults:
  //   Fill=(0,0,0,1), FillB=(1,1,1,1), Background=(0.67475104,0.67498636,0.67569184,1)
  //   Size=(32,16), Offset=(0,0), Scale=4.0, Rotate=0.0, Feather=0.015, RotateShapes=45.0
  //   ShapeSize=0.22, BarWidth=0.035, BorderWidth=0.06, RowSwift=0.0
  //   RAffects_BarWidth=0.0, GAffects_ShapeSize=0.0, BAffects_LineRatio=0.0
  FraserGridParams p{};
  // FillA (Vec4, default (0,0,0,1) — black)
  p.FillAR = cookParam(c, "Fill.r", 0.0f);
  p.FillAG = cookParam(c, "Fill.g", 0.0f);
  p.FillAB = cookParam(c, "Fill.b", 0.0f);
  p.FillAA = cookParam(c, "Fill.a", 1.0f);
  // FillB (Vec4, default (1,1,1,1) — white)
  p.FillBR = cookParam(c, "FillB.r", 1.0f);
  p.FillBG = cookParam(c, "FillB.g", 1.0f);
  p.FillBB = cookParam(c, "FillB.b", 1.0f);
  p.FillBA = cookParam(c, "FillB.a", 1.0f);
  // Background (Vec4, default ~grey)
  p.BgR = cookParam(c, "Background.r", 0.67475104f);
  p.BgG = cookParam(c, "Background.g", 0.67498636f);
  p.BgB = cookParam(c, "Background.b", 0.67569184f);
  p.BgA = cookParam(c, "Background.a", 1.0f);
  // Size (Vec2, default (32,16))
  p.SizeX = cookParam(c, "Size.x", 32.0f);
  p.SizeY = cookParam(c, "Size.y", 16.0f);
  // Offset (Vec2, default (0,0))
  p.OffsetX = cookParam(c, "Offset.x", 0.0f);
  p.OffsetY = cookParam(c, "Offset.y", 0.0f);
  // Scalars
  p.ScaleFactor    = cookParam(c, "Scale",            4.0f);
  p.Rotate         = cookParam(c, "Rotate",           0.0f);
  p.Feather        = cookParam(c, "Feather",          0.015f);
  p.RotateShapes   = cookParam(c, "RotateShapes",     45.0f);
  p.ShapeSize      = cookParam(c, "ShapeSize",        0.22f);
  p.BarWidth       = cookParam(c, "BarWidth",         0.035f);
  p.BorderWidth    = cookParam(c, "BorderWidth",      0.06f);
  p.RowSwift       = cookParam(c, "RowSwift",         0.0f);
  p.RAffects_BarWidth  = cookParam(c, "RAffects_BarWidth",  0.0f);
  p.GAffects_ShapeSize = cookParam(c, "GAffects_ShapeSize", 0.0f);
  p.BAffects_LineRatio = cookParam(c, "BAffects_LineRatio", 0.0f);

  FraserGridResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // Bind input texture (or 1x1 black dummy when no upstream is wired).
  // FORK (named): TiXL Image=null default -> imgColorForCel=(0,0,0,0) -> RAffects_*/etc. inactive.
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
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(inputTex), 0);
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p,   sizeof(FraserGridParams),     FRASERGRID_Params);
  enc->setFragmentBytes(&res, sizeof(FraserGridResolution), FRASERGRID_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();
  if (dummyTex) dummyTex->release();
}

}  // namespace

// Self-registration. NodeSpec mirrors FraserGrid.cs inputs.
// Vec4 inputs (Fill, FillB, Background) decomposed to .r/.g/.b/.a scalar ports.
// Vec2 inputs (Size, Offset) decomposed to .x/.y scalar ports.
// Resolution: standard Output Size enum + CustomW/H.
// FORKS (named):
//   1. TiXL Image input optional (default null): sw accepts null input and renders over black.
//   2. Fixed clamp sampler (TiXL .t3 Wrap=Clamp verbatim).
//   3. TiXL TextureFormat -> not modelled (sw uses RGBA8Unorm via Resolution enum).
static const ImageFilterOp _reg_frasergrid{
    // FraserGrid (TiXL Lib.image.generate.pattern.FraserGrid): Fraser-grid pattern generator.
    // Optional Texture2D in (Image, default null) -> Texture2D out.
    // Kernel: FraserGrid.hlsl — per-cell diamond shapes with center/gap bars, color modulation.
    {"FraserGrid", "FraserGrid",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Fill (Vec4, TiXL t3 default (0,0,0,1) — black)
      {"Fill.r", "Fill", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Fill.g", "Fill.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Fill.b", "Fill.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Fill.a", "Fill.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // FillB (Vec4, TiXL t3 default (1,1,1,1) — white)
      {"FillB.r", "FillB", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"FillB.g", "FillB.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"FillB.b", "FillB.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"FillB.a", "FillB.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Background (Vec4, TiXL t3 default ~(0.675,0.675,0.676,1) grey)
      {"Background.r", "Background", "Float", true, 0.67475104f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Background.g", "Background.g", "Float", true, 0.67498636f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.b", "Background.b", "Float", true, 0.67569184f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.a", "Background.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Feather (float, TiXL t3 default 0.015)
      {"Feather", "Feather", "Float", true, 0.015f, 0.0f, 0.5f, Widget::Slider},
      // Size (Vec2, TiXL t3 default (32,16); cell dimensions in pixels)
      {"Size.x", "Size", "Float", true, 32.0f, 1.0f, 512.0f, Widget::Vec, {}, true, 2},
      {"Size.y", "Size.y", "Float", true, 16.0f, 1.0f, 512.0f, Widget::Vec, {}, true, 1},
      // Offset (Vec2, TiXL t3 default (0,0))
      {"Offset.x", "Offset", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Offset.y", "Offset.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Scale (float, TiXL t3 default 4.0)
      {"Scale", "Scale", "Float", true, 4.0f, 0.01f, 20.0f, Widget::Slider},
      // Rotate (float, TiXL t3 default 0.0, degrees)
      {"Rotate", "Rotate", "Float", true, 0.0f, -180.0f, 180.0f},
      // RotateShapes (float, TiXL t3 default 45.0, degrees)
      {"RotateShapes", "RotateShapes", "Float", true, 45.0f, -180.0f, 180.0f},
      // ShapeSize (float, TiXL t3 default 0.22)
      {"ShapeSize", "ShapeSize", "Float", true, 0.22f, 0.0f, 1.0f, Widget::Slider},
      // BarWidth (float, TiXL t3 default 0.035)
      {"BarWidth", "BarWidth", "Float", true, 0.035f, 0.0f, 0.5f, Widget::Slider},
      // BorderWidth (float, TiXL t3 default 0.06)
      {"BorderWidth", "BorderWidth", "Float", true, 0.06f, 0.0f, 0.5f, Widget::Slider},
      // RowSwift (float, TiXL t3 default 0.0)
      {"RowSwift", "RowSwift", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider},
      // Image-modulation scalars (TiXL t3 default 0.0 for all three)
      {"RAffects_BarWidth",  "RAffects_BarWidth",  "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider},
      {"GAffects_ShapeSize", "GAffects_ShapeSize", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider},
      {"BAffects_LineRatio", "BAffects_LineRatio", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider},
      // Resolution
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "FraserGrid", cookFraserGrid, "frasergrid", runFraserGridSelfTest,
    /*mippedOutput=*/true};

// --- FraserGrid MATH golden -------------------------------------------------------------------
// Configuration: default params (Fill=black, FillB=white, Background~grey, Size=(32,16),
// Scale=4.0, Rotate=0, Feather=0.015, RotateShapes=45, ShapeSize=0.22, BarWidth=0.035,
// BorderWidth=0.06, RowSwift=0, all RAffects_*=0). No input texture (generator mode).
// Output: 128x128 RGBA8Unorm.
//
// Derived quantities (Scale=4, Size=(32,16), 128x128):
//   cellAspect = (32/16, 1) = (2, 1)
//   edgeSmooth = 0.015 / (4 * (32+16)/2) * 100 = 0.015 / 96 * 100 = 0.015625
//   divisions = (128/32, 128/16) / 4 = (1, 2)
//   Each cell spans UV width=1, UV height=0.5.
//
// GOLDEN A: pixel (64, 64) — UV center, background plateau.
//   p=(0,0), Rotate=0 -> rotation identity. p1=(0,0). ppp=(0,0). pInCell=(0,0). cellId=(0,0).
//   Row even (1010%2=0) -> no flip. pInCell.x=0. pInCell=(0,0).
//   s1a_raw = box(rotateDeg((0,0)-(0,0.5), 45, (2,1))):
//     q=(0,-0.5)*cellAspect=(0,-0.5); a=45*pi/180=pi/4;
//     sina=sin(-pi/4-pi/2)=-sqrt(2)/2; cosa=cos(-3pi/4)=-sqrt(2)/2;
//     x'=-0.7071*0-(-0.7071)*(-0.5)=-0.3536; y'=-0.7071*(-0.5)+(-0.7071)*0=0.3536
//     box=max(0.3536,0.3536)=0.3536 >> shapeSize=0.22 -> s1a=1, s1aBorder=1.
//   s1b_raw >> shapeSize -> s1bBorder=1. s2border=1. background=1-1=0.
//   ta=asin(0.035*4+0.015625/4)=asin(0.1439)≈0.1443.
//   pcb=rotate((-0.5,-0.5),ta); pcb.x=cosa*(-0.5)-sina*(-0.5); |pcb.x|>>BarWidth -> centerBar=1.
//   fillA=1*1*1*1=1. cBorderOrBackground=lerp(Background,FillB,0)=Background~(0.675,0.675,0.676,1).
//   cFill=lerp(FillA=(0,0,0,1), Background, 1)=Background. RGBA8: R=172, G=172, B=172, A=255.
//   ASSERTION A: R in [155,190], G in [155,190], B in [155,190], A in [240,255].
//
// GOLDEN B: pixel (64, 90) — ON center bar, FillA = black.
//   p=(0, 0.2136), divisions=(1,2). p1=(0,0.2136). ppp=mod((0,0.2136),(1,0.5))=(0,0.2136).
//   pInCell=(0, 0.4272). cellId=(0,0). No flip (row 0 even). pInCell.x=0.
//   pcb.x = cosa*(0-0.5)-sina*(0.4272-0.5) = (-0.1440)*(-0.5)-(-0.9896)*(-0.0728) ≈ 0.072-0.072=0.
//   abs(pcb.x)≈0 < BarWidth-edgeSmooth=0.019375 -> centerBar=0 -> fillA=0.
//   s1a_raw=box(rotateDeg((0,0.4272)-(0,0.5),45,(2,1))):
//     q=(0,-0.0728)*cellAspect=(0,-0.0728); x'=(-0.7071)*0-(-0.7071)*(-0.0728)=-0.0515;
//     y'=(-0.7071)*(-0.0728)+0=0.0515. box=0.0515 << shapeSize=0.22 -> s1a=0, s1aBorder=0.
//   background=1-0*..=1. cBorderOrBackground=lerp(Background,FillB,1)=FillB=(1,1,1,1).
//   cFill=lerp(FillA=(0,0,0,1), (1,1,1,1), 0)=(0,0,0,1). RGBA8: R=0,G=0,B=0,A=255.
//   ASSERTION B: R<20, G<20, B<20, A>240.
//
// injectBug: set Fill.r/g/b = 1.0 (white FillA) -> on-bar pixel (64,90) becomes white (R>200)
//   -> assertion B fails (R>200 but expected R<20) -> RED.
int runFraserGridSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-frasergrid] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(td);

  // Default params — FraserGrid.t3 values.
  std::map<std::string, float> params;
  // injectBug: flip FillA to white -> on-bar pixels go white -> assertion B fails.
  params["Fill.r"]        = injectBug ? 1.0f : 0.0f;
  params["Fill.g"]        = injectBug ? 1.0f : 0.0f;
  params["Fill.b"]        = injectBug ? 1.0f : 0.0f;
  params["Fill.a"]        = 1.0f;
  params["FillB.r"]       = 1.0f;  params["FillB.g"]       = 1.0f;
  params["FillB.b"]       = 1.0f;  params["FillB.a"]       = 1.0f;
  params["Background.r"]  = 0.67475104f;
  params["Background.g"]  = 0.67498636f;
  params["Background.b"]  = 0.67569184f;
  params["Background.a"]  = 1.0f;
  params["Size.x"]        = 32.0f;  params["Size.y"]        = 16.0f;
  params["Offset.x"]      = 0.0f;   params["Offset.y"]      = 0.0f;
  params["Scale"]         = 4.0f;
  params["Rotate"]        = 0.0f;
  params["Feather"]       = 0.015f;
  params["RotateShapes"]  = 45.0f;
  params["ShapeSize"]     = 0.22f;
  params["BarWidth"]      = 0.035f;
  params["BorderWidth"]   = 0.06f;
  params["RowSwift"]      = 0.0f;
  params["RAffects_BarWidth"]  = 0.0f;
  params["GAffects_ShapeSize"] = 0.0f;
  params["BAffects_LineRatio"] = 0.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1;
  c.inputTexture = nullptr;  // GENERATOR MODE: no upstream texture (TiXL Image=null default)
  c.output = dst;
  c.params = &params;
  cookFraserGrid(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // Assertion A: center pixel (64,64) = background grey plateau.
  const uint32_t ax = W / 2, ay = H / 2;
  size_t ai = ((size_t)ay * W + ax) * 4;
  int aR = out[ai], aG = out[ai+1], aB = out[ai+2], aA = out[ai+3];
  bool centerBg = (aR >= 155 && aR <= 190 && aG >= 155 && aG <= 190 &&
                   aB >= 155 && aB <= 190 && aA >= 240);

  printf("[selftest-frasergrid] A center(%d,%d)=(%d,%d,%d,%d) bgGrey=%d\n",
         ax, ay, aR, aG, aB, aA, centerBg ? 1 : 0);

  // Assertion B: pixel (64,90) = on center bar = FillA = black (when injectBug=false).
  const uint32_t bx = W / 2, by = 90;
  size_t bi = ((size_t)by * W + bx) * 4;
  int bR = out[bi], bG = out[bi+1], bB = out[bi+2], bA = out[bi+3];
  bool barBlack = (bR < 20 && bG < 20 && bB < 20 && bA > 240);

  printf("[selftest-frasergrid] B bar(%d,%d)=(%d,%d,%d,%d) fillA_black=%d\n",
         bx, by, bR, bG, bB, bA, barBlack ? 1 : 0);

  bool pass = centerBg && barBlack;
  printf("[selftest-frasergrid] -> %s\n", pass ? "PASS" : "FAIL");

  dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
