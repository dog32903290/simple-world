// RadialGradient image generator op — clone of LinearGradient (point_ops_lineargradient.cpp), the
// Gradient->t1 image-filter binding seam's SECOND consumer (the seam is already LIVE in both cooks).
//
// TiXL authority: external/tixl/Operators/Lib/image/generate/basic/RadialGradient.{cs,t3} +
//   external/tixl/Operators/Lib/Assets/shaders/img/generate/RadialGradient.hlsl.
//   .cs   — slot declarations (Image, Gradient, Width, Stretch, Offset, PingPong, Repeat, Center,
//           PolarOrientation, BiasAndGain, Noise, BlendMode, Resolution, ...) + RgbBlendModes enum map.
//   .t3   — defaults (Width=1, Stretch=(1,1), BiasAndGain=(0.5,0.5), Center=(0,0), Offset=0,
//           PingPong/Repeat/PolarOrientation=false, Noise=0, BlendMode=0) + the Gradient→
//           GradientsToTexture→t1 plumbing. (NO Multiply/PickFloat on Offset — wired DIRECT, see below.)
//   .hlsl — psMain (ported VERBATIM to shaders/radialgradient.metal).
//
// Port class: a .t3 compound whose terminal is _multiImageFxSetupStatic → a single fragment shader
//   (radialgradient_vs/radialgradient_fs). Like LinearGradient/NGon this is a RENDER op (NOT compute):
//   cachedTexPSO → renderCommandEncoder → setFragmentTexture/Sampler/Bytes → drawPrimitives triangle 3.
//   The precedent cloned is point_ops_lineargradient.cpp.
//
// The Gradient→t1 binding: the op reads its gathered Gradient input (c.inputGradients[0]), rasterizes
// it to a 1×512 RGBA row via rasterizeGradientRow (the SAME row sampling GradientsToTexture uses —
// gradient_raster.h, can't drift), and binds it at fragment texture(1) with the clampedSampler.
//
// ★Unwired-Gradient fallback (defaultRadialGradient) — TRACED from the .t3 connection, NOT the child's
//   embedded value: RadialGradient.t3 wires the op's OWN Gradient input slot (3f5a284b, default
//   ~white→~black) INTO the GradientsToTexture child's Gradients slot (588be11f). So an UNWIRED Gradient
//   input feeds the op's Gradient SLOT DEFAULT — white→black — into the gradient row; the child's
//   embedded magenta→blue default is OVERRIDDEN by that connection and never reached. We mirror the live
//   routing: fallback = the op's slot default (white→black). [fork-gradient-default-traced]
//   NOTE the order is REVERSED vs LinearGradient (whose slot default was black→white): RadialGradient's
//   slot default stop0≈white (1,0.99999,1,1), stop1≈black (0,~0,1e-06,1).
//
// ★Offset routing: UNLIKE LinearGradient (Multiply+PickFloat compound), RadialGradient.t3 wires the
//   Offset input DIRECTLY into the shader cbuffer Offset (no Multiply/PickFloat child). The host writes
//   the raw Offset. [fork-offset-direct]  (Trace: the .t3 has no Multiply/PickFloat children; the only
//   children are IntToFloat/BoolToFloat/Vector2Components feeding _multiImageFxSetupStatic + a
//   GradientsToTexture for the gradient — Offset goes straight to the shader buffer.)
//
// FORKS (named): generator dummy (1×1 transparent-black ImageA when unwired); gradient-row format
//   RGBA32F (gradient_raster.h fork-grad-row-format-32f); gain/bias + BlendColors + hash12 inlined in
//   radialgradient.metal; fork-gradient-default-traced + fork-offset-direct (above).
//
// Self-contained leaf: cookRadialGradient + ImageFilterOp self-registration + runRadialGradientSelfTest
//   (IMPL IS IN THIS FILE — not gradient_golden.cpp — registered via the imageFilterSelfTests() sink
//   through the ImageFilterOp registrar, so NO shared file is edited). CMake point_ops*.cpp glob +
//   shaders/*.metal glob auto-pick both files — no CMake edit.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"            // SymbolLibrary (resident cook input)
#include "runtime/eval_context.h"
#include "runtime/gradient_raster.h"            // rasterizeGradientRow, kGradientRowN
#include "runtime/graph.h"                      // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"               // libFromGraph (flat Graph -> SymbolLibrary)
#include "runtime/image_filter_op_registry.h"   // ImageFilterOp self-registration
#include "runtime/point_graph.h"                // TexCookCtx, cookParam, PointGraph::cook/cookResident
#include "runtime/radialgradient_params.h"      // RadialGradientParams/Resolution, RADIALGRADIENT_*
#include "runtime/resident_eval_graph.h"        // ResidentEvalGraph / buildEvalGraph
#include "runtime/sw_gradient.h"                // SwGradient (the consumed currency)
#include "runtime/tex_op_cache.h"               // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// sw-scope (EXTERNAL linkage) — defined in gradient_op_registry.cpp. MUST be declared OUTSIDE the
// anonymous namespace below, else it gets internal linkage and won't resolve the real symbol.
bool& gradientInjectBug();

namespace {

// The unwired-Gradient fallback: the op's Gradient SLOT default (RadialGradient.t3 input 3f5a284b),
// which the .t3 connection feeds into the GradientsToTexture child. ~white→~black, 2-stop Linear.
// [fork-gradient-default-traced]  (Reversed order vs LinearGradient's black→white.)
SwGradient defaultRadialGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1.0f, 0.99999f, 1.0f, 1.0f)});           // t3 stop0 ≈white
  g.steps.push_back({1.0f, simd::make_float4(0.0f, 1.2159347e-11f, 1e-06f, 1.0f)});   // t3 stop1 ≈black
  return g;
}

// 1×1 transparent-black dummy for the no-Image case (generator mode). Same convention as
// cookLinearGradient's makeDummyTex — the shader always gets a valid ImageA handle.
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

// RadialGradient texture op: single fullscreen pass. Reads c.inputGradients[0] (the gathered Gradient),
// rasterizes it to a 1×512 row, samples it in the shader at (dBiased, 0). Optionally composites over
// c.inputTexture (Image). Always writes c.output.
void cookRadialGradient(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "radialgradient_vs", "radialgradient_fs", fmt);
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

  // --- b0 params (RadialGradient.cs/.t3 defaults) ---
  RadialGradientParams p{};
  p.CenterX = cookParam(c, "Center.x", 0.0f);
  p.CenterY = cookParam(c, "Center.y", 0.0f);
  p.Width   = cookParam(c, "Width", 1.0f);
  p.Offset  = cookParam(c, "Offset", 0.0f);  // wired DIRECT in .t3 (no Multiply/PickFloat) [fork-offset-direct]
  p.PingPong = cookParam(c, "PingPong", 0.0f);
  p.Repeat   = cookParam(c, "Repeat", 0.0f);
  p.PolarOrientation = cookParam(c, "PolarOrientation", 0.0f);
  p.BlendMode = cookParam(c, "BlendMode", 0.0f);  // 0 = Normal
  // HLSL b0 field "GainAndBias" <- .cs input "BiasAndGain" (the rename trap; default (0.5,0.5)).
  p.GainAndBiasX = cookParam(c, "BiasAndGain.x", 0.5f);
  p.GainAndBiasY = cookParam(c, "BiasAndGain.y", 0.5f);
  p.StretchX = cookParam(c, "Stretch.x", 1.0f);
  p.StretchY = cookParam(c, "Stretch.y", 1.0f);
  p.Noise = cookParam(c, "Noise", 0.0f);

  // IsTextureValid: 1.0 if Image wired, else 0.0 (generator mode → return gradient).
  p.IsTextureValid = (c.inputTexture != nullptr) ? 1.0f : 0.0f;

  // b1 Resolution
  RadialGradientResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // Pull the gradient (gathered input, or the traced white→black fallback when unwired).
  const SwGradient& g = (c.inputGradients && !c.inputGradients->empty())
                            ? (*c.inputGradients)[0]
                            : defaultRadialGradient();
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
  enc->setFragmentBytes(&p,   sizeof(RadialGradientParams),     RADIALGRADIENT_Params);
  enc->setFragmentBytes(&res, sizeof(RadialGradientResolution), RADIALGRADIENT_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp0->release();
  samp1->release();
  gradTex->release();
  if (dummyTex) dummyTex->release();
}

// ======================== --selftest-radialgradient: DefineGradient → RadialGradient ========================
// CLOSED-FORM pixel golden for the Gradient->t1 binding on the RadialGradient consumer. A DefineGradient
// producer set to a NON-DEFAULT RED→GREEN gradient is wired into RadialGradient's Gradient input; with
// the default radial params (PolarOrientation=0, Width=1, Offset=0, PingPong=Repeat=0, Stretch=(1,1),
// Center=(0,0), BiasAndGain=(0.5,0.5), Noise=0) and Image unwired (IsTextureValid=0 → the shader returns
// the gradient directly, no BlendColors), each pixel's color is g.sample(t) where t is the shader's
// projection (radial distance) of that pixel through PingPongRepeat + ApplyGainAndBias(0.5,0.5)=identity.
//
// ★R-2 HARDENING (the reason this golden uses RED→GREEN, NOT black→white): if the resident Gradient wire
// were cut, the cook falls to defaultRadialGradient() (white→black). With a black→white *test* gradient
// the cut would be NEAR-INVISIBLE (LinearGradient's golden could not catch its own resident-wire-cut).
// With RED→GREEN, the fallback white→black sample is GRAY (R=G=B) at every t, so the G channel of every
// pin DIVERGES sharply from the red→green reference (G = t vs G ≈ 1-t). The resident wire is teeth-guarded.
//
// RED bite: gradientInjectBug() corrupts the REAL DefineGradient cook (drops a stop) so the rasterized
// row diverges → the pins diverge from the UN-corrupted host red→green reference (no co-condition
// tautology). Run on BOTH flat (PointGraph::cook) AND resident (cookResident) — R-2 iron rule.

bool nearf(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
bool near4(simd::float4 a, simd::float4 b, float eps = 1e-3f) {
  return nearf(a.x, b.x, eps) && nearf(a.y, b.y, eps) && nearf(a.z, b.z, eps) && nearf(a.w, b.w, eps);
}

// The non-default wired gradient the golden builds: RED (1,0,0,1) @0 → GREEN (0,1,0,1) @1, Linear.
// Host reference (UN-corrupted): sample(t) = (1-t, t, 0, 1). This is the resident-wire teeth-guard:
// distinct from the white→black fallback (gray) in the G (and R) channels.
SwGradient redGreenGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1.0f, 0.0f, 0.0f, 1.0f)});  // red
  g.steps.push_back({1.0f, simd::make_float4(0.0f, 1.0f, 0.0f, 1.0f)});  // green
  return g;
}

// --- Verbatim host replication of RadialGradient.hlsl psMain's projection (radial branch) for one
//     pixel, with the golden's params. Returns the gradient-sample t (= clamped c; ApplyGainAndBias
//     with (0.5,0.5) is the identity for c in [0.001,0.999], see header note). aspectRatio=1 (square).
float rgPingPongRepeat(float x, bool pingPong, bool repeat) {  // hlsl PingPongRepeat (lines 46-60)
  float baseValue = x + 0.5f;
  float repeatValue = x + 0.5f - std::floor(x + 0.5f);  // frac
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
float rgGetBias(float bias, float x) { return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f); }
float rgGetSchlickBias(float g, float x) {
  if (x < 0.5f) { x *= 2.0f; x = 0.5f * rgGetBias(g, x); }
  else { x = 2.0f * x - 1.0f; x = 0.5f * rgGetBias(1.0f - g, x) + 0.5f; }
  return x;
}
float rgApplyGainAndBias(float value, float gx, float gy) {  // bias-functions.hlsl scalar
  float g = std::min(std::max(gx, 0.0f), 1.0f), b = std::min(std::max(gy, 0.0f), 1.0f);
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) { value = rgGetBias(b, value); value = rgGetSchlickBias(g, value); }
  else { value = rgGetSchlickBias(g, value); value = rgGetBias(b, value); }
  return value;
}
// The fragment receives texCoord ≈ ((px+0.5)/W, (py+0.5)/H) at pixel center (square: aspectRatio=1).
// Radial branch with PolarOrientation=0, Center=(0,0), Stretch=(1,1), Width=1, Offset=0, PingPong=0,
// Repeat=0, dir=sign(1)=1 (>0 → no flip), GainAndBias=(0.5,0.5).
float shaderT(int px, int py, int W, int H) {
  float uvx = (px + 0.5f) / W, uvy = (py + 0.5f) / H;
  float aspect = (float)W / (float)H;       // square → 1
  float pxc = (uvx - 0.5f) * aspect;        // p.x *= aspect
  float pyc = (uvy - 0.5f);
  // d = (p - Center*(1,-1)) / Stretch = p   (Center=0, Stretch=1)
  float r = std::sqrt(pxc * pxc + pyc * pyc);
  float w = std::max(std::fabs(1.0f), 1e-6f);
  float c = r * 2.0f / w - (0.5f + (1.0f - 0.5f) * 0.0f);  // mix(0.5,1,PingPong=0)=0.5
  c -= 0.0f;                                                // Offset=0
  c = rgPingPongRepeat(c, false, false);
  // dir>0 → no flip
  c = std::min(std::max(c, 0.001f), 0.999f);
  float dBiased = rgApplyGainAndBias(c, 0.5f, 0.5f);
  return dBiased;
}

// Build: node 30 = RadialGradient (Gradient input = port index 1); node 1 = DefineGradient set
// RED→GREEN (Color1=red @0, Color2=green @1). Image unwired → generator mode.
void buildRadialGradientGraph(Graph& g) {
  Node rg; rg.id = 30; rg.type = "RadialGradient";
  rg.params["Width"] = 1.0f; rg.params["Offset"] = 0.0f;
  rg.params["Center.x"] = 0.0f; rg.params["Center.y"] = 0.0f;
  rg.params["BiasAndGain.x"] = 0.5f; rg.params["BiasAndGain.y"] = 0.5f;
  rg.params["Stretch.x"] = 1.0f; rg.params["Stretch.y"] = 1.0f;
  rg.params["Noise"] = 0.0f; rg.params["BlendMode"] = 0.0f;
  rg.params["PingPong"] = 0.0f; rg.params["Repeat"] = 0.0f;
  rg.params["PolarOrientation"] = 0.0f;
  rg.params["Resolution"] = 4.0f; rg.params["CustomW"] = 64.0f; rg.params["CustomH"] = 64.0f;  // Custom 64×64
  g.nodes.push_back(rg);

  Node dg; dg.id = 1; dg.type = "DefineGradient";
  // ★NON-DEFAULT gradient (the R-2 teeth-guard): Color1 = red @0, Color2 = green @1.
  dg.params["Color1.x"] = 1.0f; dg.params["Color1.y"] = 0.0f; dg.params["Color1.z"] = 0.0f; dg.params["Color1.w"] = 1.0f;
  dg.params["Color1Pos"] = 0.0f;
  dg.params["Color2.x"] = 0.0f; dg.params["Color2.y"] = 1.0f; dg.params["Color2.z"] = 0.0f; dg.params["Color2.w"] = 1.0f;
  dg.params["Color2Pos"] = 1.0f;
  dg.params["Interpolation"] = 0.0f;  // Linear
  g.nodes.push_back(dg);

  // DefineGradient out = port index 21 (16 color comps + 4 pos + 1 interp = ports 0..20; out=21).
  // RadialGradient Gradient = port index 1 (Image=0, Gradient=1, out=2, ...).
  const int dgOutPort = 21;
  g.connections.push_back({700, pinId(1, dgOutPort), pinId(30, /*Gradient*/ 1)});
}

// Read pixel (px,py) RGBA8 and assert it ≈ ref.sample(shaderT(px,py)). Two pins (a center pin near red,
// an off-center pin green-dominant) + a load-bearing distinctness + a teeth-guard against the gray
// fallback (proves the G channel actually carries the wired gradient, not the white→black fallback).
bool checkRgPixels(MTL::Texture* tex, const SwGradient& ref, const char* tag) {
  if (!tex) { std::printf("[selftest-radialgradient] %s FAIL: null tex\n", tag); return false; }
  const uint32_t W = (uint32_t)tex->width(), H = (uint32_t)tex->height();
  if (W != 64 || H != 64) {
    std::printf("[selftest-radialgradient] %s FAIL: dims=%ux%u want 64x64\n", tag, W, H);
    return false;
  }
  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  auto rd = [&](int x, int y, int ch) { return px[((size_t)y * W + x) * 4 + ch] / 255.0f; };
  bool ok = true;
  // PIN A: center (32,32) → r≈0 → c≈0.022 → t≈0.022 → nearly RED (0.978, 0.022, 0).
  // PIN B: off-center (56,32) → r large → t≈0.766 → GREEN-dominant (0.234, 0.766, 0). Distinct from A.
  const int pins[2][2] = {{32, 32}, {56, 32}};
  for (int k = 0; k < 2; ++k) {
    int x = pins[k][0], y = pins[k][1];
    float t = shaderT(x, y, (int)W, (int)H);
    simd::float4 want = ref.sample(t);
    simd::float4 got = simd::make_float4(rd(x, y, 0), rd(x, y, 1), rd(x, y, 2), rd(x, y, 3));
    // RGBA8 readback → ~1/255 quantization; tolerate 3/255 ≈ 0.012.
    if (!near4(got, want, 0.012f)) {
      std::printf("[selftest-radialgradient] %s pin%c (%d,%d) t=%.3f got=(%.3f,%.3f,%.3f,%.3f) "
                  "want=(%.3f,%.3f,%.3f,%.3f) FAIL\n",
                  tag, 'A' + k, x, y, t, got.x, got.y, got.z, got.w, want.x, want.y, want.z, want.w);
      ok = false;
    }
  }
  // Load-bearing distinctness: pin A (t≈0.022) and pin B (t≈0.766) must be DIFFERENT colors (proves
  // the radial projection actually varies across radius, not a flat constant — guards stuck-gradient).
  {
    float tA = shaderT(pins[0][0], pins[0][1], (int)W, (int)H);
    float tB = shaderT(pins[1][0], pins[1][1], (int)W, (int)H);
    if (near4(ref.sample(tA), ref.sample(tB), 0.05f)) {
      std::printf("[selftest-radialgradient] %s pins not distinct (tA=%.3f tB=%.3f) FAIL\n", tag, tA, tB);
      ok = false;
    }
  }
  // ★Teeth-guard for the RED→GREEN wire (R-2): the wired gradient's G channel must be NON-gray. With
  // the white→black fallback, G==R==B at every pixel. Assert pin B's G clearly EXCEEDS its R (green-
  // dominant) — impossible under the gray fallback (R==G). This is what makes the resident-wire-cut bite.
  {
    int x = pins[1][0], y = pins[1][1];
    float gCh = rd(x, y, 1), rCh = rd(x, y, 0);
    if (!(gCh > rCh + 0.2f)) {
      std::printf("[selftest-radialgradient] %s green-dominance teeth-guard FAIL (G=%.3f R=%.3f) — "
                  "gradient looks gray (white→black fallback?)\n", tag, gCh, rCh);
      ok = false;
    }
  }
  return ok;
}

int runRadialGradientSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-radialgradient] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  PointGraph pg(dev, lib, q, 64, 64);

  // Host reference = the UN-corrupted RED→GREEN gradient (the bug corrupts the COOK, not this ref).
  SwGradient ref = redGreenGradient();

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  Graph g;
  buildRadialGradientGraph(g);

  // --- FLAT cook --- (PointGraph::cook). injectBug corrupts the REAL DefineGradient cook → the
  // rasterized row diverges → the pins diverge from the un-corrupted red→green ref → exit 1.
  gradientInjectBug() = injectBug;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/30);
  gradientInjectBug() = false;
  bool flatOk = checkRgPixels(pg.target(), ref, injectBug ? "flat(bug)" : "flat");

  // --- RESIDENT (production) cook --- proves the Gradient→t1 wire is LIVE on cookResident (R-2 rule),
  // and (with the RED→GREEN gradient) that a CUT wire would fall to the gray fallback and bite the
  // green-dominance teeth-guard. Skipped in -bug mode (the flat tooth already bit).
  bool resOk = true;
  if (!injectBug) {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rgr = buildEvalGraph(slib, "Root");
    pg.cookResident(rgr, ctx, /*reg=*/nullptr, /*targetPath=*/"30");
    resOk = checkRgPixels(pg.target(), ref, "resident");
  }

  bool ok = flatOk && resOk;
  if (!injectBug && ok)
    std::printf("[selftest-radialgradient] flat+resident 64x64 Gradient→t1 (red→green) pixel match\n");

  lib->release();
  q->release();
  dev->release();
  pool->release();
  std::printf("[selftest-radialgradient] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace

// Self-registration. File-scope static feeds imageFilterSpecSink() + texReg() + imageFilterSelfTests()
// during pre-main dynamic init. No shared file edited (point_ops*.cpp glob picks this up). The selftest
// pair (radialgradient / radialgradient-bug) registers HERE via the ImageFilterOp trailing args —
// gradient_golden.cpp and selftests.cpp are NOT touched.
static const ImageFilterOp _reg_radialgradient{
    // RadialGradient (TiXL Lib.image.generate.basic.RadialGradient): radial/polar gradient.
    // Gradient input (the t1 binding) + optional Image input → Texture2D out. When no Image: returns
    // the gradient directly (IsTextureValid=0); when wired: BlendColors composite.
    {"RadialGradient", "RadialGradient",
     {// Optional Image input (TiXL default null — generator mode draws the gradient on its own)
      {"Image", "Image", "Texture2D", true},
      // Gradient input (the t1 binding). Unwired → traced white→black fallback.
      {"Gradient", "Gradient", "Gradient", true},
      {"out", "out", "Texture2D", false},
      // Width (Single, TiXL t3 default 1.0; sign flips center↔edge)
      {"Width", "Width", "Float", true, 1.0f, -4.0f, 4.0f, Widget::Slider},
      // Center (Vec2, TiXL t3 default (0,0))
      {"Center.x", "Center", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Center.y", "Center.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Stretch (Vec2, TiXL t3 default (1,1))
      {"Stretch.x", "Stretch", "Float", true, 1.0f, 0.0f, 4.0f, Widget::Vec, {}, true, 2},
      {"Stretch.y", "Stretch.y", "Float", true, 1.0f, 0.0f, 4.0f, Widget::Vec, {}, true, 1},
      // BiasAndGain (Vec2, TiXL t3 default (0.5,0.5)) — HLSL b0 field name is "GainAndBias"
      {"BiasAndGain.x", "BiasAndGain", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"BiasAndGain.y", "BiasAndGain.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Offset (Single, TiXL t3 default 0.0; wired DIRECT — no PickFloat)
      {"Offset", "Offset", "Float", true, 0.0f, -4.0f, 4.0f, Widget::Slider},
      // Noise (Single, TiXL t3 default 0.0; hash12 dithering)
      {"Noise", "Noise", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider},
      // PingPong / Repeat / PolarOrientation (bool→float; TiXL t3 default false)
      {"PingPong", "PingPong", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"Repeat", "Repeat", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      {"PolarOrientation", "PolarOrientation", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      // BlendMode (Int→float, SharedEnums.RgbBlendModes; TiXL t3 default 0 = Normal)
      {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 9.0f, Widget::Enum,
       {"Normal", "Screen", "Multiply", "Overlay", "Difference", "UseImageA_RGB",
        "UseImageB_RGB", "ColorDodge", "LinearDodge", "MultiplyA"}},
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "RadialGradient", cookRadialGradient, "radialgradient", runRadialGradientSelfTest};

}  // namespace sw
