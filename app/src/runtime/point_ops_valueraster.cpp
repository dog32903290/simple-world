// ValueRaster image generator / raster-grid op (Phase C leaf).
// TiXL authority: Operators/Lib/image/generate/pattern/ValueRaster.cs (ports + defaults) +
//   ValueRaster.t3 (Wrap=Clamp, GenerateMips=true, defaults: Color=(1,1,1,0.695),
//   Background=(0,0,0,0), MixOriginal=1.0, RangeX=(0,1), RangeY=(0,1), MajorLineWidth=1.0,
//   MinorLineWidth=0.25, Density=(1000,1000), Resolution=(0,0)=WindowFollow) +
//   Assets/shaders/img/fx/ValueRaster.hlsl (psMain: adaptive log10-decade raster grid,
//   AA smoothstep via fwidth, color-temperature gradient by p.y, alpha-composite over input).
//
// PORTABILITY HARDGATE (Cut 49/55/58 discipline):
//   - Single optional texture input (Image, default null) — generator + optional overlay.
//     No multi-image seam required.
//   - STEP-0 .t3 backward-trace: Color/Background/RangeX/RangeY/Density → Vec4Components/
//     Vec2Components unwrappers → _ImageFxShaderSetupStatic slot 4ef6f204. Zero math-node
//     intermediates (no Multiply/IntToFloat). Direct 1:1 port confirmed.
//   - No gradient/curve-LUT/asset-texture/mip dependency.
//   - _ImageFxShaderSetupStatic class (not compound).
//
// GENERATOR CONVENTION (Cut 61): null Image input → 1×1 transparent-black dummy texture so
//   valueraster_fs always has a valid texture2d handle, orgColor=(0,0,0,0) = TiXL null SRV.
//   Pattern from point_ops_sinform.cpp / point_ops_checkerboard.cpp.
//
// HLSL→MSL NOTES (named forks, full detail in valueraster.metal):
//   [fork-mod-floor-macro]  HLSL #define mod() only for reference; Grid1D uses frac() → fract().
//   [fork-frac-fract]  HLSL frac() → MSL fract().
//   [fork-sampler-clamp]  TiXL .t3 Wrap=Clamp → ClampToEdge on s/t axes.
//   [fork-log10-helper]  INV_LN10=0.4342944819f, log(max(x,1e-20))*INV_LN10 verbatim.
//   [fork-distancefromcenter-gradient]  HLSL L96-100 color temperature gradient verbatim.
//
// Self-contained leaf: cookValueRaster + _reg_valueraster + runValueRasterSelfTest.
// Selftest forward-decl in this file only (not in point_ops.h) — parallel-lane zero conflict.
// CMake CONFIGURE_DEPENDS glob auto-picks point_ops_valueraster.cpp; no CMake edit needed.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"               // TexCookCtx, cookParam, registerTexOp
#include "runtime/tex_op_cache.h"              // cachedTexPSO (D2-2 PSO reuse)
#include "runtime/valueraster_params.h"        // ValueRasterParams/Resolution, VALUERASTER_* bindings

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration so the file-scope ImageFilterOp registrar can reference runValueRasterSelfTest.
int runValueRasterSelfTest(bool injectBug);

namespace {

// Helper: create a 1×1 transparent-black dummy texture for the no-input case.
// ValueRaster is a generator (TiXL Image=null default). We still run the shader;
// a 1×1 dummy gives valueraster_fs a valid texture2d handle → orgColor=(0,0,0,0).
static MTL::Texture* makeVRDummyBlackTexture(MTL::Device* dev) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  const uint8_t px[4] = {0, 0, 0, 0};
  t->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, px, 4);
  return t;
}

// ValueRaster texture op: single render pass. Optionally reads c.inputTexture (if wired), always
// writes c.output. If no upstream: the grid is drawn over black (transparent-black dummy).
void cookValueRaster(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "valueraster_vs", "valueraster_fs", fmt);
  if (!rps) return;

  // TiXL ValueRaster.t3: Wrap=Clamp → ClampToEdge.
  // [fork-sampler-clamp]: TiXL .t3 Wrap=Clamp (DX11 TextureAddressMode.Clamp).
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // Cbuffer b0 — mirror ValueRaster.hlsl cbuffer ParamConstants field order verbatim.
  // TiXL ValueRaster.t3 defaults used when no param wired.
  ValueRasterParams p{};
  // LineColor (Vec4, TiXL Color default: X=1,Y=1,Z=1,W=0.695)
  p.LineColorR = cookParam(c, "Color.r", 1.0f);
  p.LineColorG = cookParam(c, "Color.g", 1.0f);
  p.LineColorB = cookParam(c, "Color.b", 1.0f);
  p.LineColorA = cookParam(c, "Color.a", 0.695f);
  // BackgroundColor (Vec4, TiXL Background default: (0,0,0,0))
  p.BackgroundColorR = cookParam(c, "Background.r", 0.0f);
  p.BackgroundColorG = cookParam(c, "Background.g", 0.0f);
  p.BackgroundColorB = cookParam(c, "Background.b", 0.0f);
  p.BackgroundColorA = cookParam(c, "Background.a", 0.0f);
  // RangeX (Vec2, TiXL default: (0,1))
  p.RangeXMin = cookParam(c, "RangeX.x", 0.0f);
  p.RangeXMax = cookParam(c, "RangeX.y", 1.0f);
  // RangeY (Vec2, TiXL default: (0,1))
  p.RangeYMin = cookParam(c, "RangeY.x", 0.0f);
  p.RangeYMax = cookParam(c, "RangeY.y", 1.0f);
  // MajorLineWidth (float, TiXL default: 1.0)
  p.MajorLineWidth = cookParam(c, "MajorLineWidth", 1.0f);
  // MinorLineWidth (float, TiXL default: 0.25)
  p.MinorLineWidth = cookParam(c, "MinorLineWidth", 0.25f);
  // Density (Vec2, TiXL default: (1000,1000))
  p.DensityX = cookParam(c, "Density.x", 1000.0f);
  p.DensityY = cookParam(c, "Density.y", 1000.0f);
  // MixOriginal (float, TiXL default: 1.0)
  p.MixOriginal = cookParam(c, "MixOriginal", 1.0f);

  ValueRasterResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // Bind input texture (or 1×1 black dummy when no upstream is wired).
  // FORK (named): TiXL Image=null default → orgColor=(0,0,0,0) → grid over black.
  // sw host provides the dummy so valueraster_fs always has a valid texture2d binding.
  MTL::Texture* dummyTex = nullptr;
  const MTL::Texture* inputTex = c.inputTexture;
  if (!inputTex) {
    dummyTex = makeVRDummyBlackTexture(c.dev);
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
  enc->setFragmentBytes(&p,   sizeof(ValueRasterParams),     VALUERASTER_Params);
  enc->setFragmentBytes(&res, sizeof(ValueRasterResolution), VALUERASTER_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();
  if (dummyTex) dummyTex->release();
}

}  // namespace

// Self-registration. NodeSpec mirrors ValueRaster.cs inputs.
// Vec4 inputs (Color, Background) decomposed to .r/.g/.b/.a scalar ports.
// Vec2 inputs (RangeX, RangeY, Density) decomposed to .x/.y scalar ports.
// Resolution: standard Output Size enum + CustomW/H.
// FORKS (named):
//   1. TiXL Image input optional (default null): sw accepts null input, renders grid over black.
//   2. TiXL TextureFormat (DXGI Format enum) not modelled (sw uses RGBA8Unorm via Resolution enum).
//   3. Fixed Clamp sampler (TiXL .t3 Wrap=Clamp verbatim).
//   4. TiXL GenerateMips=true in .t3 — not modelled per-op (registered in
//      imageFilterMippedOutputTypes if needed; kept out for now — see note below).
static const ImageFilterOp _reg_valueraster{
    // ValueRaster (TiXL Lib.image.generate.pattern.ValueRaster): adaptive log10-decade raster
    // grid generator. Optional Texture2D in (Image, default null) → Texture2D out.
    // When no Image: grid drawn over black/transparent.
    // Kernel: ValueRaster.hlsl — fwidth AA grid lines, log10 decade stepping with fade,
    // color-temperature gradient by world-space Y, alpha-composite over input.
    {"ValueRaster", "ValueRaster",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Color (Vec4, TiXL t3 default (1,1,1,0.695) — near-opaque white line color)
      {"Color.r", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Color.g", "Color.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Color.b", "Color.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Color.a", "Color.a", "Float", true, 0.695f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Background (Vec4, TiXL t3 default (0,0,0,0) — transparent)
      {"Background.r", "Background", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Background.g", "Background.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.b", "Background.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.a", "Background.a", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // MixOriginal (float, TiXL t3 default 1.0)
      {"MixOriginal", "MixOriginal", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Slider},
      // RangeX (Vec2, TiXL t3 default (0,1) — world-space X range)
      {"RangeX.x", "RangeX", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 2},
      {"RangeX.y", "RangeX.y", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
      // RangeY (Vec2, TiXL t3 default (0,1) — world-space Y range)
      {"RangeY.x", "RangeY", "Float", true, 0.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 2},
      {"RangeY.y", "RangeY.y", "Float", true, 1.0f, -100.0f, 100.0f, Widget::Vec, {}, true, 1},
      // MajorLineWidth (float, TiXL t3 default 1.0 — in pixels)
      {"MajorLineWidth", "MajorLineWidth", "Float", true, 1.0f, 0.0f, 10.0f, Widget::Slider},
      // MinorLineWidth (float, TiXL t3 default 0.25 — in pixels)
      {"MinorLineWidth", "MinorLineWidth", "Float", true, 0.25f, 0.0f, 5.0f, Widget::Slider},
      // Density (Vec2, TiXL t3 default (1000,1000) — pixels per grid-unit target density)
      {"Density.x", "Density", "Float", true, 1000.0f, 1.0f, 5000.0f, Widget::Vec, {}, true, 2},
      {"Density.y", "Density.y", "Float", true, 1000.0f, 1.0f, 5000.0f, Widget::Vec, {}, true, 1},
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "ValueRaster", cookValueRaster, "valueraster", runValueRasterSelfTest};

// --- ValueRaster MATH golden -------------------------------------------------------------------
// Configuration: default params (Color=(1,1,1,0.695), Background=(0,0,0,0), MixOriginal=1.0,
//   RangeX=(0,1), RangeY=(0,1), MajorLineWidth=1.0, MinorLineWidth=0.25, Density=(1000,1000)).
// No input texture (generator mode: no upstream wired).
//
// ASSERTION A (MAJOR-LINE DETERMINISTIC): pixel (0,0) is on the major gridline at world-X=0,
//   world-Y=1. At this pixel, d=0 in Grid1D for majorX → independent of fwidth magnitude.
//
//   Derivation (256×256):
//   uv=(0,0) → rasterUv=(0,1) → p=(0,1).
//   fwidth(p.x) ≈ 1/256 ≈ 0.003906.
//   rawStep = 0.003906 * 1000 = 3.906. log10(3.906) ≈ 0.5918. decadeExp=0. majorStep=1.0.
//   Grid1D(p.x=0, majorStep=1, width=1): s=0, fract(0)=0, d=min(0,1)=0. fs=0.003906.
//     w=1.0*0.003906. smoothstep(w, w+fs, 0) = 0 → Grid1D=1.0. majorMask=1.0. lineMask=1.0.
//   c = lerp(Background, LineColor, lineMask * LineColor.a) = lerp((0,0,0,0),(1,1,1,0.695),0.695).
//     c.a = 0.695 * 0.695 = 0.48302.
//   Gradient at p.y=1.0: distanceFromCenter=pow(sat(0.1),0.3)=pow(0.1,0.3)≈0.5012.
//     amplifyCenterLine=lerp(1.5,1,sat(100))=1.0. colorTempFactor.a = lerp(1,1,0.5012)=1.0.
//   c.a_final = 0.48302 * 1.0 * 1.0 = 0.48302 → uint8 = round(0.48302*255) = 123.
//   MixOriginal=1, orgColor.a=0 → final alpha = c.a = 0.48302 → uint8 = 123.
//   GPU measured: 123. GOLDEN: alpha ∈ [120, 126] (±3 for LSB rounding on Apple GPU).
//
//   injectBug: Color.a=0 → lerp weight=0 → c=Background=(0,0,0,0) → alpha=0 → RED.
//
// ASSERTION B (PLATEAU EXACT): pixel (96,96) → rasterUv=(0.375, 0.625) → p=(0.375, 0.625).
//   p.x=0.375 → s=0.375/0.1=3.75, fract=0.75, d=min(0.75,0.25)=0.25.
//   fs=fwidth(3.75)≈0.039062. w=0.25*0.039062=0.009766.
//   smoothstep(0.009766, 0.04883, 0.25): 0.25 >> 0.04883 → returns 1.0 → Grid1D=0.
//   All grid1D terms evaluate to 0 → lineMask=0 → c=Background → alpha=0. GPU measured: 0.
//   GOLDEN: alpha == 0 (exact — deep in plateau, no grid line within many fwidth distances).
//
// FORK NOTES:
//   [fork-generatemips-unmodelled]: TiXL ValueRaster.t3 sets GenerateMips=true. sw output is
//     un-mipped (not registered in imageFilterMippedOutputTypes). No LOD consumer exists yet;
//     non-blocking. Register there when an LOD consumer is added.
//   [fork-samplelevel]: shader now uses sample(texSampler, uv, level(0.0)) matching TiXL HLSL
//     SampleLevel(texSampler, uv, 0.0). Bit-identical on un-mipped inputs, but correctly
//     pins LOD=0 to match TiXL intent rather than relying on implicit gradient.
int runValueRasterSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-valueraster] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(td);

  // ---- CASE A+B: default params (grid visible, center on grid line) -------------------------
  std::map<std::string, float> params;
  // injectBug: set Color.a=0 → LineColor.a=0 → all grid invisible → center goes dark.
  params["Color.r"] = 1.0f; params["Color.g"] = 1.0f; params["Color.b"] = 1.0f;
  params["Color.a"] = injectBug ? 0.0f : 0.695f;  // BUG: zero alpha → no grid
  params["Background.r"] = 0.0f; params["Background.g"] = 0.0f;
  params["Background.b"] = 0.0f; params["Background.a"] = 0.0f;
  params["MixOriginal"]    = 1.0f;
  params["RangeX.x"]       = 0.0f; params["RangeX.y"] = 1.0f;
  params["RangeY.x"]       = 0.0f; params["RangeY.y"] = 1.0f;
  params["MajorLineWidth"] = 1.0f;
  params["MinorLineWidth"] = 0.25f;
  params["Density.x"]      = 1000.0f; params["Density.y"] = 1000.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1;
  c.inputTexture = nullptr;  // GENERATOR MODE: no upstream texture (TiXL Image=null default)
  c.output = dst;
  c.params = &params;
  cookValueRaster(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // Assertion A: pixel (0,0) — exactly on major gridline (world-X=0, world-Y=1). d=0 in Grid1D
  // for majorX → fwidth-independent result. Expected alpha=123 ±3 (see MATH golden above).
  // injectBug (Color.a=0): c=Background everywhere → alpha=0 → RED.
  int aA = (int)out[3];  // pixel(0,0) alpha (offset = 0*W*4 + 0*4 + 3 = 3)
  bool majorLineVisible = (aA >= 120 && aA <= 126);

  // Assertion B: pixel (96,96) — plateau, deep off-grid. Expected alpha == 0 (exact).
  // p=(0.375,0.625): nearest minor line distance 0.25/step >> fwidth → lineMask=0 → Background.
  const uint32_t bx = W * 3 / 8, by = H * 3 / 8;
  size_t bi = ((size_t)by * W + bx) * 4;
  int bA = out[bi+3];
  bool plateauBlack = (bA == 0);

  printf("[selftest-valueraster] A major-line(0,0) alpha=%d majorLineVisible=%d [expect 120-126] | "
         "B plateau(%d,%d) alpha=%d plateauBlack=%d [expect 0]\n",
         aA, majorLineVisible ? 1 : 0,
         bx, by, bA, plateauBlack ? 1 : 0);

  bool pass = majorLineVisible && plateauBlack;
  printf("[selftest-valueraster] -> %s\n", pass ? "PASS" : "FAIL");

  dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
