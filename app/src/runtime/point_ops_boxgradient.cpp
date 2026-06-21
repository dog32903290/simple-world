// BoxGradient image generator op — the SECOND consumer of the Gradient->t1 image-filter binding seam
// (the seam was proven LIVE by LinearGradient, commit 0d09bd8; here we ride it, not build it).
//
// TiXL authority: external/tixl/Operators/Lib/image/generate/basic/BoxGradient.{cs,hlsl,t3}.
//   .cs   — slot declarations (Image, Rotation, Center, Size, UniformScale, CornersRadius, Gradient,
//           GradientWidth, Offset, PingPong, Repeat, GainAndBias, BlendMode, Resolution).
//   .t3   — defaults (Size=(0.25,0.25), CornersRadius=0, Rotation=0, UniformScale=1, GradientWidth=1,
//           Offset=0, PingPong=TRUE, Repeat=false, GainAndBias=(0.5,0.5), BlendMode=0, Center=0) +
//           the FloatsToBuffer connection-order cbuffer fill + the Gradient→GradientsToTexture→t1
//           plumbing. ★Traced: the cbuffer field order matches the .hlsl b0 1:1 (validated against
//           the known-good LinearGradient trace), and the CornersRadius float4 is fed in (Z,Y,W,X)
//           component order — replicated as a SCALAR RESHUFFLE here, NOT a cbuffer reorder.
//   .hlsl — psMain (ported VERBATIM to shaders/boxgradient.metal).
//
// Port class: a .t3 compound whose terminal is _multiImageFxSetupStatic → a single fragment shader
//   (boxgradient_vs / boxgradient_fs). RENDER op (NOT compute), same shape as LinearGradient / NGon:
//   cachedTexPSO → renderCommandEncoder → setFragmentTexture/Sampler/Bytes → drawPrimitives triangle.
//
// The Gradient→t1 binding: the op reads c.inputGradients[0], rasterizes it to a 1×512 RGBA row via
//   rasterizeGradientRow (the SAME shared row sampler LinearGradient/GradientsToTexture use — can't
//   drift), and binds it at fragment texture(1) with the s1 clamp sampler.
//
// ★Unwired-Gradient fallback — TRACED from BoxGradient.t3 (NOT the GradientsToTexture child's embedded
//   magenta→blue value): the .t3 wires the op's OWN Gradient input SLOT default (5e7cd523, t3:29-57,
//   white→black 2-stop Linear) INTO the GradientsToTexture child's Gradients slot (588be11f), so an
//   UNWIRED Gradient input feeds the op's slot default into the row; the child's embedded value is
//   overridden by that connection and never reached. Fallback = the op's slot default (white→black).
//   [fork-gradient-default-traced]
//
// ★CornersRadius reshuffle (fork-cornersradius-zywx, see boxgradient_params.h): shader CornersRadius
//   = inputCorners.zywx. Default (0,0,0,0) makes it invisible at default; replicated for non-default.
//
// FORKS (named): generator dummy (1×1 transparent-black ImageA when unwired); gradient-row format
//   RGBA32F (gradient_raster.h fork-grad-row-format-32f); gain/bias + BlendColors inlined in
//   boxgradient.metal; fork-gradient-default-traced + fork-cornersradius-zywx (above).
//
// Self-contained leaf: cookBoxGradient + ImageFilterOp self-registration + runBoxGradientSelfTest (the
// golden lives IN THIS FILE, registered via the ImageFilterOp selftest slot → imageFilterSelfTests()
// sink, the SAME mechanism LinearGradient uses — NO gradient_golden.cpp edit, NO selftests.cpp edit).
// CMake point_ops*.cpp glob + shaders/*.metal glob auto-pick both files — no CMake edit.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/boxgradient_params.h"         // BoxGradientParams/Resolution, BOXGRADIENT_*
#include "runtime/compound_graph.h"             // SymbolLibrary (resident cook input)
#include "runtime/eval_context.h"               // EvaluationContext
#include "runtime/gradient_raster.h"            // rasterizeGradientRow, kGradientRowN
#include "runtime/graph.h"                      // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"               // libFromGraph (flat Graph -> SymbolLibrary)
#include "runtime/image_filter_op_registry.h"   // ImageFilterOp self-registration
#include "runtime/point_graph.h"                // TexCookCtx, cookParam, PointGraph
#include "runtime/resident_eval_graph.h"        // ResidentEvalGraph / buildEvalGraph
#include "runtime/sw_gradient.h"                // SwGradient (the consumed currency)
#include "runtime/tex_op_cache.h"               // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

bool& gradientInjectBug();  // gradient_op_registry.cpp (corrupts the REAL DefineGradient cook)

namespace {

// The unwired-Gradient fallback: the op's Gradient SLOT default (BoxGradient.t3 :29-57), which the
// .t3 connection (:374-378) feeds into the GradientsToTexture child. White→black, 2-stop Linear
// (stop0 = white at t3:37-42, stop1 = ~black at t3:46-52). [fork-gradient-default-traced]
SwGradient defaultBoxGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1.0f, 0.99999f, 1.0f, 1.0f)});            // t3:37-42 (white)
  g.steps.push_back({0.5f, simd::make_float4(0.0f, 1.2159347e-11f, 1e-06f, 1.0f)});    // t3:46-52 (~black @0.5)
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

// BoxGradient texture op: single fullscreen pass. Reads c.inputGradients[0] (the gathered Gradient),
// rasterizes it to a 1×512 row, samples it in the shader at (dBiased, 0). Optionally composites over
// c.inputTexture (Image). Always writes c.output.
void cookBoxGradient(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "boxgradient_vs", "boxgradient_fs", fmt);
  if (!rps) return;

  // s0 texSampler: linear+Wrap (ImageA), matching _multiImageFxSetupStatic.t3 WrapMode=Wrap.
  MTL::SamplerDescriptor* sd0 = MTL::SamplerDescriptor::alloc()->init();
  sd0->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd0->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd0->setSAddressMode(MTL::SamplerAddressModeRepeat);
  sd0->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp0 = c.dev->newSamplerState(sd0);
  sd0->release();

  // ★s1 clammpedSampler (TiXL typo preserved): linear+ClampToEdge (the gradient row). MANDATORY — the
  // row is sampled at v=0 with the gradient value at u=dBiased; a Wrap sampler would corrupt the edges.
  MTL::SamplerDescriptor* sd1 = MTL::SamplerDescriptor::alloc()->init();
  sd1->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd1->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd1->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd1->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp1 = c.dev->newSamplerState(sd1);
  sd1->release();

  // --- b0 params (BoxGradient.cs/.t3 defaults; cbuffer field order == BoxGradient.hlsl b0) ---
  BoxGradientParams p{};
  p.CenterX = cookParam(c, "Center.x", 0.0f);
  p.CenterY = cookParam(c, "Center.y", 0.0f);
  p.SizeX   = cookParam(c, "Size.x", 0.25f);
  p.SizeY   = cookParam(c, "Size.y", 0.25f);

  // ★CornersRadius reshuffle (fork-cornersradius-zywx): shader float4 = inputCorners.zywx.
  const float crX = cookParam(c, "CornersRadius.x", 0.0f);
  const float crY = cookParam(c, "CornersRadius.y", 0.0f);
  const float crZ = cookParam(c, "CornersRadius.z", 0.0f);
  const float crW = cookParam(c, "CornersRadius.w", 0.0f);
  p.CornersRadiusX = crZ;  // b0[4] <- input Z
  p.CornersRadiusY = crY;  // b0[5] <- input Y
  p.CornersRadiusZ = crW;  // b0[6] <- input W
  p.CornersRadiusW = crX;  // b0[7] <- input X

  p.Rotation     = cookParam(c, "Rotation", 0.0f);
  p.UniformScale = cookParam(c, "UniformScale", 1.0f);
  p.Width        = cookParam(c, "GradientWidth", 1.0f);  // hlsl Width <- GradientWidth input
  p.Offset       = cookParam(c, "Offset", 0.0f);          // DIRECT (no PickFloat routing)
  p.PingPong     = cookParam(c, "PingPong", 1.0f);        // ★TiXL t3 default TRUE
  p.Repeat       = cookParam(c, "Repeat", 0.0f);
  p.GainAndBiasX = cookParam(c, "GainAndBias.x", 0.5f);
  p.GainAndBiasY = cookParam(c, "GainAndBias.y", 0.5f);
  p.BlendMode    = cookParam(c, "BlendMode", 0.0f);       // 0 = Normal

  // IsTextureValid: 1.0 if Image wired, else 0.0 (generator mode → return gradient).
  p.IsTextureValid = (c.inputTexture != nullptr) ? 1.0f : 0.0f;

  // b1 Resolution
  BoxGradientResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // Pull the gradient (gathered input, or the traced white→black fallback when unwired).
  const SwGradient& g = (c.inputGradients && !c.inputGradients->empty())
                            ? (*c.inputGradients)[0]
                            : defaultBoxGradient();
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
  enc->setFragmentSamplerState(samp1, 1);                          // s1 clammpedSampler (ClampToEdge)
  enc->setFragmentBytes(&p,   sizeof(BoxGradientParams),     BOXGRADIENT_Params);
  enc->setFragmentBytes(&res, sizeof(BoxGradientResolution), BOXGRADIENT_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp0->release();
  samp1->release();
  gradTex->release();
  if (dummyTex) dummyTex->release();
}

// ===================== --selftest-boxgradient golden (lives IN THIS LEAF) =====================
// CLOSED-FORM pixel golden for the Gradient->t1 binding, ★with a NON-DEFAULT wired gradient
// (red→green) — the R-2 HARDENING. The golden builds a DefineGradient producer set to red(1,0,0,1)@0
// → green(0,1,0,1)@1 and wires it into BoxGradient's Gradient input. With Image unwired
// (IsTextureValid=0 → the shader returns the gradient directly), each pixel's color is g.sample(t)
// where t is the shader's SDF projection through PingPongRepeat + ApplyGainAndBias.
//
// ★Why red→green (not LinearGradient's black→white): if the resident Gradient wire were cut, the cook
//   would fall to defaultBoxGradient() (white→black). The red→green pins would then read white/black
//   instead of red/green → DIVERGE. So the resident wire is TEETH-GUARDED by this golden — a defect
//   LinearGradient's black→white golden could NOT catch (its fallback was also ~black→white, so a
//   cut wire produced near-identical pixels). This golden does not repeat that mistake.
//
// We replicate the EXACT shader SDF projection in shaderT() (host), compute t for two DISTINCT pixels,
// and assert readback == ref.sample(t) for the red→green ref. Run BOTH flat (cookTexNode) AND resident
// (cookResident) — R-2 iron rule.
//
// RED bite: gradientInjectBug() corrupts the REAL DefineGradient cook (drops a stop) so the rasterized
// row diverges → the pixels diverge from the UN-corrupted host ref. No co-conditioning tautology: both
// pins assert got==ref(un-corrupted red→green); the bug makes them diverge → ok=false → exit 1.

bool bgNearf(float a, float b, float eps) { return std::fabs(a - b) < eps; }
bool bgNear4(simd::float4 a, simd::float4 b, float eps) {
  return bgNearf(a.x, b.x, eps) && bgNearf(a.y, b.y, eps) && bgNearf(a.z, b.z, eps) &&
         bgNearf(a.w, b.w, eps);
}

// The non-default wired gradient: red(1,0,0,1) at 0 → green(0,1,0,1) at 1 (2-stop Linear). Used as the
// host reference; built in the graph via DefineGradient Color1/Color2 params below.
SwGradient redGreenGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1.0f, 0.0f, 0.0f, 1.0f)});  // red
  g.steps.push_back({1.0f, simd::make_float4(0.0f, 1.0f, 0.0f, 1.0f)});  // green
  return g;
}

// --- Host replication of BoxGradient.hlsl psMain projection (verbatim, the golden's default SDF) ---
float bgPingPongRepeat(float x, float pingPong, float repeat) {  // hlsl :33-51 (baseValue = x)
  float baseValue = x;
  float repeatValue = x - std::floor(x);  // frac
  float pingPongValue = 1.0f - std::fabs((x * 0.5f - std::floor(x * 0.5f)) * 2.0f - 1.0f);
  float singlePingPong = std::fabs(x);
  float sR = (repeat >= 0.5f) ? 1.0f : 0.0f, sP = (pingPong >= 0.5f) ? 1.0f : 0.0f;
  float pingPongOutput = singlePingPong + (pingPongValue - singlePingPong) * sR;
  float value = baseValue + (repeatValue - baseValue) * sR;
  value = value + (pingPongOutput - value) * sP;
  float sat = std::min(std::max(value, 0.0f), 1.0f);
  value = sat + (value - sat) * sR;
  return value;
}
float bgGetBias(float bias, float x) { return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f); }
float bgGetSchlickBias(float g, float x) {
  if (x < 0.5f) { x *= 2.0f; x = 0.5f * bgGetBias(g, x); }
  else { x = 2.0f * x - 1.0f; x = 0.5f * bgGetBias(1.0f - g, x) + 0.5f; }
  return x;
}
float bgApplyGainAndBias(float value, float gx, float gy) {  // bias-functions.hlsl scalar
  float g = std::min(std::max(gx, 0.0f), 1.0f), b = std::min(std::max(gy, 0.0f), 1.0f);
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) { value = bgGetBias(b, value); value = bgGetSchlickBias(g, value); }
  else { value = bgGetSchlickBias(g, value); value = bgGetBias(b, value); }
  return value;
}
// sdRoundedBox with CornersRadius=0 (default golden) — reduces to the standard box SDF.
float bgSdRoundedBox(float px, float py, float bx, float by) {
  float qx = std::fabs(px) - bx, qy = std::fabs(py) - by;
  float outside = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) +
                            std::max(qy, 0.0f) * std::max(qy, 0.0f));
  return std::min(std::max(qx, qy), 0.0f) + outside;  // r.x = 0
}
// Default golden params: Center=0, Size=(0.25,0.25), CornersRadius=0, Rotation=0, UniformScale=1,
// Width=1, Offset=0, PingPong=1(true), Repeat=0, GainAndBias=(0.5,0.5). aspect=1 (square 64×64).
float shaderT(int px, int py, int W, int H) {
  float uvx = (px + 0.5f) / W, uvy = (py + 0.5f) / H;
  float aspect = (float)W / (float)H;            // square → 1
  float pxc = uvx - 0.5f, pyc = uvy - 0.5f;
  pxc *= aspect;                                  // :90
  pxc += 0.0f; pyc += 0.0f;                       // :91 Center*(-1,1) with Center=0
  // rotatePoint(p, 0): cos=1, sin=0 → (p.x, -p.y)  (:79-80 with angle 0)
  float rpx = pxc, rpy = -pyc;
  float c = bgSdRoundedBox(rpx, rpy, 0.25f, 0.25f) * 2.0f - 0.0f;  // :98 (Offset=0)
  c = bgPingPongRepeat(c / 1.0f, 1.0f, 0.0f);    // :102 Width=1, PingPong=1, Repeat=0
  float dBiased = bgApplyGainAndBias(c, 0.5f, 0.5f);  // :104 (no saturate — fork-no-saturate)
  return std::min(std::max(dBiased, 0.001f), 0.999f);  // :106 [fork-clamp-range]
}

// Build: node 20 = BoxGradient (Gradient input = port index 1); node 1 = DefineGradient set red→green.
void buildBoxGradientGraph(Graph& g) {
  Node bg; bg.id = 20; bg.type = "BoxGradient";
  bg.params["Center.x"] = 0.0f; bg.params["Center.y"] = 0.0f;
  bg.params["Size.x"] = 0.25f;  bg.params["Size.y"] = 0.25f;
  bg.params["CornersRadius.x"] = 0.0f; bg.params["CornersRadius.y"] = 0.0f;
  bg.params["CornersRadius.z"] = 0.0f; bg.params["CornersRadius.w"] = 0.0f;
  bg.params["Rotation"] = 0.0f; bg.params["UniformScale"] = 1.0f;
  bg.params["GradientWidth"] = 1.0f; bg.params["Offset"] = 0.0f;
  bg.params["PingPong"] = 1.0f; bg.params["Repeat"] = 0.0f;       // TiXL default PingPong=true
  bg.params["GainAndBias.x"] = 0.5f; bg.params["GainAndBias.y"] = 0.5f;
  bg.params["BlendMode"] = 0.0f;
  bg.params["Resolution"] = 4.0f; bg.params["CustomW"] = 64.0f; bg.params["CustomH"] = 64.0f;  // Custom 64×64
  g.nodes.push_back(bg);

  // DefineGradient configured to a NON-DEFAULT red→green gradient (the R-2 hardening).
  Node dg; dg.id = 1; dg.type = "DefineGradient";
  dg.params["Color1.x"] = 1.0f; dg.params["Color1.y"] = 0.0f; dg.params["Color1.z"] = 0.0f;
  dg.params["Color1.w"] = 1.0f; dg.params["Color1Pos"] = 0.0f;   // red @ 0
  dg.params["Color2.x"] = 0.0f; dg.params["Color2.y"] = 1.0f; dg.params["Color2.z"] = 0.0f;
  dg.params["Color2.w"] = 1.0f; dg.params["Color2Pos"] = 1.0f;   // green @ 1
  dg.params["Color3Pos"] = -1.0f; dg.params["Color4Pos"] = -1.0f;  // skipped
  dg.params["Interpolation"] = 0.0f;  // Linear
  g.nodes.push_back(dg);

  // DefineGradient out = port index 21 (16 color comps + 4 pos + 1 interp = ports 0..20; out=21).
  // BoxGradient Gradient input = port index 1 (Image=0, Gradient=1, out=2, ...).
  const int dgOutPort = 21;
  g.connections.push_back({700, pinId(1, dgOutPort), pinId(20, /*Gradient*/ 1)});
}

// Read pixel (px,py) RGBA8 and assert it ≈ ref.sample(shaderT(px,py)). Two distinct pins.
bool checkBgPixels(MTL::Texture* tex, const SwGradient& ref, const char* tag) {
  if (!tex) { std::printf("[selftest-boxgradient] %s FAIL: null tex\n", tag); return false; }
  const uint32_t W = (uint32_t)tex->width(), H = (uint32_t)tex->height();
  if (W != 64 || H != 64) {
    std::printf("[selftest-boxgradient] %s FAIL: dims=%ux%u want 64x64\n", tag, W, H);
    return false;
  }
  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  auto rd = [&](int x, int y, int ch) { return px[((size_t)y * W + x) * 4 + ch] / 255.0f; };
  bool ok = true;
  // PIN A: (16,32) — on the box EDGE band, the pingpong triangle wave bottoms out → t≈0.016 → near
  //   the RED endpoint (0.984,0.016,0). (Fallback would be near-white here → 0.97 apart, strong guard.)
  // PIN B: (32,32) — box center, pingpong peak → t≈0.484 → a DISTINCT color (0.516,0.484,0), ~0.47
  //   away from pin A. (Hand-derived in the closed-form sweep; both clear the distinctness teeth.)
  const int pins[2][2] = {{16, 32}, {32, 32}};
  for (int k = 0; k < 2; ++k) {
    int x = pins[k][0], y = pins[k][1];
    float t = shaderT(x, y, (int)W, (int)H);
    simd::float4 want = ref.sample(t);
    simd::float4 got = simd::make_float4(rd(x, y, 0), rd(x, y, 1), rd(x, y, 2), rd(x, y, 3));
    // RGBA8 readback → ~1/255 quantization; tolerate 3/255 ≈ 0.012.
    if (!bgNear4(got, want, 0.012f)) {
      std::printf("[selftest-boxgradient] %s pin%c (%d,%d) t=%.3f got=(%.3f,%.3f,%.3f,%.3f) "
                  "want=(%.3f,%.3f,%.3f,%.3f) FAIL\n",
                  tag, 'A' + k, x, y, t, got.x, got.y, got.z, got.w, want.x, want.y, want.z, want.w);
      ok = false;
    }
  }
  // Load-bearing distinctness: pin A and pin B must be DIFFERENT colors (proves the SDF projection
  // varies across the image, not a flat constant — guards a stuck-gradient bug).
  {
    float tA = shaderT(pins[0][0], pins[0][1], (int)W, (int)H);
    float tB = shaderT(pins[1][0], pins[1][1], (int)W, (int)H);
    if (bgNear4(ref.sample(tA), ref.sample(tB), 0.02f)) {
      std::printf("[selftest-boxgradient] %s pins not distinct (tA=%.3f tB=%.3f) FAIL\n", tag, tA, tB);
      ok = false;
    }
  }
  // ★R-2 resident-wire-cut guard: the wired gradient is red→green; the unwired fallback is white→black.
  // Assert pin A's color is NOT the fallback's sample (proves the live wire fed the cook, not the
  // fallback). If the resident wire were cut, pin A would read the white→black fallback and this bites.
  {
    SwGradient fb = defaultBoxGradient();
    float tA = shaderT(pins[0][0], pins[0][1], (int)W, (int)H);
    simd::float4 got = simd::make_float4(rd(pins[0][0], pins[0][1], 0), rd(pins[0][0], pins[0][1], 1),
                                         rd(pins[0][0], pins[0][1], 2), rd(pins[0][0], pins[0][1], 3));
    if (bgNear4(got, fb.sample(tA), 0.02f)) {
      std::printf("[selftest-boxgradient] %s pinA matches the UNWIRED fallback — resident wire CUT? FAIL\n",
                  tag);
      ok = false;
    }
  }
  return ok;
}

}  // namespace

int runBoxGradientSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-boxgradient] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  PointGraph pg(dev, lib, q, 64, 64);

  // Host reference = the wired red→green gradient (UN-corrupted; the bug corrupts the COOK).
  SwGradient ref = redGreenGradient();

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  Graph g;
  buildBoxGradientGraph(g);

  // --- FLAT cook ---
  gradientInjectBug() = injectBug;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/20);
  gradientInjectBug() = false;
  bool flatOk = checkBgPixels(pg.target(), ref, injectBug ? "flat(bug)" : "flat");

  // --- RESIDENT (production) cook --- proves the Gradient→t1 wire is LIVE on cookResident (R-2 rule).
  bool resOk = true;
  if (!injectBug) {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, "Root");
    pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"20");
    resOk = checkBgPixels(pg.target(), ref, "resident");
  }

  bool ok = flatOk && resOk;
  if (!injectBug && ok)
    std::printf("[selftest-boxgradient] flat+resident 64x64 Gradient→t1 red→green pixel match\n");

  lib->release();
  q->release();
  dev->release();
  pool->release();
  std::printf("[selftest-boxgradient] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// Self-registration. File-scope static feeds imageFilterSpecSink() + texReg() + imageFilterSelfTests()
// during pre-main dynamic init. No shared file edited (point_ops*.cpp glob picks this up). The golden
// rides the ImageFilterOp selftest slot (5th/6th ctor args) → registered as --selftest-boxgradient.
static const ImageFilterOp _reg_boxgradient{
    // BoxGradient (TiXL Lib.image.generate.basic.BoxGradient): rounded-box SDF gradient. Gradient
    // input (the t1 binding) + optional Image input → Texture2D out. When no Image: returns the
    // gradient directly (IsTextureValid=0); when wired: BlendColors composite.
    {"BoxGradient", "BoxGradient",
     {// Optional Image input (TiXL default null — generator mode draws the gradient on its own)
      {"Image", "Image", "Texture2D", true},
      // Gradient input (the t1 binding). Unwired → traced white→black fallback.
      {"Gradient", "Gradient", "Gradient", true},
      {"out", "out", "Texture2D", false},
      // Center (Vec2, TiXL t3 default (0,0))
      {"Center.x", "Center", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Center.y", "Center.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Size (Vec2, TiXL t3 default (0.25,0.25))
      {"Size.x", "Size", "Float", true, 0.25f, 0.0f, 2.0f, Widget::Vec, {}, true, 2},
      {"Size.y", "Size.y", "Float", true, 0.25f, 0.0f, 2.0f, Widget::Vec, {}, true, 1},
      // CornersRadius (Vec4, TiXL t3 default (0,0,0,0))
      {"CornersRadius.x", "CornersRadius", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"CornersRadius.y", "CornersRadius.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"CornersRadius.z", "CornersRadius.z", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"CornersRadius.w", "CornersRadius.w", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Rotation (Single, TiXL t3 default 0.0; degrees)
      {"Rotation", "Rotation", "Float", true, 0.0f, -180.0f, 180.0f},
      // UniformScale (Single, TiXL t3 default 1.0)
      {"UniformScale", "UniformScale", "Float", true, 1.0f, 0.0f, 4.0f, Widget::Slider},
      // GradientWidth (Single, TiXL t3 default 1.0; HLSL field Width)
      {"GradientWidth", "GradientWidth", "Float", true, 1.0f, 0.0f, 4.0f, Widget::Slider},
      // Offset (Single, TiXL t3 default 0.0; DIRECT, no routing)
      {"Offset", "Offset", "Float", true, 0.0f, -4.0f, 4.0f, Widget::Slider},
      // GainAndBias (Vec2, TiXL t3 default (0.5,0.5))
      {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // PingPong / Repeat (bool→float; TiXL t3 default PingPong=TRUE, Repeat=false)
      {"PingPong", "PingPong", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Bool},
      {"Repeat", "Repeat", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool},
      // BlendMode (Int→float; TiXL t3 default 0 = normal composite with upstream image)
      {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 9.0f, Widget::Enum,
       {"Normal", "Screen", "Multiply", "Overlay", "Difference", "SrcOnly",
        "DstOnly", "HardLight", "LinearDodge", "AlphaMask"}},
      // Resolution (standard image-filter enum; TiXL Int2 default (0,0) = WindowFollow)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "BoxGradient", cookBoxGradient, "boxgradient", runBoxGradientSelfTest};

}  // namespace sw
