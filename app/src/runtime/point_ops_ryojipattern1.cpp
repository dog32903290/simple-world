// RyojiPattern1 image generator op (Phase C leaf). Recursive-cel grayscale pattern.
// TiXL authority: Operators/Lib/image/generate/pattern/RyojiPattern1.cs (Image/Background/
// Foreground/MixOriginal/Contrast/ForgroundRatio/Highlight/HighlightProbability/HighlightSeed/
// Iterations/Splits/SplitProbability/ScrollSpeed/ScrollProbability/Padding/Seed/Resolution/
// GenerateMipmaps) + RyojiPattern1.t3 (defaults below) + Assets/shaders/img/generate/
// RyojiPattern1.hlsl (the single-pass kernel ported verbatim to ryojipattern1.metal).
//
// PORTABILITY HARDGATE (Cut-49/55/58 discipline):
//   - Optional single Texture2D input (ImageA, t0). STEP-0 backward-trace of RyojiPattern1.t3:
//     every scalar/vector input wires through the _ImageFxShaderSetup2 FloatsToBuffer slot
//     (8e9b8826) via ONLY Vec4Components / Vec2Components (pure component split), ZERO math nodes
//     (no Multiply/IntToFloat). So the b0 cbuffer is filled 1:1 from the op's Float inputs in
//     RyojiPattern1.hlsl declaration order (see ryojipattern1_params.h).
//   - Sampler address mode = WRAP. RyojiPattern1.t3 _ImageFxShaderSetup2 child (6e680b83) input
//     "Wrap" (c80d3700) = "Wrap" -> SamplerAddressModeRepeat (matched verbatim, NOT clamp).
//   - _ImageFxShaderSetup2 class (Source="Lib:shaders/img/generate/RyojiPattern1.hlsl").
//
// FORK (named — Image optional / MixOriginal=0 default): TiXL Image default = null and
// MixOriginal default = 0, so `color = lerp(Foreground, originalColor, MixOriginal)` ignores the
// sample entirely (= Foreground) for the default node. We keep the Image input slot and bind it
// when wired; when unwired we bind a 1x1 transparent-black dummy (valid bind; MixOriginal=0 keeps
// it invisible). Same fork class as Pixelate's white-default Shape.
//
// FORK (named — BeatTime host-fed): RyojiPattern1.hlsl reads `beatTime` (TimeConstants b1) only in
// the scroll term. cookRyojiPattern1 fills RyojiPattern1Time.BeatTime; a headless cook has no
// global clock so BeatTime = 0 -> deterministic (parity-safe, same class as Grain's host-fed Time).
//
// Self-contained leaf: cookRyojiPattern1 + ImageFilterOp self-registration + runRyojiPattern1SelfTest.
// CMake glob auto-picks this file (CMakeLists.txt CONFIGURE_DEPENDS point_ops_*.cpp).
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
#include "runtime/image_filter_op_registry.h"   // ImageFilterOp self-registration
#include "runtime/point_graph.h"                 // TexCookCtx, cookParam
#include "runtime/ryojipattern1_params.h"        // RyojiPattern1Params/Time, RYOJIPATTERN1_*
#include "runtime/tex_op_cache.h"                // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration so the file-scope registrar can reference the golden defined below it.
int runRyojiPattern1SelfTest(bool injectBug);

namespace {

// 1x1 transparent-black dummy for the optional Image bind (so the t0 bind is always valid).
// MixOriginal=0 (default) keeps the sampled color out of the output.
MTL::Texture* makeDummyTex(MTL::Device* dev) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  uint8_t px[4] = {0, 0, 0, 0};
  t->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, px, 4);
  return t;
}

// RyojiPattern1 generator op: single fullscreen pass. Image is optional (default unwired / no-op).
void cookRyojiPattern1(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "ryojipattern1_vs", "ryojipattern1_fs", fmt);
  if (!rps) return;

  // Sampler: linear + WRAP (RyojiPattern1.t3 _ImageFxShaderSetup2 address mode = "Wrap").
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeRepeat);  // TiXL "Wrap"
  sd->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL RyojiPattern1.t3 defaults. cbuffer b0 field order verbatim (see ryojipattern1_params.h).
  RyojiPattern1Params p{};
  // Background (Vec4, default (0,0,0,1))
  p.BackgroundR = cookParam(c, "Background.r", 0.0f);
  p.BackgroundG = cookParam(c, "Background.g", 0.0f);
  p.BackgroundB = cookParam(c, "Background.b", 0.0f);
  p.BackgroundA = cookParam(c, "Background.a", 1.0f);
  // Foreground (Vec4, default (1,1,1,1))
  p.ForegroundR = cookParam(c, "Foreground.r", 1.0f);
  p.ForegroundG = cookParam(c, "Foreground.g", 1.0f);
  p.ForegroundB = cookParam(c, "Foreground.b", 1.0f);
  p.ForegroundA = cookParam(c, "Foreground.a", 1.0f);
  // Highlight (Vec4, default (1,0,0,1))
  p.HighlightR = cookParam(c, "Highlight.r", 1.0f);
  p.HighlightG = cookParam(c, "Highlight.g", 0.0f);
  p.HighlightB = cookParam(c, "Highlight.b", 0.0f);
  p.HighlightA = cookParam(c, "Highlight.a", 1.0f);
  // Subdivisions = "Splits" input (Vec2, default (4,3))
  p.SubdivisionsX = cookParam(c, "Splits.x", 4.0f);
  p.SubdivisionsY = cookParam(c, "Splits.y", 3.0f);
  // SplitProbability (Vec2, default (0, 0.27666667))
  p.SplitProbabilityX = cookParam(c, "SplitProbability.x", 0.0f);
  p.SplitProbabilityY = cookParam(c, "SplitProbability.y", 0.27666667f);
  // ScrollSpeed (Vec2, default (0, -0.23333332))
  p.ScrollSpeedX = cookParam(c, "ScrollSpeed.x", 0.0f);
  p.ScrollSpeedY = cookParam(c, "ScrollSpeed.y", -0.23333332f);
  // ScrollProbability (Vec2, default (0, 0.5))
  p.ScrollProbabilityX = cookParam(c, "ScrollProbability.x", 0.0f);
  p.ScrollProbabilityY = cookParam(c, "ScrollProbability.y", 0.5f);
  // Padding (Vec2, default (0.02, 0.023333333))
  p.PaddingX = cookParam(c, "Padding.x", 0.02f);
  p.PaddingY = cookParam(c, "Padding.y", 0.023333333f);
  // Scalars
  p.Contrast             = cookParam(c, "Contrast", 0.75f);
  p.Iterations           = cookParam(c, "Iterations", 7.0f);
  p.Seed                 = cookParam(c, "Seed", 0.0f);
  p.ForegroundRatio      = cookParam(c, "ForegroundRatio", 0.5f);
  p.HighlightProbability = cookParam(c, "HighlightProbability", 0.01f);
  p.MixOriginal          = cookParam(c, "MixOriginal", 0.0f);
  p.HighlightSeed        = cookParam(c, "HighlightSeed", 0.0f);

  // Time b1: BeatTime host-fed (0 in headless cook -> deterministic). FORK (named, see header).
  RyojiPattern1Time t{};
  t.BeatTime = cookParam(c, "BeatTime", 0.0f);

  // Optional Image bind: input texture when wired, else a 1x1 transparent-black dummy.
  MTL::Texture* dummy = nullptr;
  const MTL::Texture* img = c.inputTexture;
  if (!img) { dummy = makeDummyTex(c.dev); img = dummy; }

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
  enc->setFragmentBytes(&p, sizeof(RyojiPattern1Params), RYOJIPATTERN1_Params);
  enc->setFragmentBytes(&t, sizeof(RyojiPattern1Time),   RYOJIPATTERN1_Time);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();
  if (dummy) dummy->release();
  // rps is cache-owned (tex_op_cache), not released here.
}

}  // namespace

// Self-registration. File-scope static ImageFilterOp feeds imageFilterSpecSink() + texReg()
// + imageFilterSelfTests() during pre-main dynamic init. No shared file edited (CMake glob).
static const ImageFilterOp _reg_ryojipattern1{
    // RyojiPattern1 (TiXL Lib.image.generate.pattern.RyojiPattern1): recursive-cel grayscale
    // pattern. Optional Texture2D in (default null + MixOriginal=0 -> unused) -> Texture2D out.
    // Params mirror RyojiPattern1.cs/.t3. FORKS (named): Image optional (1x1 dummy when unwired,
    // MixOriginal=0 default keeps it invisible); BeatTime host-fed (0 headless -> deterministic);
    // Int Iterations modeled as Float. Sampler = WRAP (RyojiPattern1.t3 verbatim).
    {"RyojiPattern1", "RyojiPattern1",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Background (Vec4, TiXL default (0,0,0,1))
      {"Background.r", "Background", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Background.g", "Background.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.b", "Background.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.a", "Background.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Foreground (Vec4, TiXL default (1,1,1,1))
      {"Foreground.r", "Foreground", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Foreground.g", "Foreground.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Foreground.b", "Foreground.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Foreground.a", "Foreground.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // MixOriginal (Single, TiXL default 0)
      {"MixOriginal", "MixOriginal", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider},
      // Contrast (Single, TiXL default 0.75)
      {"Contrast", "Contrast", "Float", true, 0.75f, 0.0f, 1.0f, Widget::Slider},
      // ForegroundRatio (TiXL input "ForgroundRatio", Single, default 0.5)
      {"ForegroundRatio", "ForegroundRatio", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Slider},
      // Highlight (Vec4, TiXL default (1,0,0,1))
      {"Highlight.r", "Highlight", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Highlight.g", "Highlight.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Highlight.b", "Highlight.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Highlight.a", "Highlight.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // HighlightProbability (Single, TiXL default 0.01)
      {"HighlightProbability", "HighlightProbability", "Float", true, 0.01f, 0.0f, 1.0f, Widget::Slider},
      // HighlightSeed (Single, TiXL default 0)
      {"HighlightSeed", "HighlightSeed", "Float", true, 0.0f, 0.0f, 100.0f, Widget::Slider},
      // Iterations (Single in TiXL, default 7; shader clamps to min(.,10))
      {"Iterations", "Iterations", "Float", true, 7.0f, 1.0f, 10.0f, Widget::Slider},
      // Splits = Subdivisions (Int2 in TiXL, default (4,3))
      {"Splits.x", "Splits", "Float", true, 4.0f, 1.0f, 16.0f, Widget::Vec, {}, true, 2},
      {"Splits.y", "Splits.y", "Float", true, 3.0f, 1.0f, 16.0f, Widget::Vec, {}, true, 1},
      // SplitProbability (Vec2, TiXL default (0, 0.27666667))
      {"SplitProbability.x", "SplitProbability", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"SplitProbability.y", "SplitProbability.y", "Float", true, 0.27666667f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // ScrollSpeed (Vec2, TiXL default (0, -0.23333332))
      {"ScrollSpeed.x", "ScrollSpeed", "Float", true, 0.0f, -4.0f, 4.0f, Widget::Vec, {}, true, 2},
      {"ScrollSpeed.y", "ScrollSpeed.y", "Float", true, -0.23333332f, -4.0f, 4.0f, Widget::Vec, {}, true, 1},
      // ScrollProbability (Vec2, TiXL default (0, 0.5))
      {"ScrollProbability.x", "ScrollProbability", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"ScrollProbability.y", "ScrollProbability.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Padding (Vec2, TiXL default (0.02, 0.023333333))
      {"Padding.x", "Padding", "Float", true, 0.02f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Padding.y", "Padding.y", "Float", true, 0.023333333f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Seed (Single, TiXL default 0)
      {"Seed", "Seed", "Float", true, 0.0f, 0.0f, 100.0f, Widget::Slider},
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "RyojiPattern1", cookRyojiPattern1, "ryojipattern1", runRyojiPattern1SelfTest};

// --- RyojiPattern1 MATH golden ----------------------------------------------------------------
// Ground truth hand-derived from RyojiPattern1.hlsl (independent of the port; see /tmp trace).
//
// Golden config (deterministic plateau, NOT default node — chosen to collapse the recursion to a
// single subdivision so the cel grid is exactly 4x3 and the per-cel grayscale is closed-form):
//   Iterations=1  -> steps = min(1,10) = 1; the for-loop (i=1..steps) does NOT run, so ONLY the
//                    first subDivideCel(cel,(1,1)) executes.
//   Seed=0, BeatTime=0, Contrast=0, Padding=(0,0), Background=(0,0,0,1), Foreground=(1,1,1,1),
//   MixOriginal=0, HighlightProbability=0 (no highlight overwrite), Splits=(4,3).
//
//   First subDivideCel(cel=(0,0,1,1), splitProb=(1,1)): hash=hash22((0.1,0)) in [0,1) so
//   hash.x>1 && hash.y>1 is FALSE -> subdivide. hash.x<1 && hash.y<1 both TRUE -> cel.zw /= (4,3)
//   = (0.25, 0.3333). cel.xy snaps to the cell containing P. So the image is a 4x3 cel grid; every
//   pixel inside one cel reads the SAME hashForCel -> SAME grayscale, and Contrast=0 makes
//   gray = hashForCel directly (mix(hashForCel, ., 0) = hashForCel).
//   color = mix(Background=0, mix(Foreground=1, original, 0)=1, gray) = (gray,gray,gray, 1).
//
//   Hand-derived (Python float64 trace, see report):
//     cel (x in [0.25,0.5), y in [0,0.333)): hashForCel = 0.78058 -> u8 = round(0.78058*255) = 199
//     cel (x in [0.5,0.75),  y in [0,0.333)): hashForCel = 0.86235 -> u8 = 220
//
//   Pins (256x256, output pixel (x,y) -> texCoord ((x+0.5)/256,(y+0.5)/256)):
//     A=(70,30), B=(120,80), C=(70,80): all in cel (0.25,0.0) -> all u8 ~199, ALL EQUAL.
//     D=(160,30): in cel (0.5,0.0) -> u8 ~220, DIFFERENT from A.
//
// Asserts (teeth): (1) BLOCK UNIFORMITY: A==B==C exactly (same cel -> identical output; structural,
//   independent of float32-vs-float64). (2) CEL VALUE: A ~ 199 (+/-3 for GPU float32 hashing).
//   (3) CEL DIFFERENCE: |A - D| > 8 (neighbor cels differ -> the grid is real, not flat).
// injectBug: Seed=5.0 -> every cel's hashForCel changes (hash12 input cel.xy + (Seed+0.1, cel.w)),
//   so cel (0.25,0.0) hashes to 0.024 -> u8 6, NOT 199. Block uniformity still holds (same cel), but
//   the CLOSED-FORM value pin (A ~ 199) BREAKS -> valuePin assertion FAILS (teeth). This proves the
//   golden nails the per-cel hash formula, not merely the grid structure.
int runRyojiPattern1SelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-ryojipattern1] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(td);

  // Deterministic-plateau config (see golden header). injectBug raises Iterations to break the
  // single-subdivision cel plateau.
  std::map<std::string, float> params;
  params["Iterations"]           = 1.0f;  // single subdivision -> exact 4x3 cel plateau
  params["Seed"]                 = injectBug ? 5.0f : 0.0f;  // bug: shifts hashForCel -> value pin breaks
  params["BeatTime"]             = 0.0f;
  params["Contrast"]             = 0.0f;  // gray = hashForCel directly
  params["ForegroundRatio"]      = 0.5f;
  params["Padding.x"]            = 0.0f;  // no border (so probes never hit Background)
  params["Padding.y"]            = 0.0f;
  params["HighlightProbability"] = 0.0f;  // no highlight overwrite
  params["Background.r"] = 0.0f; params["Background.g"] = 0.0f;
  params["Background.b"] = 0.0f; params["Background.a"] = 1.0f;
  params["Foreground.r"] = 1.0f; params["Foreground.g"] = 1.0f;
  params["Foreground.b"] = 1.0f; params["Foreground.a"] = 1.0f;
  params["MixOriginal"]  = 0.0f;
  params["Splits.x"] = 4.0f; params["Splits.y"] = 3.0f;
  params["SplitProbability.x"] = 0.0f; params["SplitProbability.y"] = 0.27666667f;
  params["ScrollSpeed.x"] = 0.0f; params["ScrollSpeed.y"] = -0.23333332f;
  params["ScrollProbability.x"] = 0.0f; params["ScrollProbability.y"] = 0.5f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1;
  c.inputTexture = nullptr;  // Image unwired (MixOriginal=0 -> no-op); cook binds 1x1 dummy.
  c.output = dst;
  c.params = &params;
  cookRyojiPattern1(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  auto px = [&](uint32_t x, uint32_t y, int ch) -> int {
    return (int)out[((size_t)y * W + x) * 4 + ch];
  };

  // A,B,C in cel (0.25,0.0); D in cel (0.5,0.0). Read R (R=G=B for grayscale; verify channels too).
  int aR = px(70, 30, 0), aG = px(70, 30, 1), aB = px(70, 30, 2);
  int bR = px(120, 80, 0);
  int cR = px(70, 80, 0);
  int dR = px(160, 30, 0);

  // (1) BLOCK UNIFORMITY: same cel -> identical output (exact; structural). +/-1 GPU slack.
  bool uniform = (std::abs(aR - bR) <= 1) && (std::abs(aR - cR) <= 1);
  // (1b) grayscale: R==G==B.
  bool grayscale = (std::abs(aR - aG) <= 1) && (std::abs(aR - aB) <= 1);
  // (2) CEL VALUE pin: hashForCel=0.78058 -> u8 199 (+/-3 for float32 hashing).
  bool valuePin = std::abs(aR - 199) <= 3;
  // (3) CEL DIFFERENCE: neighbor cel (D, u8~220) differs from A -> grid is real.
  bool celDiffers = std::abs(aR - dR) > 8;

  bool pass = uniform && grayscale && valuePin && celDiffers;
  printf("[selftest-ryojipattern1] A(70,30)=%d(G=%d,B=%d) B(120,80)=%d C(70,80)=%d "
         "D(160,30)=%d -> uniform=%d gray=%d valuePin(~199)=%d celDiff=%d -> %s\n",
         aR, aG, aB, bR, cR, dR,
         uniform ? 1 : 0, grayscale ? 1 : 0, valuePin ? 1 : 0, celDiffers ? 1 : 0,
         pass ? "PASS" : "FAIL");

  dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
