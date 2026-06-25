// MandelbrotFractal image generator op (Phase C leaf; Bucket A pure procedural generator).
//
// TiXL authority: external/tixl/Operators/Lib/image/generate/fractal/MandelbrotFractal.{cs,hlsl,t3}.
//   .cs   — slot declarations: Phase, Scale, Offset(Vec2), ColorScale, Gradient.
//   .t3   — defaults (Scale=-0.5, ColorScale=10.0, Phase=0.0, Offset=(0.251,0)); the op rides
//           _ImageFxShaderSetupStatic (Wrap=Wrap, OutputFormat=R16G16B16A16_Float). The Gradient
//           input (default black->white 2-stop Linear) feeds a GradientsToTexture child whose row
//           is bound to the shader's GradientImage (t0). AspectRatio is host-computed (RequestedRes).
//   .hlsl — psMain + mandelbrot() (ported VERBATIM to shaders/mandelbrotfractal.metal).
//
// Port class: a .t3 compound whose terminal is _ImageFxShaderSetupStatic -> a single fragment shader
//   (mandelbrotfractal_vs/mandelbrotfractal_fs). Like LinearGradient, this is a RENDER op (NOT compute)
//   that binds a rasterized gradient ROW and samples it. The precedent cloned is
//   point_ops_lineargradient.cpp (the Gradient->t0 binding seam) + point_ops_valueraster.cpp (the
//   1x1-dummy generator convention, here unused since Mandelbrot has NO Image input).
//
// ★Difference from LinearGradient: MandelbrotFractal has NO optional Image input. The ONLY texture is
//   the GradientImage row at t0 (LinearGradient binds Image at t0 and Gradient at t1). So this op binds
//   the gradient row at texture(0) and there is no dummy-Image / blend / IsTextureValid logic.
//
// ★Unwired-Gradient fallback (kDefaultMandelbrotGradient) — traced from MandelbrotFractal.t3 :17-46:
//   the op's OWN Gradient input default is a 2-stop Linear black(0,0,0,1)->white(1,1,1,1). The .t3
//   wires that op Gradient slot into the GradientsToTexture child, so an unwired Gradient feeds the op
//   slot default. We mirror it. [fork-gradient-default-traced]
//
// FORKS (named): gradient-row format RGBA32F (gradient_raster.h fork-grad-row-format-32f);
//   pow(10,Scale)/SampleLevel forks documented in mandelbrotfractal.metal.
//
// Self-contained leaf: cookMandelbrotFractal + ImageFilterOp self-registration +
//   runMandelbrotFractalSelfTest (declared + defined in THIS file). CMake point_ops*.cpp glob +
//   shaders/*.metal glob auto-pick both files — no CMake edit.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/gradient_raster.h"            // rasterizeGradientRow, kGradientRowN
#include "runtime/image_filter_op_registry.h"   // ImageFilterOp self-registration
#include "runtime/mandelbrotfractal_params.h"   // MandelbrotFractalParams/Resolution, MANDELBROTFRACTAL_*
#include "runtime/point_graph.h"                // TexCookCtx, cookParam, registerTexOp
#include "runtime/sw_gradient.h"                // SwGradient (the consumed currency)
#include "runtime/tex_op_cache.h"              // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

int runMandelbrotFractalSelfTest(bool injectBug);

namespace {

// The unwired-Gradient fallback: the op's Gradient SLOT default (MandelbrotFractal.t3 :17-46), which
// the .t3 connection feeds into the GradientsToTexture child. Black->white, 2-stop Linear.
// [fork-gradient-default-traced]
SwGradient defaultMandelbrotGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(0.0f, 0.0f, 0.0f, 1.0f)});  // t3:21-30
  g.steps.push_back({1.0f, simd::make_float4(1.0f, 1.0f, 1.0f, 1.0f)});  // t3:33-42
  return g;
}

// MandelbrotFractal texture op: single fullscreen pass. Reads c.inputGradients[0] (the gathered
// Gradient), rasterizes it to a 1x512 row, binds it at t0, samples it at (f, 0.5). NO Image input.
void cookMandelbrotFractal(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "mandelbrotfractal_vs", "mandelbrotfractal_fs", fmt);
  if (!rps) return;

  // texSampler: linear + Repeat (Wrap). The single shared s0 sampler in TiXL's
  // _ImageFxShaderSetupStatic is driven by the op's Wrap slot, and MandelbrotFractal.t3 sets
  // Wrap="Wrap" (Filter stays at the setup default MinMagMipLinear). The shader samples the gradient
  // row at u = f = mandelbrot()/ColorScale + Phase, which routinely exceeds 1.0 for escaped pixels, so
  // U MUST repeat (frac) to band — NOT clamp (flat-white past f=1). [fix-sampler-wrap-parity]
  // V: gradient row is 1px tall so v=0.5 is mid-row regardless, but match TiXL's uniform Wrap faithfully.
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeRepeat);
  sd->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // --- b0 params (MandelbrotFractal.cs/.t3 defaults; routing trace in mandelbrotfractal_params.h) ---
  MandelbrotFractalParams p{};
  p.OffsetX    = cookParam(c, "Offset.x", 0.251f);  // t3 default (0.251, 0)
  p.OffsetY    = cookParam(c, "Offset.y", 0.0f);
  p.Scale      = cookParam(c, "Scale", -0.5f);      // t3 default -0.5
  p.ColorScale = cookParam(c, "ColorScale", 10.0f); // t3 default 10.0
  p.ColorPhase = cookParam(c, "Phase", 0.0f);       // <- Phase input; t3 default 0.0
  p.AspectRatio = (float)c.output->width() / (float)c.output->height();  // RequestedResolution child

  MandelbrotFractalResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // Pull the gradient (gathered input, or the traced black->white fallback when unwired).
  const SwGradient& g = (c.inputGradients && !c.inputGradients->empty())
                            ? (*c.inputGradients)[0]
                            : defaultMandelbrotGradient();
  MTL::Texture* gradTex = rasterizeGradientRow(c.dev, g, kGradientRowN);  // owned; release after draw
  if (!gradTex) { samp->release(); return; }

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(gradTex, 0);                              // t0 GradientImage row
  enc->setFragmentSamplerState(samp, 0);                          // s0 texSampler (Repeat/Wrap)
  enc->setFragmentBytes(&p,   sizeof(MandelbrotFractalParams),     MANDELBROTFRACTAL_Params);
  enc->setFragmentBytes(&res, sizeof(MandelbrotFractalResolution), MANDELBROTFRACTAL_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();
  gradTex->release();
}

}  // namespace

// Self-registration. File-scope static feeds imageFilterSpecSink() + texReg() + imageFilterSelfTests()
// during pre-main dynamic init. No shared file edited (point_ops*.cpp glob picks this up).
static const ImageFilterOp _reg_mandelbrotfractal{
    // MandelbrotFractal (TiXL Lib.image.generate.fractal.MandelbrotFractal): smooth-iteration
    // Mandelbrot-set generator, colored by a Gradient (the t0 binding). No Image input.
    {"MandelbrotFractal", "MandelbrotFractal",
     {// Gradient input (the t0 binding). Unwired -> traced black->white fallback.
      {"Gradient", "Gradient", "Gradient", true},
      {"out", "out", "Texture2D", false},
      // Phase (Single, TiXL t3 default 0.0; -> shader ColorPhase, added to normalized iteration count)
      {"Phase", "Phase", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Slider},
      // Scale (Single, TiXL t3 default -0.5; p /= pow(10, Scale) — zoom decade)
      {"Scale", "Scale", "Float", true, -0.5f, -4.0f, 8.0f, Widget::Slider},
      // Offset (Vec2, TiXL t3 default (0.251, 0.0) — complex-plane center)
      {"Offset.x", "Offset", "Float", true, 0.251f, -4.0f, 4.0f, Widget::Vec, {}, true, 2},
      {"Offset.y", "Offset.y", "Float", true, 0.0f, -4.0f, 4.0f, Widget::Vec, {}, true, 1},
      // ColorScale (Single, TiXL t3 default 10.0; f / ColorScale — gradient stretch)
      {"ColorScale", "ColorScale", "Float", true, 10.0f, 0.0001f, 100.0f, Widget::Slider},
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "MandelbrotFractal", cookMandelbrotFractal, "mandelbrotfractal", runMandelbrotFractalSelfTest};

// --- MandelbrotFractal MATH golden ----------------------------------------------------------------
// Configuration: default params EXCEPT Offset=(0,0) so the CENTER pixel maps to complex c=(0,0), the
//   deepest interior of the set (forced for a deterministic golden — the t3 default (0.251,0) sits on
//   the cardioid boundary, an unstable golden probe). Gradient = default black->white linear.
//
// ASSERTION A (DEEP INTERIOR — DETERMINISTIC): center pixel (128,128) of a 256x256 render, Phase=0.1.
//   uv=(0.5,0.5) -> p=(0,0) -> p.y*=-1 -> (0,0) -> p.x*=aspect(=1) -> (0,0) -> p/=pow(10,-0.5) -> (0,0)
//   -> p+=Offset(0,0) -> c=(0,0). mandelbrot((0,0)): cardioid test 256*0 - 96*0 + 32*0 - 3 = -3 < 0
//   -> INSIDE M1 -> returns 0.0. f = 0/10 + Phase = 0.1. Gradient row sampled at u=0.1.
//   ★Phase=0.1 (not 0): with the CORRECT Repeat sampler, u=0.0 lands ON the wrap SEAM and linearly
//   blends texel 511 (white) with texel 0 (black) -> grey (NOT black). That seam-blend is faithful TiXL
//   (TiXL's s0 is Wrap too), so the old "interior reads black at u=0" premise was a Clamp-only artifact.
//   Probing at u=0.1 sits safely mid-row (texel coord 0.1*512-0.5 = 50.7, no wrap): row value
//   ~ 50.7/511 = 0.0992 -> dark grey, RGBA8 ~ (25,25,25,255). EXACT (interior is a flat M1 plateau).
//   injectBug: swaps the gradient endpoints (white->black, texel i = 1 - i/511) so u=0.1 reads
//   ~ 1-0.0992 = 0.9008 -> light grey ~ (230,230,230,255) -> the bug RED-flips dark<->light.
//
// ASSERTION B (FAR ESCAPE — DETERMINISTIC): corner pixel (0,0) of the render.
//   uv=(0,0) -> p=(0,0) -> p-=0.5 -> (-0.5,-0.5) -> p.y*=-1 -> (-0.5,0.5) -> p.x*=aspect(=1) ->
//   (-0.5,0.5) -> p/=pow(10,-0.5)=0.31623 -> (-1.5811, 1.5811) -> p+=Offset(0,0) -> c=(-1.5811,1.5811).
//   |c|^2 = 2*1.5811^2 = 5.0 >> 4 -> escapes on the FIRST iteration (z1 = c, dot(z1,z1)=5.0 < B*B=65536,
//   continues; but |c| this large escapes within a handful of iterations -> small l). The KEY golden
//   fact we assert: this pixel is OUTSIDE the set, so its iteration count l is SMALL (not the
//   max-iteration interior value). With f = l/10 + 0 and l small (a few), f is a small positive number;
//   the gradient maps small f to NEAR-BLACK (dark grey), strictly DARKER than full white. We assert
//   this corner is NOT the interior-black AND NOT full-white: a mid/low grey strictly between, proving
//   the escape path ran (distinguishes "generator produced a real Mandelbrot field" from "all black").
//   Concretely we assert corner.R is in (0, 255) — escaped-but-not-interior. (We avoid pinning an exact
//   value to the GPU's log2(log2()) smooth-count LSBs; the load-bearing parity claim is the structural
//   interior-black vs escaped-grey contrast.)
//
// ASSERTION C (SAMPLER WRAP MODE — BITES THE Repeat-vs-Clamp BUG): a SECOND render, Offset=(0,0) AND
//   ColorPhase=1.5. Center pixel c=(0,0) -> mandelbrot()=0.0 (M1 early-out) -> f = 0/ColorScale + 1.5
//   = 1.5 DETERMINISTICALLY. The shader samples the gradient row at u = f = 1.5.
//     • Under the CORRECT Repeat (Wrap) sampler: u wraps -> frac(1.5) = 0.5 -> the 512-texel black->white
//       row is sampled at its midpoint (texel coord 0.5*512-0.5 = 255.5 -> lerp(255/511, 256/511, 0.5)
//       = 255.5/511 = 0.5 exactly) -> mid-grey ~ byte 128.
//     • Under the WRONG ClampToEdge sampler: u clamps to 1.0 -> last texel = white -> byte 255.
//   We assert the center is mid-grey (~128, NOT 255). This case PASSES ONLY with the Repeat sampler and
//   FAILS (reads 255) under Clamp — the tooth that bites the parity bug the refuter found.
//   [golden-sampler-wrap-tooth]  (injectBug is irrelevant to C; we run C with the correct gradient.)
int runMandelbrotFractalSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-mandelbrotfractal] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(td);

  // Default params except Offset=(0,0) so center -> c=(0,0) (deep interior, deterministic).
  std::map<std::string, float> params;
  params["Offset.x"]   = 0.0f;
  params["Offset.y"]   = 0.0f;
  params["Scale"]      = -0.5f;
  params["ColorScale"] = 10.0f;
  params["Phase"]      = 0.1f;  // interior f=0.1 -> u=0.1, OFF the u=0 wrap seam (see ASSERTION A)

  // injectBug: swap the gradient endpoints (white->black) -> interior reads WHITE instead of BLACK.
  std::vector<SwGradient> grads;
  {
    SwGradient g;
    g.interpolation = kGradientLinear;
    if (injectBug) {
      g.steps.push_back({0.0f, simd::make_float4(1.0f, 1.0f, 1.0f, 1.0f)});  // BUG: white at t=0
      g.steps.push_back({1.0f, simd::make_float4(0.0f, 0.0f, 0.0f, 1.0f)});
    } else {
      g.steps.push_back({0.0f, simd::make_float4(0.0f, 0.0f, 0.0f, 1.0f)});  // black at t=0
      g.steps.push_back({1.0f, simd::make_float4(1.0f, 1.0f, 1.0f, 1.0f)});
    }
    grads.push_back(g);
  }

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1;
  c.inputTexture = nullptr;       // NO Image input (Mandelbrot is a pure generator + gradient)
  c.inputGradients = &grads;      // Gradient wired (the t0 binding)
  c.output = dst;
  c.params = &params;
  cookMandelbrotFractal(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // Assertion A: center pixel (128,128) -> c=(0,0) -> interior -> f=0.1 -> gradient u=0.1 -> dark grey
  // ~25 (correct black->white gradient). injectBug swaps endpoints -> light grey ~230. (u=0.1 is OFF the
  // u=0 wrap seam, so this is a clean mid-row sample under the correct Repeat sampler.)
  const uint32_t cx = W / 2, cy = H / 2;
  size_t ci = ((size_t)cy * W + cx) * 4;
  int cR = out[ci+0], cG = out[ci+1], cB = out[ci+2], cA = out[ci+3];
  // Correct gradient: dark grey ~25 (±3). injectBug: light grey ~230 -> A FAILS, proving A bites.
  bool interiorDark = (cR >= 22 && cR <= 28 && cG >= 22 && cG <= 28 && cB >= 22 && cB <= 28 &&
                       cA >= 253);

  // Assertion B: corner (0,0) -> c=(-1.581,1.581) -> escapes -> small l -> dark grey, strictly between
  // interior-black and full-white. Proves the Mandelbrot escape field actually ran (not all-black).
  size_t qi = 0;  // pixel (0,0)
  int qR = out[qi+0];
  bool escapedGrey = (qR > 2 && qR < 253);

  // Assertion C: SECOND render with ColorPhase=1.5 -> center f=1.5 -> Repeat sampler frac(1.5)=0.5 ->
  // mid-grey ~128; Clamp would read 255 (white). Bites the sampler-wrap parity bug. Use the CORRECT
  // (non-bug) black->white gradient regardless of injectBug — C probes the sampler, not the gradient.
  SwGradient gc;
  gc.interpolation = kGradientLinear;
  gc.steps.push_back({0.0f, simd::make_float4(0.0f, 0.0f, 0.0f, 1.0f)});
  gc.steps.push_back({1.0f, simd::make_float4(1.0f, 1.0f, 1.0f, 1.0f)});
  std::vector<SwGradient> gradsC{gc};

  std::map<std::string, float> paramsC = params;
  paramsC["Phase"] = 1.5f;  // f = mandelbrot(0,0)/ColorScale + 1.5 = 1.5 at center

  MTL::Texture* dstC = dev->newTexture(td);
  TexCookCtx cc;
  cc.dev = dev; cc.lib = lib; cc.queue = q;
  cc.nodeId = 1;
  cc.inputTexture = nullptr;
  cc.inputGradients = &gradsC;
  cc.output = dstC;
  cc.params = &paramsC;
  cookMandelbrotFractal(cc);

  std::vector<uint8_t> outC((size_t)W * H * 4, 0);
  dstC->getBytes(outC.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  size_t cci = ((size_t)cy * W + cx) * 4;
  int wR = outC[cci+0], wG = outC[cci+1], wB = outC[cci+2], wA = outC[cci+3];
  // Repeat(correct): frac(1.5)=0.5 -> row midpoint 0.5 -> ~128. Clamp(wrong): u=1.0 -> white 255.
  // ±2 tolerance covers the round(127.5) LSB; the gap to the Clamp value (255) is ~127, far outside it.
  bool wrapMidGrey = (wR >= 126 && wR <= 130 && wG >= 126 && wG <= 130 && wB >= 126 && wB <= 130 &&
                      wA >= 253);

  printf("[selftest-mandelbrotfractal] A center(%u,%u) rgba=(%d,%d,%d,%d) interiorDark=%d "
         "[expect ~(25,25,25,255)] | B corner(0,0) R=%d escapedGrey=%d [expect 0<R<255] | "
         "C wrap center rgba=(%d,%d,%d,%d) wrapMidGrey=%d [expect ~(128,128,128,255), Clamp=255]\n",
         cx, cy, cR, cG, cB, cA, interiorDark ? 1 : 0, qR, escapedGrey ? 1 : 0,
         wR, wG, wB, wA, wrapMidGrey ? 1 : 0);

  bool pass = interiorDark && escapedGrey && wrapMidGrey;
  printf("[selftest-mandelbrotfractal] -> %s\n", pass ? "PASS" : "FAIL");

  dstC->release();
  dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
