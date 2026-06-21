// RemapColor image/color op — per-channel gradient color-remap. The Gradient→t1 binding seam's newest
// consumer, and the proving op for the GradientSteps row-resolution seam extension (gradient_raster.h).
//
// TiXL authority: external/tixl/Operators/Lib/image/color/RemapColor.{cs,t3} +
//   external/tixl/Operators/Lib/Assets/shaders/img/fx/ColorRemap.hlsl (psMain ported VERBATIM to
//   shaders/remapcolor.metal) + the embedded GradientsToTexture child (GradientSteps→its Resolution).
//
// Port class: a .t3 compound whose terminal is _multiImageFxSetupStatic → one fragment shader
//   (remapcolor_vs/remapcolor_fs). Like RadialGradient/BubbleZoom this is a RENDER op (NOT compute):
//   cachedTexPSO → renderCommandEncoder → setFragmentTexture/Sampler/Bytes → drawPrimitives triangle 3.
//   The precedent cloned is point_ops_radialgradient.cpp.
//
// The Gradient→t1 binding: the op reads its gathered Gradient input (c.inputGradients[0]), rasterizes it
// to a 1×GradientSteps RGBA row via rasterizeGradientRow(..., steps) (the SAME row sampler GradientsTo-
// Texture uses — gradient_raster.h, can't drift), and binds it at fragment texture(1) with clampedSampler.
//
// ★★ Cut55 ROUTING TRACE (adversarial, NOT 1:1) — see remapcolor_params.h for the full table. The two
//   non-trivial routings replicated as scalar expressions in cookRemapColor below:
//   • [fork-offset-from-cycle]   cbuffer Offset ← the op's "Cycle" input (the .cs has NO Offset input).
//   • [fork-dontcoloralpha-bool] cbuffer DontColorAlpha ← bool input via BoolToFloat (1.0/0.0).
//   • GradientSteps → the rasterized row's texel count (GradientsToTexture.Resolution), NOT a b0 field.
//
// ★Unwired-Gradient fallback (defaultRemapColorGradient) — TRACED from RemapColor.t3's Gradient SLOT
//   default (c45d487b): ~black(0,~0,1e-6,1)→~white(1,0.99999,1,1), 2-stop Linear. (The child's embedded
//   magenta→blue GradientsToTexture default is OVERRIDDEN by the op's Gradient slot connection into the
//   child's Gradients input — same routing precedent as RadialGradient.) [fork-gradient-default-traced]
//
// FORKS (named): generator dummy (1×1 transparent-black ImageA when Image unwired); gradient-row format
//   RGBA32F (gradient_raster.h fork-grad-row-format-32f); gain/bias float4 overload inlined in
//   remapcolor.metal; fork-offset-from-cycle + fork-dontcoloralpha-bool + fork-gradient-default-traced +
//   fork-gradientsteps-is-resolution (gradient_raster.h).
//
// Self-contained leaf: cookRemapColor + ImageFilterOp self-registration + runRemapColorSelfTest (IMPL
//   IN THIS FILE via the imageFilterSelfTests() sink through the registrar — NO selftests.cpp /
//   gradient_golden.cpp / CMake edit). CMake point_ops*.cpp + shaders/*.metal globs auto-pick both files.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"            // SymbolLibrary (resident cook input)
#include "runtime/eval_context.h"
#include "runtime/gradient_raster.h"            // rasterizeGradientRow, sampleGradientRowRGBA, kGradientRowN
#include "runtime/graph.h"                      // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"               // libFromGraph (flat Graph -> SymbolLibrary)
#include "runtime/image_filter_op_registry.h"   // ImageFilterOp self-registration
#include "runtime/point_graph.h"                // TexCookCtx, cookParam, PointGraph::cook/cookResident
#include "runtime/remapcolor_params.h"          // RemapColorParams, REMAPCOLOR_*
#include "runtime/resident_eval_graph.h"        // ResidentEvalGraph / buildEvalGraph
#include "runtime/sw_gradient.h"                // SwGradient (the consumed currency)
#include "runtime/tex_op_cache.h"               // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// sw-scope (EXTERNAL linkage) — defined in gradient_op_registry.cpp. MUST be declared OUTSIDE the
// anonymous namespace, else internal linkage won't resolve the real symbol (corrupts the REAL
// DefineGradient cook in the RED bite).
bool& gradientInjectBug();

namespace {

// Unwired-Gradient fallback: RemapColor.t3 Gradient SLOT default (c45d487b) — ~black→~white, 2-stop
// Linear. [fork-gradient-default-traced]
SwGradient defaultRemapColorGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(0.0f, 1.2159347e-11f, 1e-06f, 1.0f)});  // t3 stop0 ≈black
  g.steps.push_back({1.0f, simd::make_float4(1.0f, 0.99999f, 1.0f, 1.0f)});          // t3 stop1 ≈white
  return g;
}

// 1×1 transparent-black dummy for the no-Image case (FX → bound to dummy when unwired). Same convention
// as cookRadialGradient's makeDummyTex — the shader always gets a valid ImageA handle.
MTL::Texture* makeDummyTex(MTL::Device* dev) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  const uint8_t px[4] = {0, 0, 0, 0};
  t->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, px, 4);
  return t;
}

// RemapColor texture op: single fullscreen pass. Reads c.inputTexture (Image) + c.inputGradients[0],
// rasterizes the gradient to a 1×GradientSteps row, looks up each channel value in the shader.
void cookRemapColor(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "remapcolor_vs", "remapcolor_fs", fmt);
  if (!rps) return;

  // s0 linearSampler: linear+Clamp (ImageA), matching RemapColor.t3 _multiImageFxSetupStatic WrapMode=Clamp.
  MTL::SamplerDescriptor* sd0 = MTL::SamplerDescriptor::alloc()->init();
  sd0->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd0->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd0->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd0->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp0 = c.dev->newSamplerState(sd0);
  sd0->release();

  // ★s1 clampedSampler: linear+ClampToEdge (the gradient row). MANDATORY — sampled at v=0 with the
  // value at u=channel; a Wrap sampler would corrupt the u/v edges.
  MTL::SamplerDescriptor* sd1 = MTL::SamplerDescriptor::alloc()->init();
  sd1->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd1->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd1->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd1->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp1 = c.dev->newSamplerState(sd1);
  sd1->release();

  // --- b0 params (ColorRemap.hlsl order; RemapColor.cs/.t3 defaults + Cut55 routing) ---
  RemapColorParams p{};
  // [fork-dontcoloralpha-bool] bool input → BoolToFloat (1.0/0.0). .t3 default false.
  p.DontColorAlpha = (cookParam(c, "DontColorAlpha", 0.0f) > 0.5f) ? 1.0f : 0.0f;
  p.Mode     = cookParam(c, "Mode", 1.0f);     // .t3 default 1 = IndividualChannels (IntToFloat)
  // [fork-offset-from-cycle] cbuffer Offset ← the op's "Cycle" input. .t3 Cycle default 0.0.
  p.Offset   = cookParam(c, "Cycle", 0.0f);
  p.Exposure = cookParam(c, "Exposure", 1.0f);
  p.GainAndBiasX = cookParam(c, "GainAndBias.x", 0.5f);
  p.GainAndBiasY = cookParam(c, "GainAndBias.y", 0.5f);
  p.Repeat   = cookParam(c, "Repeat", 1.0f);   // .t3 default 1.0

  // GradientSteps → the rasterized row's texel count (GradientsToTexture.Resolution). .t3 default 256.
  // [fork-gradientsteps-is-resolution]
  const int gradientSteps = (int)cookParam(c, "GradientSteps", 256.0f);

  // Pull the gradient (gathered input, or the traced black→white fallback when unwired).
  const SwGradient& g = (c.inputGradients && !c.inputGradients->empty())
                            ? (*c.inputGradients)[0]
                            : defaultRemapColorGradient();
  MTL::Texture* gradTex = rasterizeGradientRow(c.dev, g, kGradientRowN, gradientSteps);  // owned
  if (!gradTex) { samp0->release(); samp1->release(); return; }

  // Bind ImageA (or 1×1 transparent-black dummy when no upstream Image). [generator-dummy]
  MTL::Texture* dummyTex = nullptr;
  const MTL::Texture* imageTex = c.inputTexture;
  if (!imageTex) { dummyTex = makeDummyTex(c.dev); imageTex = dummyTex; }

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(imageTex), 0);  // t0 ImageA
  enc->setFragmentTexture(gradTex, 1);                              // t1 Gradient row
  enc->setFragmentSamplerState(samp0, 0);                          // s0 linearSampler (Clamp)
  enc->setFragmentSamplerState(samp1, 1);                          // s1 clampedSampler (ClampToEdge)
  enc->setFragmentBytes(&p, sizeof(RemapColorParams), REMAPCOLOR_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp0->release();
  samp1->release();
  gradTex->release();
  if (dummyTex) dummyTex->release();
}

// ======================== --selftest-remapcolor: CheckerBoard → RemapColor (Gradient→t1) ========================
// CLOSED-FORM pixel golden for the Gradient→t1 binding on the RemapColor consumer.
//
// Setup: a CheckerBoard generator with ColorA == ColorB == a known CONSTANT color (uniform input image,
// no UV-boundary fragility) feeds RemapColor's Image (t0). A DefineGradient set to a NON-DEFAULT
// RED→GREEN gradient feeds RemapColor's Gradient (t1, the binding under test).
//
// Output per pixel (Mode=1 IndividualChannels, the .t3 default; Exposure=1, GainAndBias=(0.5,0.5)=identity
// for values in [0.001,0.999], Repeat=1, Offset/Cycle=0):
//   out.r = rowLinear(grad, steps, inR).r ; out.g = rowLinear(grad, steps, inG).g ; out.b = ...b ; out.a = ...a
// where rowLinear() is the EXACT host replication of the GPU's linear-clamped sampling of the rasterized
// N=steps-texel gradient row (same texel-center math the GPU does). steps=0 → row width = kGradientRowN
// (512, smooth, byte-identical to all existing gradient consumers).
//
// ★R-2 HARDENING (why RED→GREEN, not black→white): if the resident Gradient wire were cut, the cook
// falls to defaultRemapColorGradient() (black→white = GRAY at every t, R==G==B). With RED→GREEN the
// per-channel lookup gives distinct R vs G, so a cut wire → gray → the green-dominance teeth-guard bites.
//
// ★GradientSteps SEAM teeth-guard (CASE 2): a second golden with a SMALL GradientSteps (=3) — the
// quantized 3-texel row is sampled and the output asserted against rowLinear(grad, 3, value). With
// steps=3 the band structure differs sharply from the smooth steps=0 case, so a broken seam param (e.g.
// ignoring steps → smooth row) DIVERGES from the steps=3 reference. Distinctness vs the smooth case is
// also asserted (steps actually changed the output), so the new param is teeth-guarded.
//
// RED bite: gradientInjectBug() corrupts the REAL DefineGradient cook (drops a stop) so the rasterized
// row diverges → pins diverge from the UN-corrupted host red→green reference (no co-condition tautology).
// Run on BOTH flat (PointGraph::cook) AND resident (cookResident) — R-2 iron rule.

bool nearf(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
bool near4(simd::float4 a, simd::float4 b, float eps = 1e-3f) {
  return nearf(a.x, b.x, eps) && nearf(a.y, b.y, eps) && nearf(a.z, b.z, eps) && nearf(a.w, b.w, eps);
}

// The non-default wired gradient: RED (1,0,0,1) @0 → GREEN (0,1,0,1) @1, Linear. Host reference
// sample(t) = (1-t, t, 0, 1). The resident-wire teeth-guard: distinct from the black→white fallback
// (gray) in the R/G channels.
SwGradient redGreenGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1.0f, 0.0f, 0.0f, 1.0f)});  // red
  g.steps.push_back({1.0f, simd::make_float4(0.0f, 1.0f, 0.0f, 1.0f)});  // green
  return g;
}

// EXACT host replication of the GPU sampling the rasterized N-texel gradient row with a linear+
// ClampToEdge sampler at texcoord u (v=0). Texel center i sits at u=(i+0.5)/N (Metal convention); a
// sample at u maps to f = u*N - 0.5; lerp the two bracketing texels (clamped to [0,N-1]) by frac(f).
// steps=0 → N = kGradientRowN (the smooth row), matching cookRemapColor's rasterizeGradientRow call.
simd::float4 rowLinear(const SwGradient& g, int steps, float u) {
  const int N = (steps > 0) ? std::min(std::max(steps, 1), 16384) : kGradientRowN;
  if (N == 1) return g.sample(0.0f);                  // single texel → 0/0 t collapses to sample(0)
  u = std::min(std::max(u, 0.0f), 1.0f);              // clampedSampler clamps texcoord to [0,1]
  float f = u * (float)N - 0.5f;
  int i0 = (int)std::floor(f);
  float frac = f - (float)i0;
  int i1 = i0 + 1;
  i0 = std::min(std::max(i0, 0), N - 1);
  i1 = std::min(std::max(i1, 0), N - 1);
  auto texel = [&](int i) { return g.sample((float)i / (N - 1.0f)); };  // row texel i = sample(i/(N-1))
  simd::float4 a = texel(i0), b = texel(i1);
  return simd::make_float4(a.x + (b.x - a.x) * frac, a.y + (b.y - a.y) * frac,
                           a.z + (b.z - a.z) * frac, a.w + (b.w - a.w) * frac);
}

// ApplyGainAndBias(0.5,0.5) is the identity for v in [0.001,0.999] (g==b==0.5 → GetBias(0.5,x)=x,
// GetSchlickBias(0.5,x)=x). The golden's input channels are chosen inside that band, so the host
// reference = rowLinear(grad, steps, channelValue + offset).{channel} with channelValue = input * Repeat.

// The uniform input image color (CheckerBoard ColorA==ColorB). Channels chosen distinct + inside the
// gain/bias identity band so each per-channel gradient lookup lands at a different, hand-derivable t.
constexpr float kInR = 0.25f, kInG = 0.60f, kInB = 0.85f, kInA = 1.0f;

// Build: node 30 = RemapColor; node 20 = CheckerBoard (uniform: ColorA==ColorB==(kInR,kInG,kInB,kInA));
// node 1 = DefineGradient RED→GREEN. CheckerBoard.out → RemapColor.Image; DefineGradient.out →
// RemapColor.Gradient. `steps` sets the GradientSteps param; mode sets Mode (1=individual, 0=gray).
void buildRemapColorGraph(Graph& g, int steps, float mode) {
  Node rc; rc.id = 30; rc.type = "RemapColor";
  rc.params["Mode"] = mode;
  rc.params["Cycle"] = 0.0f;            // → cbuffer Offset [fork-offset-from-cycle]
  rc.params["Exposure"] = 1.0f;
  rc.params["GainAndBias.x"] = 0.5f; rc.params["GainAndBias.y"] = 0.5f;
  rc.params["Repeat"] = 1.0f;
  rc.params["DontColorAlpha"] = 0.0f;
  rc.params["GradientSteps"] = (float)steps;  // 0 → smooth (kGradientRowN); >0 → stepped row
  rc.params["Resolution"] = 4.0f; rc.params["CustomW"] = 64.0f; rc.params["CustomH"] = 64.0f;  // 64×64
  g.nodes.push_back(rc);

  Node cb; cb.id = 20; cb.type = "CheckerBoard";
  // Uniform input: ColorA == ColorB == the known constant → every pixel identical (no UV fragility).
  cb.params["ColorA.r"] = kInR; cb.params["ColorA.g"] = kInG; cb.params["ColorA.b"] = kInB; cb.params["ColorA.a"] = kInA;
  cb.params["ColorB.r"] = kInR; cb.params["ColorB.g"] = kInG; cb.params["ColorB.b"] = kInB; cb.params["ColorB.a"] = kInA;
  cb.params["Resolution"] = 4.0f; cb.params["CustomW"] = 64.0f; cb.params["CustomH"] = 64.0f;
  g.nodes.push_back(cb);

  Node dg; dg.id = 1; dg.type = "DefineGradient";
  // ★NON-DEFAULT gradient (the R-2 teeth-guard): Color1 = red @0, Color2 = green @1.
  dg.params["Color1.x"] = 1.0f; dg.params["Color1.y"] = 0.0f; dg.params["Color1.z"] = 0.0f; dg.params["Color1.w"] = 1.0f;
  dg.params["Color1Pos"] = 0.0f;
  dg.params["Color2.x"] = 0.0f; dg.params["Color2.y"] = 1.0f; dg.params["Color2.z"] = 0.0f; dg.params["Color2.w"] = 1.0f;
  dg.params["Color2Pos"] = 1.0f;
  dg.params["Interpolation"] = 0.0f;  // Linear
  g.nodes.push_back(dg);

  // CheckerBoard out port: ports are out=0 then the param ports (no Image input). out = port 0.
  // RemapColor ports: Image=0, Gradient=1, out=2, ... → Image=0, Gradient=1.
  g.connections.push_back({800, pinId(20, /*CheckerBoard out*/ 0), pinId(30, /*Image*/ 0)});
  // DefineGradient out = port 21 (16 color comps + 4 pos + 1 interp = ports 0..20; out=21).
  g.connections.push_back({801, pinId(1, /*DefineGradient out*/ 21), pinId(30, /*Gradient*/ 1)});
}

// Per-pixel expected color for Mode=1 (IndividualChannels), GainAndBias identity, Exposure=Repeat=1,
// Offset=0: out.{r,g,b,a} = rowLinear(grad, steps, in.{r,g,b,a}).{r,g,b,a}.
simd::float4 expectIndividual(const SwGradient& ref, int steps) {
  return simd::make_float4(rowLinear(ref, steps, kInR).x, rowLinear(ref, steps, kInG).y,
                           rowLinear(ref, steps, kInB).z, rowLinear(ref, steps, kInA).w);
}
// Per-pixel expected for Mode=0 (UseGrayScale): gray = (r+g+b)/3; out = rowLinear(grad, steps, gray)
// (the whole float4 from the .r lookup texcoord, all channels). gradient.a then = .a of that sample.
simd::float4 expectGray(const SwGradient& ref, int steps) {
  float gray = (kInR + kInG + kInB) / 3.0f;
  // Mode<0.5: orgColor = ApplyGainAndBias(saturate(gray)) [identity] * Repeat = gray (broadcast). The
  // lookup uses orgColor.r (= gray) for the single Gradient.Sample; the whole float4 is returned.
  return rowLinear(ref, steps, gray);
}

// Read pixel (px,py) and assert it ≈ want. Two pins (uniform input → both identical; tests spatial
// invariance) + a green-dominance teeth-guard (R-2: proves the wire carries red→green, not gray fallback).
bool checkRcPixels(MTL::Texture* tex, simd::float4 want, const char* tag, bool individualMode) {
  if (!tex) { std::printf("[selftest-remapcolor] %s FAIL: null tex\n", tag); return false; }
  const uint32_t W = (uint32_t)tex->width(), H = (uint32_t)tex->height();
  if (W != 64 || H != 64) {
    std::printf("[selftest-remapcolor] %s FAIL: dims=%ux%u want 64x64\n", tag, W, H);
    return false;
  }
  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  auto rd = [&](int x, int y, int ch) { return px[((size_t)y * W + x) * 4 + ch] / 255.0f; };
  bool ok = true;
  // PIN A center, PIN B off-center — uniform input means both must equal `want` (spatial invariance +
  // ≥2 distinct pins).
  const int pins[2][2] = {{32, 32}, {12, 50}};
  for (int k = 0; k < 2; ++k) {
    int x = pins[k][0], y = pins[k][1];
    simd::float4 got = simd::make_float4(rd(x, y, 0), rd(x, y, 1), rd(x, y, 2), rd(x, y, 3));
    if (!near4(got, want, 0.012f)) {  // RGBA8 readback → ~3/255 tolerance
      std::printf("[selftest-remapcolor] %s pin%c (%d,%d) got=(%.3f,%.3f,%.3f,%.3f) "
                  "want=(%.3f,%.3f,%.3f,%.3f) FAIL\n",
                  tag, 'A' + k, x, y, got.x, got.y, got.z, got.w, want.x, want.y, want.z, want.w);
      ok = false;
    }
  }
  // ★Teeth-guard for the RED→GREEN wire (R-2). In IndividualChannels mode, out.r is sampled from the
  // gradient's R channel at a SMALL t (inR=0.25 → R≈0.75) and out.g from the G channel at a LARGER t
  // (inG=0.60 → G≈0.60), so R clearly EXCEEDS G — impossible under the gray (R==G) black→white fallback.
  // In grayscale mode, the single gray=0.567 → sample → R≈0.43, G≈0.57 → G>R; assert that instead.
  {
    float rCh = rd(pins[0][0], pins[0][1], 0), gCh = rd(pins[0][0], pins[0][1], 1);
    bool guard = individualMode ? (rCh > gCh + 0.1f) : (gCh > rCh + 0.05f);
    if (!guard) {
      std::printf("[selftest-remapcolor] %s color teeth-guard FAIL (R=%.3f G=%.3f, mode=%s) — gradient "
                  "looks gray (black→white fallback / cut wire?)\n",
                  tag, rCh, gCh, individualMode ? "individual" : "gray");
      ok = false;
    }
  }
  return ok;
}

bool cookAndCheckFlat(PointGraph& pg, const EvaluationContext& ctx, int steps, float mode,
                      const SwGradient& ref, bool injectBug, const char* tag) {
  Graph g;
  buildRemapColorGraph(g, steps, mode);
  gradientInjectBug() = injectBug;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/30);
  gradientInjectBug() = false;
  simd::float4 want = (mode < 0.5f) ? expectGray(ref, steps) : expectIndividual(ref, steps);
  return checkRcPixels(pg.target(), want, tag, mode >= 0.5f);
}

bool cookAndCheckResident(PointGraph& pg, const EvaluationContext& ctx, int steps, float mode,
                          const SwGradient& ref, const char* tag) {
  Graph g;
  buildRemapColorGraph(g, steps, mode);
  SymbolLibrary slib = libFromGraph(g);
  ResidentEvalGraph rg = buildEvalGraph(slib, "Root");
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"30");
  simd::float4 want = (mode < 0.5f) ? expectGray(ref, steps) : expectIndividual(ref, steps);
  return checkRcPixels(pg.target(), want, tag, mode >= 0.5f);
}

int runRemapColorSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-remapcolor] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  PointGraph pg(dev, lib, q, 64, 64);

  SwGradient ref = redGreenGradient();  // UN-corrupted host reference (bug corrupts the COOK, not this)

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  bool ok = true;

  // === CASE 1: smooth (GradientSteps=0 → kGradientRowN), Mode=1 IndividualChannels (.t3 default) ===
  // FLAT cook. injectBug corrupts the REAL DefineGradient cook → row diverges → pins diverge → exit 1.
  ok &= cookAndCheckFlat(pg, ctx, /*steps=*/0, /*mode=*/1.0f, ref, injectBug,
                         injectBug ? "case1-individual-smooth(bug)" : "case1-individual-smooth flat");

  if (!injectBug) {
    // RESIDENT (production) cook — proves the Gradient→t1 wire is LIVE on cookResident (R-2 rule).
    ok &= cookAndCheckResident(pg, ctx, /*steps=*/0, /*mode=*/1.0f, ref, "case1-individual-smooth resident");

    // === CASE 2: GradientSteps=3 (the new seam param teeth-guard), Mode=0 grayscale ===
    // The 3-texel quantized row sampled at gray=0.567 → bracketed by texels 1 & 2 of {sample(0),
    // sample(.5), sample(1)} → a value DISTINCT from the smooth steps=0 case. Asserts the seam param
    // actually changed the rasterized row.
    ok &= cookAndCheckFlat(pg, ctx, /*steps=*/3, /*mode=*/0.0f, ref, /*injectBug=*/false,
                           "case2-gradientsteps3-gray flat");
    ok &= cookAndCheckResident(pg, ctx, /*steps=*/3, /*mode=*/0.0f, ref,
                               "case2-gradientsteps3-gray resident");

    // Load-bearing: steps=3 output must DIFFER from steps=0 output at the same gray value (proves the
    // GradientSteps seam param is wired, not ignored). If they matched, the seam would be dead.
    {
      simd::float4 smooth = expectGray(ref, 0);
      simd::float4 stepped = expectGray(ref, 3);
      if (near4(smooth, stepped, 0.02f)) {
        std::printf("[selftest-remapcolor] GradientSteps seam dead: steps=0 (%.3f,%.3f,%.3f) == "
                    "steps=3 (%.3f,%.3f,%.3f) at gray — the param did not change the row\n",
                    smooth.x, smooth.y, smooth.z, stepped.x, stepped.y, stepped.z);
        ok = false;
      }
    }
  }

  if (!injectBug && ok)
    std::printf("[selftest-remapcolor] flat+resident 64x64 Gradient→t1 (red→green) + GradientSteps "
                "seam pixel match\n");

  lib->release();
  q->release();
  dev->release();
  pool->release();
  std::printf("[selftest-remapcolor] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace

// Self-registration. File-scope static feeds imageFilterSpecSink() + texReg() + imageFilterSelfTests()
// during pre-main dynamic init. No shared file edited (point_ops*.cpp glob picks this up). The selftest
// pair (remapcolor / remapcolor-bug) registers HERE via the ImageFilterOp trailing args.
static const ImageFilterOp _reg_remapcolor{
    // RemapColor (TiXL Lib.image.color.RemapColor): per-channel gradient color-remap.
    // Image input (t0) + Gradient input (the t1 binding) → Texture2D out. Gradient unwired → traced
    // black→white fallback.
    {"RemapColor", "RemapColor",
     {// Image input (TiXL t3 default null; FX → bound to a transparent-black dummy when unwired).
      {"Image", "Image", "Texture2D", true},
      // Gradient input (the t1 binding). Unwired → traced black→white fallback.
      {"Gradient", "Gradient", "Gradient", true},
      {"out", "out", "Texture2D", false},
      // Mode (Int→float, enum {UseGrayScale, IndividualChannels}; TiXL t3 default 1 = IndividualChannels)
      {"Mode", "Mode", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Enum,
       {"UseGrayScale", "IndividualChannels"}},
      // Exposure (Single, TiXL t3 default 1.0)
      {"Exposure", "Exposure", "Float", true, 1.0f, 0.0f, 8.0f, Widget::Slider},
      // GainAndBias (Vec2, TiXL t3 default (0.5,0.5))
      {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Cycle (Single, TiXL t3 default 0.0) — routed to the shader cbuffer "Offset" [fork-offset-from-cycle]
      {"Cycle", "Cycle", "Float", true, 0.0f, -4.0f, 4.0f, Widget::Slider},
      // Repeat (Single, TiXL t3 default 1.0)
      {"Repeat", "Repeat", "Float", true, 1.0f, 0.0f, 8.0f, Widget::Slider},
      // DontColorAlpha (Bool→float; TiXL t3 default false) — preserves orgColor.a when true
      {"DontColorAlpha", "DontColorAlpha", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      // GradientSteps (Int, TiXL t3 default 256) — the rasterized gradient row's texel count (banding).
      // [fork-gradientsteps-is-resolution]
      {"GradientSteps", "GradientSteps", "Float", true, 256.0f, 1.0f, 16384.0f, Widget::Slider},
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "RemapColor", cookRemapColor, "remapcolor", runRemapColorSelfTest};

}  // namespace sw
