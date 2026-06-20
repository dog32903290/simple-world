// MunchingSquares2 image generator op (Phase C leaf).
// TiXL authority: Operators/Lib/image/generate/MunchingSquares2.cs (slot declarations) +
//   MunchingSquares2.t3 (defaults, uses _ImageFxShaderSetupStatic bd0b9c5b) +
//   Operators/Lib/Assets/shaders/img/generate/MunchingSquares.hlsl (single-pass kernel).
//
// Pattern: XOR/bitwise munching squares. Optional Image composite via BlendColors.
// STEP-0 portability check (passed):
//   1. Single optional Texture2D input (Image), no multi-image seam.
//   2. No gradient-widget, no curve-LUT, no asset-texture, no mip-gen seam.
//   3. _ImageFxShaderSetupStatic class (NOT _multiImageFxSetup — no FloatsToBuffer math trap).
//   4. Children in .t3: only Vector4Components / Vector2Components / IntToFloat decomposers +
//      one _ImageFxShaderSetupStatic instance — all direct cbuffer feeds, no intermediate math.
//   5. Three cbuffers: b0 (ParamConstants floats), b1 (Resolution), b2 (Params — ints).
//
// Sampler: MirrorOnce (from _ImageFxShaderSetupStatic.t3 Wrap=MirrorOnce) →
//   Metal MTL::SamplerAddressModeMirrorClampToEdge.
//
// Forks (named):
//   [fork-mod-macro]           HLSL `#define mod(x,y)` = floor semantics → sw_mod() in Metal.
//   [fork-int-cbuffer]         HLSL b2 uses int types. Metal: int32_t struct at buffer index 2.
//   [fork-sampler-mirror-clamp] DX11 MirrorOnce → Metal MirrorClampToEdge.
//   [fork-IsTextureValid]      Host injects IsTextureValid=1.0 if c.inputTexture, else 0.0.
//
// Self-contained leaf: cookMunchingSquares2 + ImageFilterOp self-registration +
//   runMunchingSquares2SelfTest. CMake GLOB auto-picks this file (point_ops_*.cpp).
// No shared files edited — zero registration collisions.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"   // ImageFilterOp self-registration
#include "runtime/munchingsquares2_params.h"    // MunchingSquares2Params / Resolution / IntParams
#include "runtime/point_graph.h"               // TexCookCtx, cookParam
#include "runtime/tex_op_cache.h"              // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration for the file-scope registrar below.
int runMunchingSquares2SelfTest(bool injectBug);

namespace {

// MunchingSquares2 generator/filter op. One optional Image input.
// When no Image wired: IsTextureValid=0 → shader returns pure munching pattern.
// When Image wired: IsTextureValid=1 → BlendColors composite over upstream.
void cookMunchingSquares2(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "munchingsquares2_vs", "munchingsquares2_fs", fmt);
  if (!rps) return;

  // Sampler: Linear + MirrorClampToEdge.
  // [fork-sampler-mirror-clamp] TiXL _ImageFxShaderSetupStatic.t3 Wrap=MirrorOnce (DX11).
  // Metal equivalent: SamplerAddressModeMirrorClampToEdge.
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeMirrorClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeMirrorClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // --- b0 ParamConstants (MunchingSquares.hlsl cbuffer field order verbatim) ---
  MunchingSquares2Params p{};
  // Black (ShadowColor Vec4, TiXL default (0,0,0,1))
  p.BlackR = cookParam(c, "ShadowColor.r", 0.0f);
  p.BlackG = cookParam(c, "ShadowColor.g", 0.0f);
  p.BlackB = cookParam(c, "ShadowColor.b", 0.0f);
  p.BlackA = cookParam(c, "ShadowColor.a", 1.0f);
  // White (HighlightColor Vec4, TiXL default (1,1,1,1))
  p.WhiteR = cookParam(c, "HighlightColor.r", 1.0f);
  p.WhiteG = cookParam(c, "HighlightColor.g", 1.0f);
  p.WhiteB = cookParam(c, "HighlightColor.b", 1.0f);
  p.WhiteA = cookParam(c, "HighlightColor.a", 1.0f);
  // GrayScaleWeights (Vec4, TiXL default (0.2126, 0.7152, 0.0722, 0.0))
  p.GrayR = cookParam(c, "GrayScaleWeights.r", 0.2126f);
  p.GrayG = cookParam(c, "GrayScaleWeights.g", 0.7152f);
  p.GrayB = cookParam(c, "GrayScaleWeights.b", 0.0722f);
  p.GrayA = cookParam(c, "GrayScaleWeights.a", 0.0f);
  // GainAndBias (Vec2, TiXL default (0.5, 0.5))
  p.GainAndBiasX = cookParam(c, "GainAndBias.x", 0.5f);
  p.GainAndBiasY = cookParam(c, "GainAndBias.y", 0.5f);
  // Stretch (Vec2, TiXL default (1,1))
  p.StretchX = cookParam(c, "Stretch.x", 1.0f);
  p.StretchY = cookParam(c, "Stretch.y", 1.0f);
  // Offset (Vec2, TiXL default (0,0))
  p.OffsetX = cookParam(c, "Offset.x", 0.0f);
  p.OffsetY = cookParam(c, "Offset.y", 0.0f);
  // Scale (Single, TiXL default 4.0)
  p.Scale = cookParam(c, "Scale", 4.0f);
  // IterationFx (Single, TiXL default 0.0)
  p.IterationFx = cookParam(c, "IterationFx", 0.0f);
  // [fork-IsTextureValid] 1.0 when Image wired, 0.0 otherwise.
  p.IsTextureValid = c.inputTexture ? 1.0f : 0.0f;

  // --- b1 Resolution ---
  MunchingSquares2Resolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // --- b2 Params (ints) ---
  // [fork-int-cbuffer] HLSL b2 is an integer cbuffer; we mirror as int32_t struct.
  MunchingSquares2IntParams ip{};
  ip.Method    = (int32_t)std::round(cookParam(c, "Method",    0.0f));
  ip.Iteration = (int32_t)std::round(cookParam(c, "Iterations", 10.0f));
  ip.BlendMode = (int32_t)std::round(cookParam(c, "BlendMode", 0.0f));

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  // Bind optional upstream image (null guard: IsTextureValid gate in shader).
  if (c.inputTexture)
    enc->setFragmentTexture(const_cast<MTL::Texture*>(c.inputTexture), 0);
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p,   sizeof(MunchingSquares2Params),     MUNCHINGSQUARES2_Params);
  enc->setFragmentBytes(&res, sizeof(MunchingSquares2Resolution), MUNCHINGSQUARES2_Resolution);
  enc->setFragmentBytes(&ip,  sizeof(MunchingSquares2IntParams),  MUNCHINGSQUARES2_IntParams);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned, not released here
}

}  // namespace

// Self-registration. File-scope static → pre-main dynamic init feeds imageFilterSpecSink()
// + texReg() + imageFilterSelfTests(). No shared file edited.
static const ImageFilterOp _reg_munchingsquares2{
    // MunchingSquares2 (TiXL Lib.image.generate.MunchingSquares2): XOR/bitwise munching-squares
    // pattern generator. Optional upstream Texture2D composite via BlendMode. Methods: Classic/
    // Patterns/Or/Multiply/Chaos (int enum). Texture2D in (optional) → Texture2D out.
    // Params mirror MunchingSquares2.cs: ShadowColor/HighlightColor (Vec4), GrayScaleWeights (Vec4),
    // GainAndBias/Stretch/Offset (Vec2), Scale/IterationFx (float), Method/Iterations/BlendMethod (int).
    // FORKS (named): [fork-mod-macro] [fork-int-cbuffer] [fork-sampler-mirror-clamp] [fork-IsTextureValid].
    {"MunchingSquares2", "MunchingSquares2",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // ShadowColor (Vec4, TiXL default (0,0,0,1) — black)
      {"ShadowColor.r", "ShadowColor", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ShadowColor.g", "ShadowColor.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ShadowColor.b", "ShadowColor.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ShadowColor.a", "ShadowColor.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // HighlightColor (Vec4, TiXL default (1,1,1,1) — white)
      {"HighlightColor.r", "HighlightColor", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"HighlightColor.g", "HighlightColor.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"HighlightColor.b", "HighlightColor.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"HighlightColor.a", "HighlightColor.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // GrayScaleWeights (Vec4, TiXL default (0.2126, 0.7152, 0.0722, 0.0))
      {"GrayScaleWeights.r", "GrayScaleWeights", "Float", true, 0.2126f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"GrayScaleWeights.g", "GrayScaleWeights.g", "Float", true, 0.7152f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"GrayScaleWeights.b", "GrayScaleWeights.b", "Float", true, 0.0722f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"GrayScaleWeights.a", "GrayScaleWeights.a", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // GainAndBias (Vec2, TiXL default (0.5, 0.5))
      {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Stretch (Vec2, TiXL default (1,1))
      {"Stretch.x", "Stretch", "Float", true, 1.0f, 0.01f, 8.0f, Widget::Vec, {}, true, 2},
      {"Stretch.y", "Stretch.y", "Float", true, 1.0f, 0.01f, 8.0f, Widget::Vec, {}, true, 1},
      // Offset (Vec2, TiXL default (0,0))
      {"Offset.x", "Offset", "Float", true, 0.0f, -8.0f, 8.0f, Widget::Vec, {}, true, 2},
      {"Offset.y", "Offset.y", "Float", true, 0.0f, -8.0f, 8.0f, Widget::Vec, {}, true, 1},
      // Scale (Single, TiXL default 4.0)
      {"Scale", "Scale", "Float", true, 4.0f, 0.1f, 64.0f, Widget::Slider},
      // IterationFx (Single, TiXL default 0.0)
      {"IterationFx", "IterationFx", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Slider},
      // Method (int enum, TiXL Methods: Classic=0/Patterns=1/Or=2/Multiply=3/Chaos=4)
      {"Method", "Method", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"Classic", "Patterns", "Or", "Multiply", "Chaos"}, true},
      // Iterations (Int, TiXL default 10)
      {"Iterations", "Iterations", "Float", true, 10.0f, 1.0f, 64.0f, Widget::Slider},
      // BlendMethod (int, TiXL SharedEnums.RgbBlendModes, default 0 = normal)
      {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 9.0f, Widget::Enum,
       {"Normal", "Screen", "Multiply", "Overlay", "Difference",
        "UseA", "UseB", "ColorDodge", "LinearDodge", "MaskAlpha"}, true},
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "MunchingSquares2", cookMunchingSquares2, "munchingsquares2", runMunchingSquares2SelfTest};

// --- MunchingSquares2 MATH golden ------------------------------------------------------------
//
// Ground truth from TiXL MunchingSquares.hlsl (hand-derived, no Image input → pure generator).
// Setup: 256×256 texture, Scale=4.0, Stretch=(1,1), Offset=(0,0), Method=Classic(0), Iterations=10.
//
// Shader: epsilonScale = 4.0 - 0.0001 = 3.9999
//   divisions = float2(256, 256) / 3.9999 ≈ (64.0016, 64.0016)
//   gridSize ≈ (0.015624, 0.015624)
//
// PROBE A (center cell — plateau, Method=Classic):
//   texCoord = (128.5/256, 128.5/256) ≈ (0.50195, 0.50195); p = (0.00195, 0.00195) after -0.5
//   pInCell = sw_mod(0.00195, 0.015624) = 0.00195  (floor(0.00195/0.015624) = 0)
//   cellIds = (0.5, 0.5)
//   X = int(0.5 * 64.0016 + 0.5) = int(32.5) = 32
//   Y = 32;  F = int(10 + 0.5) = 10
//   Classic: (32 ^ 32) & 10 = 0 & 10 = 0; !(0) = 1 → White = (1,1,1,1) → RGBA8 (255,255,255,255)
//   → R=255, G=255, B=255 (all channels saturated)
//
// PROBE B (adjacent cell shifted by 2 cell-widths in Y — Black plateau):
//   texCoord ≈ (0.50195, 0.50195 + 2/64.0016) ≈ (0.50195, 0.53319)
//   p.y = 0.03319 after -0.5
//   pInCell.y = sw_mod(0.03319, 0.015624):
//     floor(0.03319 / 0.015624) = floor(2.125) = 2; pInCell.y = 0.03319 - 2*0.015624 = 0.001942
//   cellIds.y = 0.03319 - 0.001942 + 0.5 = 0.53125
//   Y = int(0.53125 * 64.0016 + 0.5) = int(34.0009 + 0.5) = int(34.5) = 34
//   X = 32 (unchanged); F = 10
//   Classic: (32 ^ 34) & 10 = 2 & 10 = 2; !(2) = 0 → Black = (0,0,0,1) → RGBA8 (0,0,0,255)
//   → R=0, G=0, B=0
//
// injectBug: Iterations=1 → F=1
//   PROBE A: (32 ^ 32) & 1 = 0 & 1 = 0; !(0) = 1 → still White (same result, probe A not flipped)
//   PROBE B: (32 ^ 34) & 1 = 2 & 1 = 0; !(0) = 1 → White! Was Black → FLIPS.
//   Assertion "PROBE B is black (R<20)" FAILS under injectBug → RED teeth. ✓
//
// This is a d=0 saturated plateau probe (center cells, deterministic integer XOR output),
// not dependent on fwidth/smoothstep/noise. Golden values hand-derived from TiXL formula.
int runMunchingSquares2SelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-munchingsquares2] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(td);

  // No Image input (pure generator). ShadowColor=black, HighlightColor=white for easy assertion.
  std::map<std::string, float> params;
  params["ShadowColor.r"] = 0.0f; params["ShadowColor.g"] = 0.0f;
  params["ShadowColor.b"] = 0.0f; params["ShadowColor.a"] = 1.0f;
  params["HighlightColor.r"] = 1.0f; params["HighlightColor.g"] = 1.0f;
  params["HighlightColor.b"] = 1.0f; params["HighlightColor.a"] = 1.0f;
  params["GrayScaleWeights.r"] = 0.2126f; params["GrayScaleWeights.g"] = 0.7152f;
  params["GrayScaleWeights.b"] = 0.0722f; params["GrayScaleWeights.a"] = 0.0f;
  params["GainAndBias.x"] = 0.5f; params["GainAndBias.y"] = 0.5f;
  params["Stretch.x"] = 1.0f; params["Stretch.y"] = 1.0f;
  params["Offset.x"] = 0.0f; params["Offset.y"] = 0.0f;
  params["Scale"] = 4.0f;
  params["IterationFx"] = 0.0f;
  params["Method"] = 0.0f;  // Classic
  // [injectBug]: Iterations=1 → F=1; causes PROBE B (X=32,Y=34) to flip Black→White.
  params["Iterations"] = injectBug ? 1.0f : 10.0f;
  params["BlendMode"] = 0.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1;
  c.inputTexture = nullptr;  // generator: no Image wired
  c.output = dst;
  c.params = &params;
  cookMunchingSquares2(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // PROBE A: pixel at (128, 128) — center of texture, cell (X=32,Y=32).
  // Hand-calc: Classic (32^32)&10 = 0; !0=1 → White → R=255. Both normal and injectBug=White.
  // (This probe validates the white-plateau path, not the injectBug distinction.)
  uint32_t axp = W / 2, ayp = H / 2;
  int aR = (int)out[((size_t)ayp * W + axp) * 4 + 0];
  int aG = (int)out[((size_t)ayp * W + axp) * 4 + 1];
  int aB = (int)out[((size_t)ayp * W + axp) * 4 + 2];
  bool probeAwhite = (aR > 230 && aG > 230 && aB > 230);

  // PROBE B: pixel at (128, 136) — shifted 2 cells up from center in Y.
  // texCoord.y ≈ (136.5/256) = 0.53320. Cell Y=34, X=32.
  // Classic: (32^34)&10 = 2&10 = 2; !2=0 → Black → R=0.
  // Under injectBug (F=1): (32^34)&1 = 2&1 = 0; !0=1 → White → FAILS assertion.
  uint32_t bxp = W / 2, byp = 136;  // pixel row 136
  int bR = (int)out[((size_t)byp * W + bxp) * 4 + 0];
  int bG = (int)out[((size_t)byp * W + bxp) * 4 + 1];
  int bB = (int)out[((size_t)byp * W + bxp) * 4 + 2];
  bool probeBblack = (bR < 20 && bG < 20 && bB < 20);  // FAILS under injectBug (Iterations=1)

  bool pass = probeAwhite && probeBblack;
  printf("[selftest-munchingsquares2] "
         "probeA(128,128) R=%d G=%d B=%d (want white) "
         "probeB(128,136) R=%d G=%d B=%d (want black) "
         "-> %s%s\n",
         aR, aG, aB, bR, bG, bB,
         pass ? "PASS" : "FAIL",
         injectBug ? " [injectBug: Iterations=1 → probeB flips Black→White → FAIL expected]" : "");

  dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
