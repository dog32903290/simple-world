// NGonGradient image generator op (a Gradient->t1 image-filter binding seam consumer; cloned from
// LinearGradient, the PROVING leaf for that seam).
//
// TiXL authority: external/tixl/Operators/Lib/image/generate/basic/NGonGradient.{cs,hlsl,t3}.
//   .cs   — slot declarations (Position, Sides, Radius, Curvature, Roundness, Blades, Rotate, Gradient,
//           Width, Offset, PingPong, Repeat, BiasAndGain, BlendMode, Resolution, Image) + BlendMode enum.
//   .t3   — defaults (Sides=5, Radius=0.33, Curvature=0, Roundness=1, Blades=0, Rotate=180, Width=0.14,
//           Offset=0, PingPong/Repeat=false, BiasAndGain=(0.5,0.5), BlendMode=0, Position=(0,0)) + the
//           Gradient→GradientsToTexture→t1 plumbing + the BiasAndGain Vector2Components routing.
//   .hlsl — psMain (ported VERBATIM to shaders/ngongradient.metal); SDF = sdRegularPolygon.
//
// Port class: a .t3 compound whose terminal is _multiImageFxSetupStatic → a single fragment shader
//   (ngongradient_vs/ngongradient_fs). Like NGon/LinearGradient this is a RENDER op (NOT compute):
//   cachedTexPSO → renderCommandEncoder → setFragmentTexture/Sampler/Bytes → drawPrimitives triangle 3.
//   Precedent cloned: point_ops_lineargradient.cpp.
//
// The Gradient→t1 binding: the op reads its gathered Gradient input (c.inputGradients[0]), rasterizes it
// to a 1×512 RGBA row via rasterizeGradientRow (the SAME row sampling GradientsToTexture uses —
// gradient_raster.h, can't drift), binds it at fragment texture(1) with the clampedSampler (s1).
//
// ★Unwired-Gradient fallback — TRACED from NGonGradient.t3 (NOT the child's embedded value):
//   NGonGradient.t3 :294-298 wires the op's OWN Gradient input SLOT (08937f41, default black→white at
//   t3:6-32) INTO the GradientsToTexture child (ba704603) Gradients slot (588be11f). So an UNWIRED
//   Gradient input feeds the op's Gradient SLOT DEFAULT — black→white (t3:13-28) — into the gradient
//   row. We mirror the live routing: fallback = black→white (same as LinearGradient's traced fallback).
//   [fork-gradient-default-traced]
//
// ★Offset routing: UNLIKE LinearGradient (Multiply + PickFloat), NGonGradient.t3 wires Offset (d926fc8b)
//   DIRECTLY into the FloatsToBuffer cbuffer slot (t3:246-249, no Multiply/PickFloat in the path). The
//   shader does `* 2 - Offset * Width` itself (NGonGradient.hlsl:151). So the cbuffer Offset = raw Offset
//   input, no host-side scalar reshuffle. [fork-offset-direct]
//
// ★BiasAndGain routing: the .cs input `BiasAndGain` (default (0.5,0.5)) routes through a Vector2Components
//   (X out 1cee5adb first, Y out 305d321d second, t3:192-201) into the cbuffer `GainAndBias` slot →
//   GainAndBias.x = BiasAndGain.x, GainAndBias.y = BiasAndGain.y (no swap). [fork-biasandgain-direct]
//
// ★Position.yx: the cbuffer Position is float2 (x,y); NGonGradient.hlsl:149 does `p += Position.yx`. The
//   swap lives in the SHADER (ngongradient.metal), ported verbatim; the host fills Position.x/.y straight.
//
// FORKS (named): generator dummy (1×1 transparent-black ImageA when unwired); gradient-row format
//   RGBA32F (gradient_raster.h fork-grad-row-format-32f); gain/bias + BlendColors + sdRegularPolygon
//   inlined in ngongradient.metal; fork-gradient-default-traced / fork-offset-direct above.
//
// Self-contained leaf: cookNGonGradient + ImageFilterOp self-registration + runNGonGradientSelfTest
//   (golden body IN THIS FILE — self-contained checker reusing sw_gradient.h + gradient_raster.h, so NO
//   gradient_golden.cpp edit, NO selftests.cpp edit; ImageFilterOp's 5th arg pushes the pair into
//   imageFilterSelfTests() — the SAME sink LinearGradient's golden uses). CMake point_ops*.cpp glob +
//   shaders/*.metal glob auto-pick both files — no CMake edit.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"            // SymbolLibrary (resident cook input)
#include "runtime/eval_context.h"              // EvaluationContext
#include "runtime/gradient_raster.h"           // rasterizeGradientRow, kGradientRowN
#include "runtime/graph.h"                     // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"              // libFromGraph (flat Graph -> SymbolLibrary)
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/ngongradient_params.h"       // NGonGradientParams/Resolution, NGONGRADIENT_*
#include "runtime/point_graph.h"               // TexCookCtx, cookParam, PointGraph::cook/cookResident
#include "runtime/resident_eval_graph.h"       // ResidentEvalGraph / buildEvalGraph
#include "runtime/sw_gradient.h"               // SwGradient (the consumed currency)
#include "runtime/tex_op_cache.h"             // cachedTexPSO, clearTexOpCache

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration so the file-scope ImageFilterOp registrar can reference the golden.
int runNGonGradientSelfTest(bool injectBug);

namespace {

// The unwired-Gradient fallback: the op's Gradient SLOT default (NGonGradient.t3 :13-28), which the
// .t3 connection (:294-298) feeds into the GradientsToTexture child. White→black, 2-stop Linear.
// (NGonGradient.t3 Color: stop0 = (1,1,1,1) @0, stop1 = (0,0,0,1) @1.) [fork-gradient-default-traced]
SwGradient defaultNGonGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1.0f, 1.0f, 1.0f, 1.0f)});  // t3:13-18
  g.steps.push_back({1.0f, simd::make_float4(0.0f, 0.0f, 0.0f, 1.0f)});  // t3:23-28
  return g;
}

// 1×1 transparent-black dummy for the no-Image case (generator mode). Same convention as
// makeNGonDummyTex / LinearGradient's makeDummyTex — the shader always gets a valid ImageA handle.
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

// NGonGradient texture op: single fullscreen pass. Reads c.inputGradients[0] (the gathered Gradient),
// rasterizes it to a 1×512 row, builds an SDF n-gon and samples the row at (dBiased, 0). Optionally
// composites over c.inputTexture (Image). Always writes c.output.
void cookNGonGradient(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "ngongradient_vs", "ngongradient_fs", fmt);
  if (!rps) return;

  // s0 texSampler: linear+Wrap (ImageA), matching _multiImageFxSetupStatic.t3 WrapMode=Wrap.
  MTL::SamplerDescriptor* sd0 = MTL::SamplerDescriptor::alloc()->init();
  sd0->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd0->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd0->setSAddressMode(MTL::SamplerAddressModeRepeat);
  sd0->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp0 = c.dev->newSamplerState(sd0);
  sd0->release();

  // ★s1 clampedSampler: linear+ClampToEdge (the gradient row). MANDATORY — the row is sampled at v=0
  // with the gradient value at u=dBiased; a Wrap sampler would corrupt the u/v edges.
  MTL::SamplerDescriptor* sd1 = MTL::SamplerDescriptor::alloc()->init();
  sd1->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd1->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd1->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd1->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp1 = c.dev->newSamplerState(sd1);
  sd1->release();

  // --- b0 params (NGonGradient.cs/.t3 defaults) ---
  NGonGradientParams p{};
  p.PositionX = cookParam(c, "Position.x", 0.0f);  // shader swaps to .yx (:149)
  p.PositionY = cookParam(c, "Position.y", 0.0f);
  p.Sides     = cookParam(c, "Sides",     5.0f);
  p.Radius    = cookParam(c, "Radius",    0.33f);
  p.Curvature = cookParam(c, "Curvature", 0.0f);
  p.Blades    = cookParam(c, "Blades",    0.0f);
  p.Roundness = cookParam(c, "Roundness", 1.0f);
  p.Rotate    = cookParam(c, "Rotate",    180.0f);  // degrees
  p.Width     = cookParam(c, "Width",     0.14f);
  p.Offset    = cookParam(c, "Offset",    0.0f);   // [fork-offset-direct] raw, no PickFloat reshuffle
  p.PingPong  = cookParam(c, "PingPong",  0.0f);
  p.Repeat    = cookParam(c, "Repeat",    0.0f);
  p.BlendMode = cookParam(c, "BlendMode", 0.0f);   // 0 = Normal
  // [fork-biasandgain-direct] GainAndBias.x = BiasAndGain.x, .y = BiasAndGain.y (no swap).
  p.GainAndBiasX = cookParam(c, "BiasAndGain.x", 0.5f);
  p.GainAndBiasY = cookParam(c, "BiasAndGain.y", 0.5f);
  // IsTextureValid: 1.0 if Image wired, else 0.0 (generator mode → return gradient).
  p.IsTextureValid = (c.inputTexture != nullptr) ? 1.0f : 0.0f;

  // b1 Resolution
  NGonGradientResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // Pull the gradient (gathered input, or the traced white→black fallback when unwired).
  const SwGradient& g = (c.inputGradients && !c.inputGradients->empty())
                            ? (*c.inputGradients)[0]
                            : defaultNGonGradient();
  MTL::Texture* gradTex = rasterizeGradientRow(c.dev, g, kGradientRowN);  // owned; release after draw
  if (!gradTex) { samp0->release(); samp1->release(); return; }

  // Bind ImageA (or 1×1 transparent-black dummy when no upstream). [generator-dummy]
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
  enc->setFragmentSamplerState(samp0, 0);                          // s0 texSampler (Wrap)
  enc->setFragmentSamplerState(samp1, 1);                          // s1 clampedSampler (ClampToEdge)
  enc->setFragmentBytes(&p,   sizeof(NGonGradientParams),     NGONGRADIENT_Params);
  enc->setFragmentBytes(&res, sizeof(NGonGradientResolution), NGONGRADIENT_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp0->release();
  samp1->release();
  gradTex->release();
  if (dummyTex) dummyTex->release();
}

}  // namespace

// Self-registration. File-scope static feeds imageFilterSpecSink() + texReg() + imageFilterSelfTests()
// during pre-main dynamic init. No shared file edited (point_ops*.cpp glob picks this up).
static const ImageFilterOp _reg_ngongradient{
    // NGonGradient (TiXL Lib.image.generate.basic.NGonGradient): gradient-mapped SDF N-gon generator.
    // Gradient input (the t1 binding) + optional Image input → Texture2D out. When no Image: returns
    // the gradient directly (IsTextureValid=0); when wired: BlendColors composite.
    // Distinct from NGon (which lerps Fill/Background) — this maps the SDF onto a Gradient via t1.
    {"NGonGradient", "NGonGradient",
     {// Optional Image input (TiXL default null — generator mode draws the gradient on its own)
      {"Image", "Image", "Texture2D", true},
      // Gradient input (the t1 binding). Unwired → traced white→black fallback.
      {"Gradient", "Gradient", "Gradient", true},
      {"out", "out", "Texture2D", false},
      // Sides (Single, TiXL t3 default 5.0)
      {"Sides", "Sides", "Float", true, 5.0f, 3.0f, 20.0f, Widget::Slider},
      // Radius (Single, TiXL t3 default 0.33)
      {"Radius", "Radius", "Float", true, 0.33f, 0.0f, 1.0f, Widget::Slider},
      // Curvature (Single, TiXL t3 default 0.0; flower/petal effect)
      {"Curvature", "Curvature", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Slider},
      // Blades (Single, TiXL t3 default 0.0; iris-blade star effect)
      {"Blades", "Blades", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider},
      // Roundness (Single, TiXL t3 default 1.0; corner rounding)
      {"Roundness", "Roundness", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Slider},
      // Rotate (Single, TiXL t3 default 180.0; degrees)
      {"Rotate", "Rotate", "Float", true, 180.0f, -180.0f, 180.0f},
      // Width (Single, TiXL t3 default 0.14; gradient band width)
      {"Width", "Width", "Float", true, 0.14f, 0.0f, 4.0f, Widget::Slider},
      // Offset (Single, TiXL t3 default 0.0; routed DIRECTLY, shader does * 2 - Offset*Width)
      {"Offset", "Offset", "Float", true, 0.0f, -4.0f, 4.0f, Widget::Slider},
      // Position (Vec2, TiXL t3 default (0,0); shader does p += Position.yx)
      {"Position.x", "Position", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Position.y", "Position.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // BiasAndGain (Vec2, TiXL t3 default (0.5,0.5); routed into cbuffer GainAndBias, no swap)
      {"BiasAndGain.x", "BiasAndGain", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"BiasAndGain.y", "BiasAndGain.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // PingPong / Repeat (bool→float; TiXL t3 default false)
      {"PingPong", "PingPong", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"Repeat", "Repeat", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      // BlendMode (Int→float; TiXL t3 default 0 = normal composite with upstream image)
      {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 9.0f, Widget::Enum,
       {"Normal", "Screen", "Multiply", "Overlay", "Difference", "SrcOnly",
        "DstOnly", "HardLight", "LinearDodge", "AlphaMask"}},
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "NGonGradient", cookNGonGradient, "ngongradient", runNGonGradientSelfTest};

// =================== --selftest-ngongradient: DefineGradient → NGonGradient (t1 binding) ===================
// CLOSED-FORM pixel golden for the Gradient->t1 binding via the NGonGradient SDF. A DefineGradient
// producer wired into NGonGradient's Gradient input — ★with a NON-DEFAULT red→green gradient (NOT the
// black→white fallback). This is the R-2 HARDENING: if the resident wire were cut, the cook would fall
// to the traced white→black FALLBACK and the red→green pins would DIVERGE → the resident wire is
// teeth-guarded. (LinearGradient's golden used black→white, which == its fallback, so it could NOT catch
// a resident-wire-cut. This one can.)
//
// With Image unwired (IsTextureValid=0 → the shader returns gradient directly, no BlendColors), each
// pixel's color is g.sample(t) where t is the shader's NGon-SDF projection of that pixel through
// sdRegularPolygon → PingPongRepeat → ApplyGainAndBias → clamp(0.001,0.999). We replicate the EXACT
// shader projection in ngonGradientT() (host), compute t for two distinct pixels, and assert the
// readback == g.sample(t) for the red→green gradient. Run on BOTH flat (cook) AND resident (cookResident).
//
// ── CLOSED-FORM ANCHOR (PIN A, center pixel) ──────────────────────────────────────────────────
// At center UV=(0.5,0.5) (square → aspectRatio=1): p=(0,0) → rotatePoint=(0,0) → +Position.yx=(0,0).
//   sdRegularPolygon((0,0), r=0.33, n=5, Blades=0, Curvature=0, Roundness=1):
//     an=π/5, acs=(cos π/5, sin π/5); originalLen=0.
//     bn = sw_mod(atan2(0,0), 2an) - an = 0 - π/5 = -π/5; bn≤0 → ×1.
//     p = 0·(cos bn, |sin bn|) = (0,0); p -= r·acs = (-0.33cos π/5, -0.33sin π/5).
//     p.y += clamp(-p.y, 0, r·acs.y): -p.y = 0.33 sin π/5 = r·acs.y → clamp = r·acs.y → p.y = 0.
//     p.y ≤ 0 → ×1 (unchanged 0). dist = length(p)·sign(p.x) = 0.33cos(π/5)·(-1) = -0.33cos(π/5).
//     flowerEffect = (0.33-0)·0 = 0. → dist = -0.33·cos(π/5) ≈ -0.2670.
//   c = dist·2 - Offset·Width = -0.5340 - 0 = -0.5340.
//   c = PingPongRepeat(-0.5340/0.14 = -3.814, 0, 0): repeat=0 → value=baseValue=-3.814,
//     then value = saturate(-3.814) = 0.  dBiased = ApplyGainAndBias(0,(0.5,0.5)) → 0 (value<1e-5).
//   clamp(0, 0.001, 0.999) = 0.001 → t≈0.001 → near the gradient START (red, for red→green).
// GOLDEN ASSERTION A: center pixel ≈ g.sample(0.001) ≈ red. (ngonGradientT reproduces this exactly.)
//
// ── PIN B (off-center, a DISTINCT t) ──────────────────────────────────────────────────────────
// A pixel far outside the polygon → dist large positive → c/Width large positive → saturate→1 →
// ApplyGainAndBias→1 → clamp(0.999) → t≈0.999 → gradient END (green). DISTINCT color from PIN A
// (guards a stuck-gradient bug). ngonGradientT computes the exact t for the chosen pixel.
//
// ── injectBug ─────────────────────────────────────────────────────────────────────────────────
// gradientInjectBug() corrupts the REAL DefineGradient cook (drops the last step) → the red→green
// gradient collapses to a single red stop → sample(t) is red for ALL t → PIN B (expects green) diverges
// from the FIXED un-corrupted host ref → ok=false → exit 1. No co-conditioning tautology.

namespace {

// Verbatim host replication of NGonGradient.hlsl psMain's projection for ONE pixel, with the golden's
// params (Sides=5, Radius=0.33, Curvature=0, Blades=0, Roundness=1, Rotate=180, Width=0.14, Offset=0,
// Position=(0,0), BiasAndGain=(0.5,0.5), PingPong=Repeat=false). aspectRatio=1 (square). Returns the
// gradient-sample t (= clamped dBiased).
float ngFmod(float x, float y) { return x - y * std::floor(x / y); }

float ngGetBias(float bias, float x) { return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f); }
float ngGetSchlickBias(float g, float x) {
  if (x < 0.5f) { x *= 2.0f; x = 0.5f * ngGetBias(g, x); }
  else { x = 2.0f * x - 1.0f; x = 0.5f * ngGetBias(1.0f - g, x) + 0.5f; }
  return x;
}
float ngApplyGainAndBias(float value, float gx, float gy) {  // bias-functions.hlsl scalar
  float g = std::min(std::max(gx, 0.0f), 1.0f), b = std::min(std::max(gy, 0.0f), 1.0f);
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) { value = ngGetBias(b, value); value = ngGetSchlickBias(g, value); }
  else { value = ngGetSchlickBias(g, value); value = ngGetBias(b, value); }
  return value;
}
float ngPingPongRepeat(float x, bool pingPong, bool repeat) {  // NGonGradient.hlsl (baseValue = x)
  float baseValue = x;
  float repeatValue = x - std::floor(x);  // frac
  float pingPongValue = 1.0f - std::fabs((x * 0.5f - std::floor(x * 0.5f)) * 2.0f - 1.0f);
  float singlePingPong = std::fabs(x);
  float sR = repeat ? 1.0f : 0.0f, sP = pingPong ? 1.0f : 0.0f;
  float pingPongOutput = singlePingPong + (pingPongValue - singlePingPong) * sR;
  float value = baseValue + (repeatValue - baseValue) * sR;
  value = value + (pingPongOutput - value) * sP;
  float sat = std::min(std::max(value, 0.0f), 1.0f);
  value = sat + (value - sat) * sR;
  return value;
}
float ngSdRegularPolygon(float px, float py, float r, float n,
                         float blades, float curvature, float roundness) {
  float an = 3.141593f / n;
  float acsx = std::cos(an), acsy = std::sin(an);
  float originalLen = std::sqrt(px * px + py * py);
  float bn = ngFmod(std::atan2(px, py), 2.0f * an) - an;
  bn *= bn > 0.0f ? (1.0f - std::min(std::max(blades, 0.0f), 1.0f)) : 1.0f;
  float len = std::sqrt(px * px + py * py);
  px = len * std::cos(bn);
  py = len * std::fabs(std::sin(bn));
  px -= r * acsx;
  py -= r * acsy;
  py += std::min(std::max(-py, 0.0f), r * acsy);  // clamp(-py, 0, r*acsy)
  py *= py > 0.0f ? (std::min(std::max(roundness, 0.0f), 1.0f)) : 1.0f;
  float dist = std::sqrt(px * px + py * py) * (px > 0.0f ? 1.0f : (px < 0.0f ? -1.0f : 0.0f));
  dist += (r - originalLen) * curvature;
  return dist;
}
float ngonGradientT(int px, int py, int W, int H) {
  float uvx = (px + 0.5f) / W, uvy = (py + 0.5f) / H;  // fragment center
  float aspect = (float)W / (float)H;                  // square → 1
  float pcx = uvx - 0.5f, pcy = uvy - 0.5f;
  pcx *= aspect;
  // rotatePoint(p, Rotate=180): angle=180°=π. cos=−1, sin≈0.
  float ang = 180.0f * (3.14159265358979323846f / 180.0f);
  float ca = std::cos(ang), sa = std::sin(ang);
  float rx = pcx * ca + pcy * sa;
  float ry = pcx * sa - pcy * ca;
  // p += Position.yx = (0,0)
  float c = ngSdRegularPolygon(rx, ry, 0.33f, 5.0f, 0.0f, 0.0f, 1.0f) * 2.0f - 0.0f * 0.14f;
  c = ngPingPongRepeat(c / 0.14f, false, false);
  float dBiased = ngApplyGainAndBias(c, 0.5f, 0.5f);
  return std::min(std::max(dBiased, 0.001f), 0.999f);
}

bool ngNearf(float a, float b, float eps) { return std::fabs(a - b) < eps; }
bool ngNear4(simd::float4 a, simd::float4 b, float eps) {
  return ngNearf(a.x, b.x, eps) && ngNearf(a.y, b.y, eps) && ngNearf(a.z, b.z, eps) &&
         ngNearf(a.w, b.w, eps);
}

// Build: node 30 = NGonGradient (Gradient input = port index 1); node 1 = DefineGradient set to a
// NON-DEFAULT red→green 2-stop Linear gradient (the R-2 hardening — distinct from the white→black
// fallback). DefineGradient out = port index 21 (16 color comps + 4 pos + 1 interp = ports 0..20; out=21).
void buildNGonGradientGraph(Graph& g) {
  Node ng; ng.id = 30; ng.type = "NGonGradient";
  ng.params["Sides"] = 5.0f; ng.params["Radius"] = 0.33f;
  ng.params["Curvature"] = 0.0f; ng.params["Blades"] = 0.0f; ng.params["Roundness"] = 1.0f;
  ng.params["Rotate"] = 180.0f; ng.params["Width"] = 0.14f; ng.params["Offset"] = 0.0f;
  ng.params["Position.x"] = 0.0f; ng.params["Position.y"] = 0.0f;
  ng.params["BiasAndGain.x"] = 0.5f; ng.params["BiasAndGain.y"] = 0.5f;
  ng.params["PingPong"] = 0.0f; ng.params["Repeat"] = 0.0f; ng.params["BlendMode"] = 0.0f;
  ng.params["Resolution"] = 4.0f; ng.params["CustomW"] = 64.0f; ng.params["CustomH"] = 64.0f;  // 64×64
  g.nodes.push_back(ng);

  // DefineGradient — RED→GREEN (NON-default): Color1=(1,0,0,1)@0, Color2=(0,1,0,1)@1.
  Node dg; dg.id = 1; dg.type = "DefineGradient";
  dg.params["Color1.x"] = 1.0f; dg.params["Color1.y"] = 0.0f; dg.params["Color1.z"] = 0.0f;
  dg.params["Color1.w"] = 1.0f; dg.params["Color1Pos"] = 0.0f;
  dg.params["Color2.x"] = 0.0f; dg.params["Color2.y"] = 1.0f; dg.params["Color2.z"] = 0.0f;
  dg.params["Color2.w"] = 1.0f; dg.params["Color2Pos"] = 1.0f;
  dg.params["Color3Pos"] = -1.0f; dg.params["Color4Pos"] = -1.0f;  // skipped (pos<0)
  dg.params["Interpolation"] = 0.0f;  // Linear
  g.nodes.push_back(dg);

  const int dgOutPort = 21;
  g.connections.push_back({700, pinId(1, dgOutPort), pinId(30, /*Gradient*/ 1)});
}

// The UN-corrupted host reference: red→green 2-stop Linear (matches buildNGonGradientGraph's DefineGradient).
SwGradient redGreenRef() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1, 0, 0, 1)});  // red
  g.steps.push_back({1.0f, simd::make_float4(0, 1, 0, 1)});  // green
  return g;
}

// Read pixel (px,py) RGBA8 and assert it ≈ ref.sample(ngonGradientT(px,py)). Two distinct pins.
bool checkNgPixels(MTL::Texture* tex, const SwGradient& ref, const char* tag) {
  if (!tex) { std::printf("[selftest-ngongradient] %s FAIL: null tex\n", tag); return false; }
  const uint32_t W = (uint32_t)tex->width(), H = (uint32_t)tex->height();
  if (W != 64 || H != 64) {
    std::printf("[selftest-ngongradient] %s FAIL: dims=%ux%u want 64x64\n", tag, W, H);
    return false;
  }
  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  auto rd = [&](int x, int y, int ch) { return px[((size_t)y * W + x) * 4 + ch] / 255.0f; };
  bool ok = true;
  // PIN A: center pixel (32,32) → t≈0.001 → red end. PIN B: corner (2,2), far outside → t≈0.999 → green.
  const int pins[2][2] = {{32, 32}, {2, 2}};
  for (int k = 0; k < 2; ++k) {
    int x = pins[k][0], y = pins[k][1];
    float t = ngonGradientT(x, y, (int)W, (int)H);
    simd::float4 want = ref.sample(t);
    simd::float4 got = simd::make_float4(rd(x, y, 0), rd(x, y, 1), rd(x, y, 2), rd(x, y, 3));
    // RGBA8 readback → ~1/255 quantization; tolerate 3/255 ≈ 0.012.
    if (!ngNear4(got, want, 0.012f)) {
      std::printf("[selftest-ngongradient] %s pin%c (%d,%d) t=%.4f got=(%.3f,%.3f,%.3f,%.3f) "
                  "want=(%.3f,%.3f,%.3f,%.3f) FAIL\n",
                  tag, 'A' + k, x, y, t, got.x, got.y, got.z, got.w, want.x, want.y, want.z, want.w);
      ok = false;
    }
  }
  // Load-bearing distinctness: pin A (red end) and pin B (green end) must be DIFFERENT colors (proves
  // the SDF projection varies across the image, not a flat constant; guards a stuck-gradient bug AND a
  // wire-cut to the white→black fallback would NOT produce red/green at these pins).
  {
    float tA = ngonGradientT(pins[0][0], pins[0][1], (int)W, (int)H);
    float tB = ngonGradientT(pins[1][0], pins[1][1], (int)W, (int)H);
    if (ngNear4(ref.sample(tA), ref.sample(tB), 0.05f)) {
      std::printf("[selftest-ngongradient] %s pins not distinct (tA=%.4f tB=%.4f) FAIL\n", tag, tA, tB);
      ok = false;
    }
  }
  return ok;
}

}  // namespace

bool& gradientInjectBug();  // gradient_op_registry.cpp (corrupts the REAL DefineGradient cook)

int runNGonGradientSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-ngongradient] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  PointGraph pg(dev, lib, q, 64, 64);

  // Host reference = the red→green gradient (UN-corrupted; the bug corrupts the COOK, not this ref).
  SwGradient ref = redGreenRef();

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  Graph g;
  buildNGonGradientGraph(g);

  // --- FLAT cook ---. Harness convention (--bite): the -bug variant must exit NON-zero. injectBug
  // corrupts the REAL DefineGradient cook (drops the green stop) so the red→green collapses → PIN B
  // (expects green) diverges from the un-corrupted ref → flatOk=false → return 1.
  gradientInjectBug() = injectBug;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/30);
  gradientInjectBug() = false;
  bool flatOk = checkNgPixels(pg.target(), ref, injectBug ? "flat(bug)" : "flat");

  // --- RESIDENT (production) cook --- proves the Gradient→t1 wire is LIVE on cookResident (R-2 rule).
  // ★With the red→green NON-default gradient, a cut resident wire would fall to the white→black fallback
  //   and the red/green pins would DIVERGE → this resident assert is teeth-guarded (unlike LinearGradient).
  bool resOk = true;
  if (!injectBug) {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, "Root");
    pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"30");
    resOk = checkNgPixels(pg.target(), ref, "resident");
  }

  bool ok = flatOk && resOk;
  if (!injectBug && ok)
    std::printf("[selftest-ngongradient] flat+resident 64x64 Gradient→t1 (red→green) pixel match\n");

  lib->release();
  q->release();
  dev->release();
  pool->release();
  std::printf("[selftest-ngongradient] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
