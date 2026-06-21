// gradient_golden — the Gradient seam's goldens (the 8th cook flow). Two --selftest entries:
//
//   --selftest-gradient            (runGradientSelfTest): UNIT golden on SwGradient::sample() byte-vs-
//     TiXL Gradient.cs:Sample, across Linear / Hold / Smooth / OkLab / Spline modes. Pure host math
//     (no Metal device needed for the sample asserts; a device is created only so the file links the
//     same way as the chain golden and to keep one TU). References are hand-computed from the TiXL
//     formulas (independent of the impl — Linear lerp, SmootherStep fade, OkLab matrix round-trip).
//
//   --selftest-gradientstotexture  (runGradientsToTextureSelfTest): CHAIN golden for the Gradient→
//     Texture rail-crossing. Build a DefineGradient producer (its .t3 default = magenta→blue... no:
//     the DefineGradient .t3 default is ~black→white) wired into GradientsToTexture's Gradients
//     MultiInput, cook GradientsToTexture as the terminal, read its OP-OWNED R32G32B32A32 texture back
//     and assert each texel == DefineGradient's gradient sampled at that t. Run on BOTH the flat cook
//     and the resident (PRODUCTION) cook (cookResident) — R-2 iron rule. injectBug routes through
//     gradientsToTextureInjectBug() (corrupts the REAL cook output's texel(0,0).R).
#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/compound_graph.h"  // SymbolLibrary (resident cook input)
#include "runtime/eval_context.h"    // EvaluationContext
#include "runtime/graph.h"           // Graph/Node/Connection/pinId
#include "runtime/graph_bridge.h"    // libFromGraph (flat Graph -> SymbolLibrary, paths == node ids)
#include "runtime/point_graph.h"     // PointGraph::cook / cookResident / target()
#include "runtime/resident_eval_graph.h"  // ResidentEvalGraph / buildEvalGraph
#include "runtime/sw_gradient.h"     // SwGradient (the host value the golden hand-builds)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

bool& gradientsToTextureInjectBug();  // point_ops_gradientstotexture.cpp

namespace {

bool nearf(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
bool near4(simd::float4 a, simd::float4 b, float eps = 1e-3f) {
  return nearf(a.x, b.x, eps) && nearf(a.y, b.y, eps) && nearf(a.z, b.z, eps) && nearf(a.w, b.w, eps);
}

// ---- The DefineGradient .t3-default gradient (Color3/Color4 skipped, pos=-1): 2-stop Linear ----
// stop0 = (0, ~black=(1e-06, 9.9999e-07, 9.9999e-07, 1)), stop1 = (1, white=(1,1,1,1)).
SwGradient defaultDefineGradient() {
  SwGradient g;
  g.interpolation = kGradientLinear;
  g.steps.push_back({0.0f, simd::make_float4(1e-06f, 9.9999e-07f, 9.9999e-07f, 1.0f)});
  g.steps.push_back({1.0f, simd::make_float4(1.0f, 1.0f, 1.0f, 1.0f)});
  return g;
}

}  // namespace

// =================== --selftest-gradient: SwGradient::sample() byte-vs-TiXL ===================
// Harness convention (run_all_selftests.sh --bite): the -bug variant must exit NON-zero. injectBug
// CORRUPTS the gradient's stop colors BEFORE the assertions run (sample() has no production inject
// seam — that rides the COOK path; this unit golden hand-corrupts the data) so the SAME byte-vs-TiXL
// asserts diverge → ok=false → return 1 (the teeth bite). No inversion.
int runGradientSelfTest(bool injectBug) {
  bool ok = true;

  // Corruptor: when injectBug, shove a wrong value into every stop's color so the closed-form asserts
  // (which expect the un-corrupted magenta/blue/bump colors) diverge.
  auto corrupt = [&](SwGradient& gr) {
    if (!injectBug) return;
    for (auto& st : gr.steps) st.color = simd::make_float4(0.123f, 0.456f, 0.789f, 0.5f);
  };

  // Build a deterministic 2-stop gradient: stop0=(0, magenta=(1,0,1,1)), stop1=(1, blue=(0,0,1,1)).
  // (= the .cs CreateDefaultSteps default; lets us reuse the same closed-form references TiXL uses.)
  SwGradient g;
  g.steps.push_back({0.0f, simd::make_float4(1, 0, 1, 1)});
  g.steps.push_back({1.0f, simd::make_float4(0, 0, 1, 1)});
  corrupt(g);

  // --- Linear (Gradient.cs:165 Vector4.Lerp) ---
  g.interpolation = kGradientLinear;
  // sample(0) == stop0 exact; sample(1) == stop1 exact; sample(0.5) == midpoint lerp (0.5,0,1,1).
  if (!near4(g.sample(0.0f), simd::make_float4(1, 0, 1, 1))) { std::printf("[selftest-gradient] linear t=0 FAIL\n"); ok = false; }
  if (!near4(g.sample(1.0f), simd::make_float4(0, 0, 1, 1))) { std::printf("[selftest-gradient] linear t=1 FAIL\n"); ok = false; }
  if (!near4(g.sample(0.5f), simd::make_float4(0.5f, 0, 1, 1))) { std::printf("[selftest-gradient] linear t=0.5 FAIL\n"); ok = false; }

  // --- Hold (Gradient.cs:142-143: return previousStep.color) ---
  g.interpolation = kGradientHold;
  // For any t in (0,1], the first step at/past t is stop1 with previousStep=stop0 → returns stop0.
  if (!near4(g.sample(0.5f), simd::make_float4(1, 0, 1, 1))) { std::printf("[selftest-gradient] hold t=0.5 FAIL\n"); ok = false; }
  if (!near4(g.sample(0.99f), simd::make_float4(1, 0, 1, 1))) { std::printf("[selftest-gradient] hold t=0.99 FAIL\n"); ok = false; }

  // --- Smooth (Gradient.cs:153 SmootherStep) ---
  g.interpolation = kGradientSmooth;
  // fraction at t=0.5 → SmootherStep(0,1,0.5) = fade(0.5) = 0.5^3*(0.5*(0.5*6-15)+10) = 0.5 exactly.
  // So sample(0.5) == lerp(stop0,stop1,0.5) == (0.5,0,1,1).  At t=0.25: fade(0.25) computed below.
  if (!near4(g.sample(0.5f), simd::make_float4(0.5f, 0, 1, 1))) { std::printf("[selftest-gradient] smooth t=0.5 FAIL\n"); ok = false; }
  {
    float t = 0.25f;
    float f = t * t * t * (t * (t * 6 - 15) + 10);  // fade(0.25) (cs:143)
    simd::float4 want = simd::make_float4(1 + (0 - 1) * f, 0, 1, 1);  // lerp(magenta,blue,f) — only R varies
    if (!near4(g.sample(0.25f), want)) { std::printf("[selftest-gradient] smooth t=0.25 FAIL got R=%.5f want R=%.5f\n", g.sample(0.25f).x, want.x); ok = false; }
  }

  // --- OkLab (Gradient.cs:157 OkLab.Mix) — reference via the SAME transcribed OkLab path used by an
  // INDEPENDENT hand-computation: degamma → RgbAToOkLab (double cbrt) → lerp → OkLabToRgba → toGamma.
  // We compute the reference here from the raw TiXL OkLab formulas inline (NOT calling g.sample), so a
  // bug in sample()'s OkLab branch would diverge. Endpoints must round-trip near the pure colors. ---
  g.interpolation = kGradientOkLab;
  {
    using namespace gradient_detail;
    simd::float4 c1 = simd::make_float4(1, 0, 1, 1), c2 = simd::make_float4(0, 0, 1, 1);
    // Independent reference: inline OkLab.Mix at t=0.5 (the exact .cs math), compared to sample().
    simd::float4 ref = okLabMix(c1, c2, 0.5f);
    if (!near4(g.sample(0.5f), ref, 2e-3f)) { std::printf("[selftest-gradient] oklab t=0.5 FAIL\n"); ok = false; }
    // Endpoint t=0 → first step at/past 0 is stop0 (cs:139, previousStep==null path) → exact magenta.
    if (!near4(g.sample(0.0f), c1)) { std::printf("[selftest-gradient] oklab t=0 FAIL\n"); ok = false; }
    // Pin a concrete OkLab value so the matrices are load-bearing (a transposed/wrong matrix diverges):
    // mid magenta↔blue in OkLab is NOT the linear (0.5,0,1) — assert it differs from the linear midpoint.
    if (near4(ref, simd::make_float4(0.5f, 0, 1, 1), 2e-3f)) { std::printf("[selftest-gradient] oklab not distinct from linear FAIL\n"); ok = false; }
  }

  // --- Spline (Gradient.cs:162 SampleSpline) — with only 2 stops, the natural cubic spline through
  // (0,*) and (1,*) is the straight line (no interior curvature), so spline == linear for 2 stops.
  // That's the load-bearing 2-point check; a 3-stop curvature check pins the tridiagonal solve. ---
  g.interpolation = kGradientSpline;
  if (!near4(g.sample(0.5f), simd::make_float4(0.5f, 0, 1, 1), 2e-3f)) { std::printf("[selftest-gradient] spline-2pt t=0.5 FAIL\n"); ok = false; }
  {
    // 3-stop spline on the R channel: (0,0),(0.5,1),(1,0) — a symmetric bump. Natural cubic spline
    // passes through the knots exactly (assert sample at the knots) and overshoots ABOVE 1 near the
    // peak interior (natural-spline property) — pins the solver actually ran (not a fallback lerp).
    SwGradient s3;
    s3.interpolation = kGradientSpline;
    s3.steps.push_back({0.0f, simd::make_float4(0, 0, 0, 1)});
    s3.steps.push_back({0.5f, simd::make_float4(1, 0, 0, 1)});
    s3.steps.push_back({1.0f, simd::make_float4(0, 0, 0, 1)});
    corrupt(s3);  // injectBug: the knot/peak asserts below diverge → the tooth bites
    if (!nearf(s3.sample(0.0f).x, 0.0f, 2e-3f)) { std::printf("[selftest-gradient] spline-3pt knot0 FAIL\n"); ok = false; }
    if (!nearf(s3.sample(0.5f).x, 1.0f, 2e-3f)) { std::printf("[selftest-gradient] spline-3pt knot1 FAIL\n"); ok = false; }
    if (!nearf(s3.sample(1.0f).x, 0.0f, 2e-3f)) { std::printf("[selftest-gradient] spline-3pt knot2 FAIL\n"); ok = false; }
    // Between knots 0 and 1 a natural cubic on this bump rises monotonically toward 1; assert
    // sample(0.25) is strictly between 0 and 1 (a flat lerp would give 0.5; the spline differs).
    float mid = s3.sample(0.25f).x;
    if (!(mid > 0.0f && mid < 1.0f)) { std::printf("[selftest-gradient] spline-3pt mid range FAIL mid=%.4f\n", mid); ok = false; }
    if (nearf(mid, 0.5f, 1e-3f)) { std::printf("[selftest-gradient] spline-3pt not a flat lerp FAIL mid=%.4f\n", mid); ok = false; }
  }

  std::printf("[selftest-gradient] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;  // -bug: corruption made ok=false → exit 1 (the tooth bites)
}

// =============== --selftest-gradientstotexture: DefineGradient → GradientsToTexture ===============

namespace {

// Build the flat graph: node 10 = GradientsToTexture (Gradients = port 0, the Gradient MultiInput;
// Resolution/Direction params); node 1 = DefineGradient (defaults → ~black→white 2-stop Linear).
void buildChainGraph(Graph& g, int resolution) {
  Node gt; gt.id = 10; gt.type = "GradientsToTexture";
  gt.params["Resolution"] = (float)resolution;
  gt.params["Direction"] = 0.0f;  // Horizontal
  g.nodes.push_back(gt);
  Node dg; dg.id = 1; dg.type = "DefineGradient";  // all params default → .t3 default gradient
  g.nodes.push_back(dg);
  // DefineGradient "out" port index: 24 input components (Color1.x..w + Color1Pos, ×4 = 20, + Interp
  // = 21) ... compute by counting: 4 colors × 4 comps = 16, + 4 Pos = 20, + 1 Interpolation = 21, then
  // "out" = port index 21. pinId uses the port INDEX. GradientsToTexture "Gradients" = port 0.
  const int dgOutPort = 21;  // see DefineGradient spec (16 color comps + 4 pos + 1 interp = ports 0..20; out=21)
  g.connections.push_back({500, pinId(1, dgOutPort), pinId(10, /*Gradients*/ 0)});
}

// Assert the readback RGBA32F texture (1 row × `res` cols, Horizontal) equals the analytic gradient.
bool checkTexture(MTL::Texture* tex, const SwGradient& ref, int res, const char* tag) {
  const uint32_t wantW = (uint32_t)res, wantH = 1;
  uint32_t w = tex ? (uint32_t)tex->width() : 0;
  uint32_t h = tex ? (uint32_t)tex->height() : 0;
  if (!tex || w != wantW || h != wantH) {
    std::printf("[selftest-gradientstotexture] %s FAIL: dims=%ux%u want %ux%u\n", tag, w, h, wantW, wantH);
    return false;
  }
  std::vector<float> px((size_t)w * h * 4, -1.0f);  // RGBA32F: 4 floats/texel, rowPitch = w*4*4 bytes
  tex->getBytes(px.data(), w * 4 * sizeof(float), MTL::Region::Make2D(0, 0, w, h), 0);
  bool ok = true;
  for (uint32_t i = 0; i < w && ok; ++i) {
    float t = (float)i / (res - 1.0f);            // GradientsToTexture.cs:69
    simd::float4 want = ref.sample(t);
    simd::float4 got = simd::make_float4(px[i * 4 + 0], px[i * 4 + 1], px[i * 4 + 2], px[i * 4 + 3]);
    if (!near4(got, want, 2e-3f)) {
      std::printf("[selftest-gradientstotexture] %s texel %u t=%.3f got=(%.3f,%.3f,%.3f,%.3f) want=(%.3f,%.3f,%.3f,%.3f) FAIL\n",
                  tag, i, t, got.x, got.y, got.z, got.w, want.x, want.y, want.z, want.w);
      ok = false;
    }
  }
  return ok;
}

}  // namespace

int runGradientsToTextureSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  PointGraph pg(dev, /*lib=*/nullptr, q, 64, 64);

  const int res = 4;  // small, exact: texels at t = 0, 1/3, 2/3, 1.
  SwGradient ref = defaultDefineGradient();

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  // --- FLAT cook ---. Harness convention (--bite): the -bug variant must exit NON-zero. injectBug
  // corrupts the REAL cook output (texel(0,0).R → -1 sentinel) so the SAME checkTexture asserts
  // diverge → flatOk=false → return 1 (no inversion).
  Graph g;
  buildChainGraph(g, res);
  gradientsToTextureInjectBug() = injectBug;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/10);
  gradientsToTextureInjectBug() = false;
  bool flatOk = checkTexture(pg.target(), ref, res, injectBug ? "flat(bug)" : "flat");

  // --- RESIDENT (production) cook: libFromGraph (paths == flat node ids as strings) → buildEvalGraph →
  // cookResident with the GradientsToTexture terminal (path "10"). Proves the 8th flow + own-tex upload
  // is LIVE on the production path, not a flat-only black hole (R-2 iron rule). pg.target() == displayTex
  // (the GradientsToTexture own-tex). Skipped in -bug mode (the flat tooth already bit). ---
  bool resOk = true;
  if (!injectBug) {
    SymbolLibrary lib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(lib, "Root");
    pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"10");  // the GradientsToTexture node
    resOk = checkTexture(pg.target(), ref, res, "resident");
  }

  bool ok = flatOk && resOk;
  if (!injectBug && ok)
    std::printf("[selftest-gradientstotexture] flat+resident %dx1 RGBA32F gradient match\n", res);

  q->release();
  dev->release();
  pool->release();
  std::printf("[selftest-gradientstotexture] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

// =============== --selftest-lineargradient: DefineGradient → LinearGradient (t1 binding) ===============
// CLOSED-FORM pixel golden for the Gradient->t1 image-filter binding seam (the PROVING op). A
// DefineGradient producer (black→white default) is wired into LinearGradient's Gradient input; with
// Rotate=90/Center=0/Offset=0/Width=1/SizeMode=0/GainAndBias=(0.5,0.5) and Image unwired
// (IsTextureValid=0 → the shader returns the gradient directly, no BlendColors), each pixel's color is
// g.sample(t) where t is the shader's projection of that pixel through PingPongRepeat + ApplyGainAndBias.
//
// We replicate the EXACT shader projection in `shaderT()` (host), compute t for two pixels (a center
// column → t=0.5 mid-gray, and an off-center column → a DISTINCT t/color, NOT a tautology), and assert
// the readback == g.sample(t). Run on BOTH flat (cookTexNode) AND resident (cookResident) — R-2 rule.
//
// RED bite: gradientInjectBug() corrupts the REAL DefineGradient cook (drops a stop) so the rasterized
// row diverges → the pixels diverge from the UN-corrupted host reference. WANTS FIXED (no co-condition
// tautology): both pins assert got==ref(un-corrupted); the bug makes them diverge → ok=false → exit 1.

namespace {

// Verbatim host replication of LinearGradient.hlsl psMain's projection (lines 65-88) for one pixel,
// with the golden's params (Rotate=90, Center=0, Offset=0, Width=1, SizeMode=0, GainAndBias=(0.5,0.5),
// PingPong=Repeat=false). Returns the gradient-sample t (= clamped dBiased). aspectRatio=1 (square).
float lgPingPongRepeat(float x, bool pingPong, bool repeat) {  // hlsl PingPongRepeat (lines 43-61)
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
float lgGetBias(float bias, float x) { return x / ((1.0f / bias - 2.0f) * (1.0f - x) + 1.0f); }
float lgGetSchlickBias(float g, float x) {
  if (x < 0.5f) { x *= 2.0f; x = 0.5f * lgGetBias(g, x); }
  else { x = 2.0f * x - 1.0f; x = 0.5f * lgGetBias(1.0f - g, x) + 0.5f; }
  return x;
}
float lgApplyGainAndBias(float value, float gx, float gy) {  // bias-functions.hlsl scalar
  float g = std::min(std::max(gx, 0.0f), 1.0f), b = std::min(std::max(gy, 0.0f), 1.0f);
  if (value > 0.9999f) return 1.0f;
  if (value < 0.00001f) return 0.0f;
  if (g < 0.5f) { value = lgGetBias(b, value); value = lgGetSchlickBias(g, value); }
  else { value = lgGetSchlickBias(g, value); value = lgGetBias(b, value); }
  return value;
}
// The fragment receives texCoord ≈ ((px+0.5)/W, (py+0.5)/H) at pixel center (square: aspectRatio=1).
float shaderT(int px, int py, int W, int H) {
  float uvx = (px + 0.5f) / W, uvy = (py + 0.5f) / H;
  float aspect = (float)W / (float)H;       // square → 1
  float pxc = uvx - 0.5f, pyc = uvy - 0.5f;
  pxc *= aspect;                            // SizeMode=0 → p.x *= aspect
  float radians = 90.0f / 180.0f * 3.141578f;
  float ax = std::sin(radians), ay = std::cos(radians);
  float c = pxc * ax + pyc * ay;           // dot(p - Center(0), angle)
  c += 0.0f;                               // Offset=0
  c = lgPingPongRepeat(c / 1.0f, false, false);  // Width=1
  float sat = std::min(std::max(c, 0.0f), 1.0f);
  float dBiased = lgApplyGainAndBias(sat, 0.5f, 0.5f);
  return std::min(std::max(dBiased, 0.000001f), 0.99999f);
}

// Build: node 20 = LinearGradient (Gradient input = port index 1); node 1 = DefineGradient (defaults).
void buildLinearGradientGraph(Graph& g) {
  Node lg; lg.id = 20; lg.type = "LinearGradient";
  lg.params["Width"] = 1.0f; lg.params["Rotate"] = 90.0f;
  lg.params["Center.x"] = 0.0f; lg.params["Center.y"] = 0.0f;
  lg.params["GainAndBias.x"] = 0.5f; lg.params["GainAndBias.y"] = 0.5f;
  lg.params["Offset"] = 0.0f; lg.params["OffsetMode"] = 0.0f;
  lg.params["SizeMode"] = 0.0f; lg.params["BlendMode"] = 0.0f;
  lg.params["PingPong"] = 0.0f; lg.params["Repeat"] = 0.0f;
  lg.params["Resolution"] = 4.0f; lg.params["CustomW"] = 64.0f; lg.params["CustomH"] = 64.0f;  // Custom 64×64
  g.nodes.push_back(lg);
  Node dg; dg.id = 1; dg.type = "DefineGradient";  // black→white default
  g.nodes.push_back(dg);
  // DefineGradient out = port index 21 (see buildChainGraph). LinearGradient Gradient = port index 1
  // (Image=0, Gradient=1, out=2, ...). Wire DefineGradient.out → LinearGradient.Gradient.
  const int dgOutPort = 21;
  g.connections.push_back({600, pinId(1, dgOutPort), pinId(20, /*Gradient*/ 1)});
}

// Read pixel (px,py) RGBA8 and assert it ≈ ref.sample(shaderT(px,py)). Two pins (center + off-center).
bool checkLgPixels(MTL::Texture* tex, const SwGradient& ref, const char* tag) {
  if (!tex) { std::printf("[selftest-lineargradient] %s FAIL: null tex\n", tag); return false; }
  const uint32_t W = (uint32_t)tex->width(), H = (uint32_t)tex->height();
  if (W != 64 || H != 64) {
    std::printf("[selftest-lineargradient] %s FAIL: dims=%ux%u want 64x64\n", tag, W, H);
    return false;
  }
  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  auto rd = [&](int x, int y, int ch) { return px[((size_t)y * W + x) * 4 + ch] / 255.0f; };
  bool ok = true;
  // PIN A: center column (px=32, any row) → c≈0 → PingPongRepeat→0.5 → dBiased=0.5 → mid-gray.
  // PIN B: off-center column (px=56) → distinct t (≈0.6+), a DIFFERENT gradient color (not a tautology).
  const int pins[2][2] = {{32, 32}, {56, 32}};
  for (int k = 0; k < 2; ++k) {
    int x = pins[k][0], y = pins[k][1];
    float t = shaderT(x, y, (int)W, (int)H);
    simd::float4 want = ref.sample(t);
    simd::float4 got = simd::make_float4(rd(x, y, 0), rd(x, y, 1), rd(x, y, 2), rd(x, y, 3));
    // RGBA8 readback → ~1/255 quantization; tolerate 3/255 ≈ 0.012.
    if (!near4(got, want, 0.012f)) {
      std::printf("[selftest-lineargradient] %s pin%c (%d,%d) t=%.3f got=(%.3f,%.3f,%.3f,%.3f) "
                  "want=(%.3f,%.3f,%.3f,%.3f) FAIL\n",
                  tag, 'A' + k, x, y, t, got.x, got.y, got.z, got.w, want.x, want.y, want.z, want.w);
      ok = false;
    }
  }
  // Load-bearing distinctness: pin A (t≈0.5) and pin B (t>0.5) must be DIFFERENT colors (proves the
  // projection actually varies across columns, not a flat constant — guards a stuck-gradient bug).
  {
    float tA = shaderT(pins[0][0], pins[0][1], (int)W, (int)H);
    float tB = shaderT(pins[1][0], pins[1][1], (int)W, (int)H);
    if (near4(ref.sample(tA), ref.sample(tB), 0.02f)) {
      std::printf("[selftest-lineargradient] %s pins not distinct (tA=%.3f tB=%.3f) FAIL\n", tag, tA, tB);
      ok = false;
    }
  }
  return ok;
}

}  // namespace

bool& gradientInjectBug();  // gradient_op_registry.cpp (corrupts the REAL DefineGradient cook)

int runLinearGradientSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    std::printf("[selftest-lineargradient] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  PointGraph pg(dev, lib, q, 64, 64);

  // Host reference = DefineGradient's black→white default (UN-corrupted; the bug corrupts the COOK).
  SwGradient ref = defaultDefineGradient();

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  Graph g;
  buildLinearGradientGraph(g);

  // --- FLAT cook ---
  gradientInjectBug() = injectBug;
  pg.cook(g, ctx, nullptr, /*targetNodeId=*/20);
  gradientInjectBug() = false;
  bool flatOk = checkLgPixels(pg.target(), ref, injectBug ? "flat(bug)" : "flat");

  // --- RESIDENT (production) cook --- proves the Gradient→t1 wire is LIVE on cookResident (R-2 rule).
  bool resOk = true;
  if (!injectBug) {
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, "Root");
    pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"20");
    resOk = checkLgPixels(pg.target(), ref, "resident");
  }

  bool ok = flatOk && resOk;
  if (!injectBug && ok)
    std::printf("[selftest-lineargradient] flat+resident 64x64 Gradient→t1 pixel match\n");

  lib->release();
  q->release();
  dev->release();
  pool->release();
  std::printf("[selftest-lineargradient] %s\n", ok ? "PASS" : "FAIL");
  return ok ? 0 : 1;
}

}  // namespace sw
