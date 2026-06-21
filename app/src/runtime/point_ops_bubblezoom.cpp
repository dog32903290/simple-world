// BubbleZoom image FX op — magnifying-bubble distortion, the Gradient->t1 image-filter binding seam's
// FX consumer (the seam is already LIVE in both cooks, proven by the gradient generators). Cloned from
// point_ops_radialgradient.cpp; render-op (NOT compute) precedent point_ops_ngon.cpp.
//
// TiXL authority: external/tixl/Operators/Lib/image/fx/distort/BubbleZoom.{cs,t3} +
//   external/tixl/Operators/Lib/Assets/shaders/img/fx/BubbleZoom.hlsl.
//   .cs   — slots: Image, Center, Magnify, Feather, FeatherGradient, Radius, FlipEffect, GainAndBias,
//           Resolution, Bias. (Bias is DEAD — unconnected in the .t3.)
//   .t3   — defaults (Center=(0,0), Magnify=1.25, Feather=1.0, Radius=0.5, GainAndBias=(0.5,0.5),
//           FlipEffect=0.0, Bias=0.0) + the FeatherGradient→GradientsToTexture→t1 plumbing.
//   .hlsl — psMain (ported VERBATIM to shaders/bubblezoom.metal).
//
// Port class: a .t3 compound whose terminal is _multiImageFxSetupStatic → a single fragment shader
//   (bubblezoom_vs/bubblezoom_fs). Like RadialGradient/LinearGradient this is a RENDER op (NOT compute):
//   cachedTexPSO → renderCommandEncoder → setFragmentTexture/Sampler/Bytes → drawPrimitives triangle 3.
//
// The Gradient→t1 binding: the op reads its gathered Gradient input (c.inputGradients[0]), rasterizes
// it to a 1×512 RGBA row via rasterizeGradientRow (the SAME row sampling GradientsToTexture uses —
// gradient_raster.h, can't drift), and binds it at fragment texture(1) with the clampedSampler.
//
// ★Unwired-Gradient fallback (defaultBubbleZoomGradient) — TRACED from the .t3 connection, NOT the
//   GradientsToTexture child's embedded value: BubbleZoom.t3 wires the op's OWN FeatherGradient input
//   slot (0ce5b753, default ~white(A=0)→~blue(A=0.03)) INTO the GradientsToTexture child's Gradients
//   slot (588be11f). So an UNWIRED Gradient input feeds the op's FeatherGradient SLOT DEFAULT into the
//   gradient row; the child's embedded magenta(1,0,1,1)→blue(0,0,1,1) default is OVERRIDDEN by that
//   connection and never reached. We mirror the live routing: fallback = the op's FeatherGradient slot
//   default (white A=0 → blue A=0.03). [fork-gradient-default-traced]  (Same trace discipline the
//   generators used — the op's slot default wins over the embedded child default.)
//
// ★Magnify→ScaleFactor rename: the HLSL b0 field is named "ScaleFactor"; the .cs input is "Magnify"
//   (.t3 default 1.25). The cook maps Magnify → ScaleFactor. NO Multiply/PickFloat in the .t3 (traced,
//   Cut55 discipline) — the connection wires Magnify DIRECTLY into the FloatsToBuffer slot that fills
//   the cbuffer. [fork-magnify-rename]
//
// ★DEAD Bias input: BubbleZoom.cs declares a `Bias` input (e5a5d0cf, .t3 default 0.0) that has NO
//   Connection in the .t3 (no target references its GUID) and feeds NO shader field. We DO NOT wire it
//   and DO NOT add a cbuffer slot. The NodeSpec omits it entirely. [fork-dead-bias]
//
// ★FX (not generator): UNLIKE the gradient generators, BubbleZoom's Image is mandatory (t0); the shader
//   always samples ImageA (no IsTextureValid branch). When the host has no upstream Image we bind a 1×1
//   transparent-black dummy (orgColor=(0,0,0,0)) so ImageA is a valid handle — the shader's
//   lerp(orgColor.rgb, gradient.rgb, gradient.a) then returns (gradient.rgb*gradient.a, 0).
//
// FORKS (named): generator dummy (1×1 transparent-black ImageA when unwired); gradient-row format
//   RGBA32F (gradient_raster.h fork-grad-row-format-32f); gain/bias inlined in bubblezoom.metal;
//   fork-gradient-default-traced + fork-magnify-rename + fork-dead-bias (above); s0=Clamp (.t3 WrapMode).
//
// Self-contained leaf: cookBubbleZoom + ImageFilterOp self-registration + runBubbleZoomSelfTest (IMPL
//   IS IN THIS FILE — not gradient_golden.cpp — registered via the imageFilterSelfTests() sink through
//   the ImageFilterOp registrar, so NO shared file is edited). CMake point_ops*.cpp glob + shaders/*.metal
//   glob auto-pick both files — no CMake edit.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/bubblezoom_params.h"          // BubbleZoomParams/Resolution, BUBBLEZOOM_*
#include "runtime/compound_graph.h"             // SymbolLibrary (resident cook input)
#include "runtime/eval_context.h"
#include "runtime/gradient_raster.h"            // rasterizeGradientRow, kGradientRowN
#include "runtime/graph.h"                      // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"               // libFromGraph (flat Graph -> SymbolLibrary)
#include "runtime/image_filter_op_registry.h"   // ImageFilterOp self-registration
#include "runtime/point_graph.h"                // TexCookCtx, cookParam, PointGraph::cook/cookResident
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

// The unwired-Gradient fallback: the op's FeatherGradient SLOT default (BubbleZoom.t3 input 0ce5b753),
// which the .t3 connection feeds into the GradientsToTexture child. ~white(A=0)→~blue(A=0.03), 2-stop
// Linear. [fork-gradient-default-traced]  (NOT the child's embedded magenta→blue — that is overridden.)
SwGradient defaultBubbleZoomGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1.0f, 0.99999f, 1.0f, 0.0f)});             // t3 stop0 white A=0
  g.steps.push_back({1.0f, simd::make_float4(0.0f, 1.2159347e-11f, 1e-06f, 0.030000024f)});  // t3 stop1 blue A=0.03
  return g;
}

// 1×1 transparent-black dummy for the no-Image case. Same convention as cookRadialGradient's
// makeDummyTex — the shader always gets a valid ImageA handle (orgColor=(0,0,0,0)).
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

// BubbleZoom texture op: single fullscreen pass. Reads c.inputGradients[0] (the gathered Gradient),
// rasterizes it to a 1×512 row, samples it in the shader at (dBiased, 0); samples ImageA (t0) at the
// magnified lookup UV; lerps the two by gradient alpha. Always writes c.output.
void cookBubbleZoom(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "bubblezoom_vs", "bubblezoom_fs", fmt);
  if (!rps) return;

  // s0 texSampler: linear+Clamp (ImageA), matching BubbleZoom.t3 _multiImageFxSetupStatic WrapMode=Clamp.
  MTL::SamplerDescriptor* sd0 = MTL::SamplerDescriptor::alloc()->init();
  sd0->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd0->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd0->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd0->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
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

  // --- b0 params (BubbleZoom.cs/.t3 defaults) ---
  BubbleZoomParams p{};
  p.CenterX = cookParam(c, "Center.x", 0.0f);
  p.CenterY = cookParam(c, "Center.y", 0.0f);
  // HLSL b0 field "ScaleFactor" <- .cs input "Magnify" (the rename trap; default 1.25, wired DIRECT).
  p.ScaleFactor = cookParam(c, "Magnify", 1.25f);  // [fork-magnify-rename]
  p.Feather = cookParam(c, "Feather", 1.0f);
  p.Radius  = cookParam(c, "Radius", 0.5f);
  p.GainAndBiasX = cookParam(c, "GainAndBias.x", 0.5f);
  p.GainAndBiasY = cookParam(c, "GainAndBias.y", 0.5f);
  p.FlipEffect = cookParam(c, "FlipEffect", 0.0f);
  // NOTE: Bias is DEAD (unconnected in .t3) — NOT read, NOT bound. [fork-dead-bias]

  // b1 Resolution
  BubbleZoomResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // Pull the gradient (gathered input, or the traced white→blue fallback when unwired).
  const SwGradient& g = (c.inputGradients && !c.inputGradients->empty())
                            ? (*c.inputGradients)[0]
                            : defaultBubbleZoomGradient();
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
  enc->setFragmentSamplerState(samp0, 0);                          // s0 texSampler (Clamp)
  enc->setFragmentSamplerState(samp1, 1);                          // s1 clampedSampler (ClampToEdge)
  enc->setFragmentBytes(&p,   sizeof(BubbleZoomParams),     BUBBLEZOOM_Params);
  enc->setFragmentBytes(&res, sizeof(BubbleZoomResolution), BUBBLEZOOM_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp0->release();
  samp1->release();
  gradTex->release();
  if (dummyTex) dummyTex->release();
}

// ======================== --selftest-bubblezoom: DefineGradient → BubbleZoom ========================
// CLOSED-FORM pixel golden for the Gradient->t1 binding on the BubbleZoom FX consumer. A DefineGradient
// producer set to a NON-DEFAULT RED→GREEN gradient is wired into BubbleZoom's Gradient input; Image is
// unwired (1×1 transparent-black dummy → orgColor=(0,0,0,0)). With FlipEffect=0, GainAndBias=(0.5,0.5)
// (identity), Center=(0,0), Feather=1.0 and Radius=1.0 (NON-default: at the default Radius=0.5 the
// feathered distance saturates to 1 everywhere → no spatial variation; Radius=1.0 makes adjustedRadius
// cancel the feather offset so c = saturate(2*|p|), a clean radial ramp), each pixel's gradient param is
// dBiased = saturate(2*|p|) and the output is (g.sample(dBiased).rgb * a, 0). For the opaque red→green
// gradient a=1 → output rgb = g.sample(dBiased).rgb, output a = orgColor.a = 0.
//
// ★R-2 HARDENING (the reason this golden uses RED→GREEN, NOT black→white): if the resident Gradient
// wire were cut, the cook falls to defaultBubbleZoomGradient() (white A=0 → blue A=0.03). That fallback
// is NEARLY TRANSPARENT (a≈0..0.03), so output.rgb = lerp(0, fallback.rgb, a≈0) ≈ BLACK at every pixel.
// With a black→white *test* gradient the cut would look similar (dark); with RED→GREEN the wired output
// is bright red/green (G clearly > 0) — impossible under the near-transparent fallback (rgb≈0). The
// resident wire is teeth-guarded by a green-dominance assertion at the off-center pin.
//
// RED bite: gradientInjectBug() corrupts the REAL DefineGradient cook (drops a stop) so the rasterized
// row diverges → the pins diverge from the UN-corrupted host red→green reference (no co-condition
// tautology — the host wants are FIXED constants, NOT switched by the bug flag). Run on BOTH flat
// (PointGraph::cook) AND resident (cookResident) — R-2 iron rule.

bool nearf(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
bool near4(simd::float4 a, simd::float4 b, float eps = 1e-3f) {
  return nearf(a.x, b.x, eps) && nearf(a.y, b.y, eps) && nearf(a.z, b.z, eps) && nearf(a.w, b.w, eps);
}

// The non-default wired gradient the golden builds: RED (1,0,0,1) @0 → GREEN (0,1,0,1) @1, Linear.
// Host reference (UN-corrupted): sample(t) = (1-t, t, 0, 1). This is the resident-wire teeth-guard:
// distinct from the near-transparent white→blue fallback (rgb≈0 after the alpha-lerp) in the G channel.
SwGradient redGreenGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1.0f, 0.0f, 0.0f, 1.0f)});  // red
  g.steps.push_back({1.0f, simd::make_float4(0.0f, 1.0f, 0.0f, 1.0f)});  // green
  return g;
}

// --- Verbatim host replication of BubbleZoom.hlsl psMain's gradient-param math for one pixel, with the
//     golden's params (Center=0, Feather=1, Radius=1, GainAndBias=(0.5,0.5), FlipEffect=0). Returns the
//     gradient-sample t (= dBiased). aspectRatio=1 (square). ApplyGainAndBias(.,(0.5,0.5)) is identity.
float bzGetBias(float bias, float x) { return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f); }
float bzGetSchlickBias(float g, float x) {
  if (x < 0.5f) { x *= 2.0f; x = 0.5f * bzGetBias(g, x); }
  else { x = 2.0f * x - 1.0f; x = 0.5f * bzGetBias(1.0f - g, x) + 0.5f; }
  return x;
}
float bzApplyGainAndBias(float value, float gx, float gy) {  // bias-functions.hlsl scalar
  float g = std::min(std::max(gx, 0.0f), 1.0f), b = std::min(std::max(gy, 0.0f), 1.0f);
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) { value = bzGetBias(b, value); value = bzGetSchlickBias(g, value); }
  else { value = bzGetSchlickBias(g, value); value = bzGetBias(b, value); }
  return value;
}
// The fragment receives texCoord ≈ ((px+0.5)/W, (py+0.5)/H) at pixel center (square: aspectRatio=1).
// BubbleZoom.hlsl :39-52 with Center=(0,0), Feather=1, Radius=1, FlipEffect=0:
//   p = uv - 0.5; p.x *= aspect(=1)
//   c = distance(p, 0)*2 = 2*|p|
//   adjustedRadius = 2*Radius*aspect = 2
//   c += -adjustedRadius + 2*abs(Feather)/aspect = -2 + 2 = 0  → c = 2*|p|
//   c = saturate(c / Feather) = saturate(2*|p|)
//   dBiased = ApplyGainAndBias(c,(0.5,0.5)) = c   (identity)
float shaderT(int px, int py, int W, int H) {
  float uvx = (px + 0.5f) / W, uvy = (py + 0.5f) / H;
  float aspect = (float)W / (float)H;          // square → 1
  float pxc = (uvx - 0.5f) * aspect;
  float pyc = (uvy - 0.5f);
  float r = std::sqrt(pxc * pxc + pyc * pyc);
  float adjustedRadius = 2.0f * 1.0f * aspect;            // Radius=1
  float c = r * 2.0f - adjustedRadius + 2.0f * std::fabs(1.0f) / aspect;  // Feather=1
  c = std::min(std::max(c / 1.0f, 0.0f), 1.0f);           // saturate(c/Feather)
  float dBiased = bzApplyGainAndBias(c, 0.5f, 0.5f);      // FlipEffect=0 → no flip lerp
  return dBiased;
}

// Build: node 30 = BubbleZoom (Gradient input = port index 1); node 1 = DefineGradient set RED→GREEN.
// Image unwired → dummy ImageA. Radius=1.0 (NON-default), all else .t3 default.
void buildBubbleZoomGraph(Graph& g) {
  Node bz; bz.id = 30; bz.type = "BubbleZoom";
  bz.params["Center.x"] = 0.0f; bz.params["Center.y"] = 0.0f;
  bz.params["Magnify"] = 1.25f;        // .t3 default
  bz.params["Feather"] = 1.0f;         // .t3 default
  bz.params["Radius"] = 1.0f;          // NON-default (golden ramp; .t3 default 0.5 saturates flat)
  bz.params["GainAndBias.x"] = 0.5f; bz.params["GainAndBias.y"] = 0.5f;  // .t3 default → identity
  bz.params["FlipEffect"] = 0.0f;      // .t3 default
  bz.params["Resolution"] = 4.0f; bz.params["CustomW"] = 64.0f; bz.params["CustomH"] = 64.0f;  // Custom 64×64
  g.nodes.push_back(bz);

  Node dg; dg.id = 1; dg.type = "DefineGradient";
  // ★NON-DEFAULT gradient (the R-2 teeth-guard): Color1 = red @0, Color2 = green @1.
  dg.params["Color1.x"] = 1.0f; dg.params["Color1.y"] = 0.0f; dg.params["Color1.z"] = 0.0f; dg.params["Color1.w"] = 1.0f;
  dg.params["Color1Pos"] = 0.0f;
  dg.params["Color2.x"] = 0.0f; dg.params["Color2.y"] = 1.0f; dg.params["Color2.z"] = 0.0f; dg.params["Color2.w"] = 1.0f;
  dg.params["Color2Pos"] = 1.0f;
  dg.params["Interpolation"] = 0.0f;  // Linear
  g.nodes.push_back(dg);

  // DefineGradient out = port index 21 (16 color comps + 4 pos + 1 interp = ports 0..20; out=21).
  // BubbleZoom Gradient = port index 1 (Image=0, Gradient=1, out=2, ...).
  const int dgOutPort = 21;
  g.connections.push_back({700, pinId(1, dgOutPort), pinId(30, /*Gradient*/ 1)});
}

// Read pixel (px,py) RGBA8 and assert it ≈ (ref.sample(shaderT(px,py)).rgb, 0) — the alpha is forced to
// 0 by the transparent-black dummy ImageA (orgColor.a). Two pins (a center pin near red, an off-center
// pin green-dominant), a load-bearing distinctness, and a green-dominance teeth-guard against the
// near-transparent white→blue fallback (proves the wired gradient actually carries through).
bool checkBzPixels(MTL::Texture* tex, const SwGradient& ref, const char* tag) {
  if (!tex) { std::printf("[selftest-bubblezoom] %s FAIL: null tex\n", tag); return false; }
  const uint32_t W = (uint32_t)tex->width(), H = (uint32_t)tex->height();
  if (W != 64 || H != 64) {
    std::printf("[selftest-bubblezoom] %s FAIL: dims=%ux%u want 64x64\n", tag, W, H);
    return false;
  }
  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  auto rd = [&](int x, int y, int ch) { return px[((size_t)y * W + x) * 4 + ch] / 255.0f; };
  bool ok = true;
  // PIN A: center (32,32) → r≈0.011 → c≈0.022 → dBiased≈0.022 → nearly RED (0.978, 0.022, 0).
  // PIN B: off-center (56,32) → r≈0.383 → c≈0.766 → dBiased≈0.766 → GREEN-dominant (0.234, 0.766, 0).
  const int pins[2][2] = {{32, 32}, {56, 32}};
  for (int k = 0; k < 2; ++k) {
    int x = pins[k][0], y = pins[k][1];
    float t = shaderT(x, y, (int)W, (int)H);
    simd::float4 grgb = ref.sample(t);
    // Output alpha = orgColor.a = 0 (transparent-black dummy); rgb = gradient.rgb (a=1 → no scaling).
    simd::float4 want = simd::make_float4(grgb.x, grgb.y, grgb.z, 0.0f);
    simd::float4 got = simd::make_float4(rd(x, y, 0), rd(x, y, 1), rd(x, y, 2), rd(x, y, 3));
    // RGBA8 readback → ~1/255 quantization; tolerate 3/255 ≈ 0.012.
    if (!near4(got, want, 0.012f)) {
      std::printf("[selftest-bubblezoom] %s pin%c (%d,%d) t=%.3f got=(%.3f,%.3f,%.3f,%.3f) "
                  "want=(%.3f,%.3f,%.3f,%.3f) FAIL\n",
                  tag, 'A' + k, x, y, t, got.x, got.y, got.z, got.w, want.x, want.y, want.z, want.w);
      ok = false;
    }
  }
  // Load-bearing distinctness: pin A (t≈0.022) and pin B (t≈0.766) must be DIFFERENT colors (proves the
  // feathered radial param actually varies across radius, not a flat constant — guards stuck-gradient).
  {
    float tA = shaderT(pins[0][0], pins[0][1], (int)W, (int)H);
    float tB = shaderT(pins[1][0], pins[1][1], (int)W, (int)H);
    if (near4(ref.sample(tA), ref.sample(tB), 0.05f)) {
      std::printf("[selftest-bubblezoom] %s pins not distinct (tA=%.3f tB=%.3f) FAIL\n", tag, tA, tB);
      ok = false;
    }
  }
  // ★Teeth-guard for the RED→GREEN wire (R-2): the wired gradient's G channel must be NON-trivial. With
  // the near-transparent white→blue fallback (a≈0..0.03), output.rgb = lerp(0, rgb, a) ≈ BLACK at every
  // pixel → G≈0. Assert pin B's G clearly EXCEEDS its R (green-dominant) AND exceeds 0.3 — impossible
  // under the near-transparent fallback. This is what makes the resident-wire-cut bite.
  {
    int x = pins[1][0], y = pins[1][1];
    float gCh = rd(x, y, 1), rCh = rd(x, y, 0);
    if (!(gCh > rCh + 0.2f && gCh > 0.3f)) {
      std::printf("[selftest-bubblezoom] %s green-dominance teeth-guard FAIL (G=%.3f R=%.3f) — "
                  "gradient looks black/gray (white→blue near-transparent fallback?)\n", tag, gCh, rCh);
      ok = false;
    }
  }
  return ok;
}

int runBubbleZoomSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-bubblezoom] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  PointGraph pg(dev, lib, q, 64, 64);

  // Host reference = the UN-corrupted RED→GREEN gradient (the bug corrupts the COOK, not this ref).
  SwGradient ref = redGreenGradient();

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  Graph g;
  buildBubbleZoomGraph(g);

  // --- FLAT cook --- (PointGraph::cook). injectBug corrupts the REAL DefineGradient cook → the
  // rasterized row diverges → the pins diverge from the un-corrupted red→green ref → exit 1.
  gradientInjectBug() = injectBug;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/30);
  gradientInjectBug() = false;
  bool flatOk = checkBzPixels(pg.target(), ref, injectBug ? "flat(bug)" : "flat");

  // --- RESIDENT (production) cook --- proves the Gradient→t1 wire is LIVE on cookResident (R-2 rule),
  // and (with the RED→GREEN gradient) that a CUT wire would fall to the near-transparent white→blue
  // fallback and bite the green-dominance teeth-guard. Skipped in -bug mode (the flat tooth already bit).
  bool resOk = true;
  if (!injectBug) {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rgr = buildEvalGraph(slib, "Root");
    pg.cookResident(rgr, ctx, /*reg=*/nullptr, /*targetPath=*/"30");
    resOk = checkBzPixels(pg.target(), ref, "resident");
  }

  bool ok = flatOk && resOk;
  if (!injectBug && ok)
    std::printf("[selftest-bubblezoom] flat+resident 64x64 Gradient→t1 (red→green) pixel match\n");

  lib->release();
  q->release();
  dev->release();
  pool->release();
  std::printf("[selftest-bubblezoom] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace

// Self-registration. File-scope static feeds imageFilterSpecSink() + texReg() + imageFilterSelfTests()
// during pre-main dynamic init. No shared file edited (point_ops*.cpp glob picks this up). The selftest
// pair (bubblezoom / bubblezoom-bug) registers HERE via the ImageFilterOp trailing args —
// gradient_golden.cpp and selftests.cpp are NOT touched.
static const ImageFilterOp _reg_bubblezoom{
    // BubbleZoom (TiXL Lib.image.fx.distort.BubbleZoom): magnifying-bubble distortion FX.
    // Gradient input (the t1 binding) + mandatory Image input (t0) → Texture2D out.
    {"BubbleZoom", "BubbleZoom",
     {// Image input (TiXL t3 default null; FX → bound to a transparent-black dummy when unwired).
      {"Image", "Image", "Texture2D", true},
      // Gradient input (the t1 binding). Unwired → traced white→blue (A=0..0.03) fallback.
      {"Gradient", "Gradient", "Gradient", true},
      {"out", "out", "Texture2D", false},
      // Center (Vec2, TiXL t3 default (0,0))
      {"Center.x", "Center", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Center.y", "Center.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Magnify (Single, TiXL t3 default 1.25) — HLSL b0 field name is "ScaleFactor" [fork-magnify-rename]
      {"Magnify", "Magnify", "Float", true, 1.25f, 0.01f, 8.0f, Widget::Slider},
      // Feather (Single, TiXL t3 default 1.0)
      {"Feather", "Feather", "Float", true, 1.0f, 0.001f, 4.0f, Widget::Slider},
      // Radius (Single, TiXL t3 default 0.5)
      {"Radius", "Radius", "Float", true, 0.5f, 0.0f, 4.0f, Widget::Slider},
      // GainAndBias (Vec2, TiXL t3 default (0.5,0.5))
      {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // FlipEffect (Single, TiXL t3 default 0.0; lerp dBiased↔1-dBiased)
      {"FlipEffect", "FlipEffect", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider},
      // NOTE: Bias (e5a5d0cf) is DEAD — unconnected in the .t3, omitted from the spec. [fork-dead-bias]
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "BubbleZoom", cookBubbleZoom, "bubblezoom", runBubbleZoomSelfTest};

}  // namespace sw
