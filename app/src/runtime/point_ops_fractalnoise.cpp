// FractalNoise image generator op (Phase C leaf).
// TiXL authority: Operators/Lib/image/generate/noise/FractalNoise.cs (slot declarations, types) +
//   FractalNoise.t3 (defaults: ColorA=(1e-6,1e-6,1e-6,1), ColorB=(1,1,1,1), Scale=1, Stretch=(2,2),
//   Offset=(0,0), RandomPhase=5.0, Iterations=2, GainAndBias=(0.5,0.5), WarpXY=(0,0), WarpZ=0.0,
//   Resolution=256×256, GenerateMips=false) +
//   Assets/shaders/img/generate/FractalNoise.hlsl (simplex fBm generator with hash33 hash,
//   noise_sum_abs, ApplyGainAndBias, lerp to ColorA/ColorB).
//
// Port class: _ImageFxShaderSetupStatic (bd0b9c5b) → single-pass fragment shader.
//
// STEP-0 portability check (PASSED):
//   ① Zero Texture2D inputs — FractalNoise.cs has NO Image [Input] slot. Pure generator.
//   ② No gradient-widget, no curve-LUT, no asset-texture, no mip-gen seam.
//   ③ _ImageFxShaderSetupStatic with direct cbuffer feed. NOT a .t3 compound.
//   ④ STEP-0 backward-trace of FractalNoise.t3 connections to FloatsToBuffer slot 4ef6f204:
//      children = Vector4Components(ColorA) → Vector4Components(ColorB) → Vector2Components(Offset)
//      → Vector2Components(Stretch) → Scale direct → RandomPhase direct → IntToFloat(Iterations)
//      → __padding child (zero float) → Vector2Components(GainAndBias) → Vector2Components(WarpXY)
//      → WarpZ direct. NO intermediate math nodes (no Multiply, no extra IntToFloat).
//      cbuffer field order matches FractalNoise.hlsl b0 verbatim.
//   ⑤ Noise is purely procedural: hash33() with MOD3 spatial constants. No temporal random,
//      no cross-frame feedback, no external noise texture asset. Deterministic for given coord.
//
// Generator convention (Cut 61): FractalNoise has NO texture input at all — not even an optional
//   one. We still bind a 1×1 dummy so the PSO doesn't have an unbound texture slot. The shader
//   ignores it entirely (no `inputTexture` sample call in FractalNoise.hlsl).
//
// HLSL→MSL forks (named):
//   [fork-hash33-verbatim]   hash33 with MOD3=(0.1031,0.11369,0.13787) ported exactly;
//     NOT using hash.metal.h (different hash family). Constants are load-bearing for parity.
//   [fork-comma-operator]    HLSL line 59 `e * (1.0-e.zxy, 1.0-e.zxy, 1.0-e.zxy)` is
//     comma-operator (evaluates to last expr) → MSL `e * (1.0f - e.zxy)`.
//   [fork-bias-functions-inline] ApplyGainAndBias from bias-functions.hlsl inlined into
//     fractalnoise.metal (no shared header needed; same logic byte-identical).
//   [fork-no-sampler]        Pure generator — no sampler bound (unlike filter ops).
//
// Self-contained leaf: cookFractalNoise + ImageFilterOp self-registration + runFractalNoiseSelfTest.
// CMake CONFIGURE_DEPENDS glob auto-picks point_ops_fractalnoise.cpp and shaders/fractalnoise.metal
// — no CMake edits needed.

#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/fractalnoise_params.h"       // FractalNoiseParams/Resolution, FRACTALNOISE_* bindings
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"               // TexCookCtx, cookParam
#include "runtime/tex_op_cache.h"              // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration so the file-scope ImageFilterOp registrar can reference runFractalNoiseSelfTest.
int runFractalNoiseSelfTest(bool injectBug);

namespace {

// Helper: 1×1 transparent-black dummy texture. FractalNoise has no Image input slot, but we need
// a valid texture bound to satisfy the Metal pipeline. The shader never samples it.
// [generator-dummy convention, Cut 61]
static MTL::Texture* makeFractalNoiseDummyTex(MTL::Device* dev) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  const uint8_t px[4] = {0, 0, 0, 0};
  t->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, px, 4);
  return t;
}

// FractalNoise generator op: single fullscreen fragment pass.
// No texture input (FractalNoise.cs has no Image slot). Runs unconditionally.
void cookFractalNoise(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "fractalnoise_vs", "fractalnoise_fs", fmt);
  if (!rps) return;

  // --- b0 params (TiXL FractalNoise.cs/.t3 defaults) ---
  FractalNoiseParams p{};
  // ColorA (Vec4, TiXL default (1e-6, 1e-6, 1e-6, 1.0) — near-black)
  p.ColorAR = cookParam(c, "ColorA.r", 1e-6f);
  p.ColorAG = cookParam(c, "ColorA.g", 1e-6f);
  p.ColorAB = cookParam(c, "ColorA.b", 1e-6f);
  p.ColorAA = cookParam(c, "ColorA.a", 1.0f);
  // ColorB (Vec4, TiXL default (1.0, 1.0, 1.0, 1.0) — white)
  p.ColorBR = cookParam(c, "ColorB.r", 1.0f);
  p.ColorBG = cookParam(c, "ColorB.g", 1.0f);
  p.ColorBB = cookParam(c, "ColorB.b", 1.0f);
  p.ColorBA = cookParam(c, "ColorB.a", 1.0f);
  // Offset (Vec2, TiXL default (0,0))
  p.OffsetX = cookParam(c, "Offset.x", 0.0f);
  p.OffsetY = cookParam(c, "Offset.y", 0.0f);
  // Stretch (Vec2, TiXL default (2,2))
  p.StretchX = cookParam(c, "Stretch.x", 2.0f);
  p.StretchY = cookParam(c, "Stretch.y", 2.0f);
  // Scale (Single, TiXL default 1.0)
  p.Scale = cookParam(c, "Scale", 1.0f);
  // RandomPhase (Single, TiXL default 5.0; shader uses Phase/10 for pos.z)
  p.Phase = cookParam(c, "RandomPhase", 5.0f);
  // Iterations (Int→float via IntToFloat in .t3; TiXL default 2; shader clamps 1..5)
  p.Iterations = cookParam(c, "Iterations", 2.0f);
  // __padding (explicit pad child in .t3, always 0.0)
  p._padding = 0.0f;
  // GainAndBias (Vec2, TiXL default (0.5, 0.5) — neutral bias/gain)
  p.GainX = cookParam(c, "GainAndBias.x", 0.5f);
  p.BiasY = cookParam(c, "GainAndBias.y", 0.5f);
  // WarpXY (Vec2 = WarpOffsetXY in shader; TiXL default (0,0))
  p.WarpX = cookParam(c, "WarpXY.x", 0.0f);
  p.WarpY = cookParam(c, "WarpXY.y", 0.0f);
  // WarpZ (Single = WarpOffsetZ in shader; TiXL default 0.0)
  p.WarpZ = cookParam(c, "WarpZ", 0.0f);

  // b1 Resolution
  FractalNoiseResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // Bind 1×1 dummy (shader never samples it; needed for valid Metal pipeline).
  MTL::Texture* dummyTex = makeFractalNoiseDummyTex(c.dev);

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  // Bind dummy (t0) — no sampler needed (shader doesn't sample it). [fork-no-sampler]
  enc->setFragmentTexture(dummyTex, 0);
  enc->setFragmentBytes(&p,   sizeof(FractalNoiseParams),     FRACTALNOISE_Params);
  enc->setFragmentBytes(&res, sizeof(FractalNoiseResolution), FRACTALNOISE_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  dummyTex->release();
}

}  // namespace

// Self-registration. File-scope static feeds imageFilterSpecSink() + texReg() +
// imageFilterSelfTests() during pre-main dynamic init. No shared file edited.
static const ImageFilterOp _reg_fractalnoise{
    // FractalNoise (TiXL Lib.image.generate.noise.FractalNoise): simplex fBm generator.
    // Zero Texture2D inputs — pure generator. Texture2D out.
    // Kernel: FractalNoise.hlsl — hash33 spatial hash, 5-octave noise_sum_abs, outer fBm warp loop
    //   (1..5 Iterations), ApplyGainAndBias, lerp ColorA/ColorB.
    // Params mirror FractalNoise.cs/.t3 verbatim.
    // FORKS (named): generator dummy (1×1 black, not sampled); hash33-verbatim (MOD3 constants);
    //   comma-operator (line 59); bias-functions-inline; no-sampler (generator).
    {"FractalNoise", "FractalNoise",
     {// No Image input — pure generator. Only output pin.
      {"out", "out", "Texture2D", false},
      // ColorA (Vec4, TiXL default (~0,~0,~0,1) — near-black)
      {"ColorA.r", "ColorA", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ColorA.g", "ColorA.g", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorA.b", "ColorA.b", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorA.a", "ColorA.a", "Float", true, 1.0f,  0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // ColorB (Vec4, TiXL default (1,1,1,1) — white)
      {"ColorB.r", "ColorB", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ColorB.g", "ColorB.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorB.b", "ColorB.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorB.a", "ColorB.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Scale (Single, TiXL default 1.0)
      {"Scale", "Scale", "Float", true, 1.0f, 0.01f, 16.0f, Widget::Slider},
      // Stretch (Vec2, TiXL default (2,2))
      {"Stretch.x", "Stretch", "Float", true, 2.0f, 0.01f, 8.0f, Widget::Vec, {}, true, 2},
      {"Stretch.y", "Stretch.y", "Float", true, 2.0f, 0.01f, 8.0f, Widget::Vec, {}, true, 1},
      // Offset (Vec2, TiXL default (0,0))
      {"Offset.x", "Offset", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 2},
      {"Offset.y", "Offset.y", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
      // RandomPhase (Single, TiXL default 5.0; controls pos.z = Phase/10 in shader)
      {"RandomPhase", "RandomPhase", "Float", true, 5.0f, 0.0f, 100.0f, Widget::Slider},
      // Iterations (Int→float; TiXL default 2; shader clamps [1..5])
      {"Iterations", "Iterations", "Float", true, 2.0f, 1.0f, 5.0f, Widget::Slider},
      // GainAndBias (Vec2, TiXL default (0.5,0.5) — neutral)
      {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // WarpXY (Vec2 = WarpOffsetXY; TiXL default (0,0))
      {"WarpXY.x", "WarpXY", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"WarpXY.y", "WarpXY.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // WarpZ (Single = WarpOffsetZ; TiXL default 0.0)
      {"WarpZ", "WarpZ", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Slider},
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "FractalNoise", cookFractalNoise, "fractalnoise", runFractalNoiseSelfTest};

// --- FractalNoise MATH golden -----------------------------------------------------------------
//
// Test configuration (chosen to minimize hand-trace depth):
//   ColorA=(0,0,0,1) black, ColorB=(1,1,1,1) white
//   Scale=1.0, Stretch=(2,2), Offset=(0,0)
//   Phase=0.0  (→ pos.z = 0.0/10 = 0.0)
//   Iterations=1 (→ steps = clamp(1+0.5, 1.1, 5.1) = 1.1 → int(1) = 1)
//   GainAndBias=(0.5,0.5) (neutral — identity transform on f)
//   WarpXY=(0,0), WarpZ=0.0
//   Square 256×256 texture (aspectRatio = 1.0)
//
// Target pixel: CENTER = (128,128), UV = (128.5/256, 128.5/256) ≈ (0.5020, 0.5020)
//   ≈ (0.5, 0.5) for the purpose of this trace.
//
// ── UV → pos derivation (FractalNoise.hlsl psMain, aspectRatio=1.0) ─────────────────────────
//   uv = (0.5, 0.5)
//   uv -= 0.5  → (0, 0)
//   uv *= Stretch*Scale = (2,2)*1 = (2,2)  → (0, 0)
//   uv += Offset*(-1/1,1)*1*(2,2) = (0,0)  → (0, 0)
//   uv.x *= 1.0 (aspectRatio)             → uv = (0, 0)
//   pos = float3(0, 0, 0.0/10) = (0, 0, 0)
//
// ── noise_sum_abs(pos=(0,0,0)) ────────────────────────────────────────────────────────────────
//   (traced by hand from hash33 and simplex_noise at p=(0,0,0))
//
//   hash33((0,0,0)):
//     MOD3 = (0.1031, 0.11369, 0.13787)
//     p3 = frac((0,0,0) * MOD3) = (0, 0, 0)
//     p3 += dot((0,0,0), (0,0,0).yxz + 19.19) = dot(0, 19.19) = 0  → p3 = (0,0,0)
//     return -1 + 2*frac(0,0,0) = (-1,-1,-1)
//
//   simplex_noise((0,0,0)):
//     K1=1/3, K2=1/6
//     i  = floor((0,0,0) + 0) = (0,0,0)
//     d0 = (0,0,0) - ((0,0,0) - 0) = (0,0,0)
//     e  = step((0,0,0), d0 - d0.yzx) = step((0,0,0), (0,0,0)) = (1,1,1)
//     i1 = e * (1 - e.zxy) = (1,1,1) * (0,0,0) = (0,0,0)
//     i2 = 1 - e.zxy*(1-e) = 1 - (1,1,1)*(0,0,0) = (1,1,1)
//     d1 = d0 - (i1 - K2) = (0,0,0) - (-1/6,-1/6,-1/6) = (1/6, 1/6, 1/6)
//     d2 = d0 - (i2 - 2*K2) = (0,0,0) - (1-1/3,...) = (-2/3,-2/3,-2/3)
//     d3 = d0 - (1 - 3*K2) = (0,0,0) - (1/2,1/2,1/2) = (-1/2,-1/2,-1/2)
//     dot(d0,d0)=0, dot(d1,d1)=3*(1/6)^2=1/12, dot(d2,d2)=3*(4/9)=4/3, dot(d3,d3)=3/4
//     h = max(0.6 - (0, 1/12, 4/3, 3/4), 0) = max((0.6, 0.5167, -0.733, -0.15), 0)
//       = (0.6, 0.5167, 0, 0)
//     hash33(i=0)   = (-1,-1,-1)   (computed above)
//     hash33(i+i1)  = hash33(0,0,0) = (-1,-1,-1)  (i1=(0,0,0))
//     dot(d0, h33) = dot((0,0,0), (-1,-1,-1)) = 0
//     dot(d1, h33) = dot((1/6,1/6,1/6), (-1,-1,-1)) = -1/2
//     n = h^4 * float4(0, -1/2, 0, 0)
//       = (0.6^4, 0.5167^4, 0, 0) * (0, -1/2, 0, 0)
//       = (0.1296*0, 0.0713*(-0.5), 0, 0)
//       = (0, -0.03565, 0, 0)
//     simplex_noise = dot(31.316 * (1,1,1,1), n) = 31.316 * (-0.03565) ≈ -1.116
//
//   noise_sum_abs((0,0,0)) at all 5 octaves with p=2^k*(0,0,0)=(0,0,0):
//     = (1.0 + 0.5 + 0.25 + 0.125 + 0.0625) * 1.116 = 1.9375 * 1.116 ≈ 2.163
//
// ── Outer loop (steps=1, i=0) ─────────────────────────────────────────────────────────────────
//   f = 0.7
//   f1 = noise_sum_abs((0,0,0)*1 + (12.4,3,0)*0) = noise_sum_abs((0,0,0)) ≈ 2.163
//   pos += 0.7 * (0,0,0) = no change
//   sin(2.163) ≈ 0.8285
//   f *= 0.8285/2 + 0.5 = 0.91425
//   f = 0.7 * 0.91425 = 0.63998
//   f += 0.2  → f = 0.83998
//   f = 2*0.83998 - 1 = 0.67996
//
// ── ApplyGainAndBias(0.67996, (0.5,0.5)) ─────────────────────────────────────────────────────
//   g=0.5 (not < 0.5) → else branch: GetSchlickBias then GetBias.
//   GetBias(0.5, x) = x / ((2-2)*(1-x)+1) = x (identity when bias=0.5).
//   GetSchlickBias(0.5, 0.67996): x>0.5 → x=2*0.67996-1=0.35992;
//     GetBias(0.5, 0.35992)=0.35992; x=0.5*0.35992+0.5=0.67996.
//   → fBiased = 0.67996 (gain=0.5 and bias=0.5 are both identity).
//
// ── Final output ─────────────────────────────────────────────────────────────────────────────
//   lerp(ColorA=(0,0,0,1), ColorB=(1,1,1,1), saturate(0.67996)) = (0.67996, 0.67996, 0.67996, 1)
//   In RGBA8: R = G = B = round(0.67996*255) ≈ 173.  A = 255.
//
// GOLDEN ASSERTION: center pixel R ∈ [158, 188] (±15 tolerance for float rounding at 5 octaves).
//   Assert: R≈173, G≈173, B≈173, A=255.
//   NOTE: pos=(0,0,0) → hash33(0)=(-1,-1,-1) regardless of MOD3, so Sub-test A does NOT bite
//   MOD3 constants. MOD3 bite is handled by Sub-test B (non-zero coord, window [108,120]).
//
// ── injectBug (two modes exercised in runFractalNoiseSelfTest) ────────────────────────────────
// Mode A — Iterations=2 (center assertion):
//   Replaces Iterations=1 with 2. The second loop iteration evaluates
//   noise_sum_abs((0,0,0) + (12.4,3,0)*1) = noise_sum_abs((12.4,3,0)) — a completely different
//   noise value → f changes drastically from 0.67996 → output R shifts far from 173.
//   The center assertion R∈[158,188] FAILS → RED. Bites Iterations routing + noise-path.
//
// Mode B — Iterations=2 (non-zero coord assertion):
//   Sets Iterations=2 → second loop iteration uses pos offset (12.4,3,0)*1 — a different
//   noise input → f changes significantly → output R shifts far from 114 → FAIL.
//   This proves the non-zero coord test path is alive and can detect regressions.
//
// Sub-test B window [108,120] inherently bites hash33 MOD3 (no shader seam required):
//   Independent double-precision derivation (pos=(0,0,0.5), correct MOD3=(0.1031,0.11369,0.13787)):
//     frac(pos * MOD3) = frac((0, 0, 0.5*0.13787)) = (0, 0, 0.068935)
//     → hash33 gives non-trivial gradient → simplex_noise → f_outer=0.44671 → R=114.
//   Typo MOD3 (0.1031→0.1131): same derivation gives R=101 (13 counts away from correct).
//   Window [108,120]: correct R=114 IN, typo R=101 OUT. Separation=13 >> tolerance=6.
//   Conclusion: any MOD3 typo on the first digit shifts R by ~13 → falls outside window →
//   selftest FAILS — without any shader test branch or pad-field seam. The window IS the bite.
int runFractalNoiseSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-fractalnoise] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(td);

  auto readPixel = [&](const std::vector<uint8_t>& buf, uint32_t x, uint32_t y, int ch) -> int {
    return (int)buf[((size_t)y * W + x) * 4 + ch];
  };

  int failures = 0;

  // ── Sub-test A: center pixel (Scale=0, Phase=0 → pos=(0,0,0)) ────────────────────────────────
  // Bites: bias chain + 2f-1 + sin + octave-weight accumulation.
  // Does NOT bite MOD3 (pos=(0,0,0) → hash33(0)=(-1,-1,-1) regardless of MOD3).
  // injectBug Mode A: Iterations=2 → second loop iteration → f shifts far from 173 → FAIL.
  {
    std::map<std::string, float> params;
    params["ColorA.r"] = 0.0f; params["ColorA.g"] = 0.0f;
    params["ColorA.b"] = 0.0f; params["ColorA.a"] = 1.0f;
    params["ColorB.r"] = 1.0f; params["ColorB.g"] = 1.0f;
    params["ColorB.b"] = 1.0f; params["ColorB.a"] = 1.0f;
    params["Scale"] = 0.0f;       // all pixels → pos=(0,0,0)
    params["Stretch.x"] = 2.0f; params["Stretch.y"] = 2.0f;
    params["Offset.x"] = 0.0f; params["Offset.y"] = 0.0f;
    params["RandomPhase"] = 0.0f; // pos.z = 0
    params["Iterations"] = injectBug ? 2.0f : 1.0f;  // inject: shifts to second loop → FAIL
    params["GainAndBias.x"] = 0.5f; params["GainAndBias.y"] = 0.5f;
    params["WarpXY.x"] = 0.0f; params["WarpXY.y"] = 0.0f;
    params["WarpZ"] = 0.0f;

    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = nullptr; c.output = dst; c.params = &params;
    cookFractalNoise(c);

    std::vector<uint8_t> out((size_t)W * H * 4, 0);
    dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

    uint32_t cx = W / 2, cy = H / 2;
    int cR = readPixel(out, cx, cy, 0);
    int cG = readPixel(out, cx, cy, 1);
    int cB = readPixel(out, cx, cy, 2);
    int cA = readPixel(out, cx, cy, 3);

    // GOLDEN: R≈173, G≈173, B≈173, A=255. Tolerance ±15 for GPU float rounding.
    // Traced: pos=(0,0,0) → simplex_noise→ f_outer=0.6400→ 2f-1=0.2800→ ...? See comment block above.
    // NOTE: does NOT bite MOD3 (zero input to hash33 is MOD3-invariant).
    const int kExpR = 173, kTol = 15;
    bool ok = (cR >= kExpR - kTol && cR <= kExpR + kTol &&
               cG >= kExpR - kTol && cG <= kExpR + kTol &&
               cB >= kExpR - kTol && cB <= kExpR + kTol &&
               cA >= 250);
    // injectBug (Iterations=2) shifts R far from [158,188] → ok=false → failure counted → exit 1.
    if (!ok) failures++;
    printf("[selftest-fractalnoise] A center(%d,%d) R=%d G=%d B=%d A=%d "
           "(expect R~%d±%d) -> %s%s\n",
           cx, cy, cR, cG, cB, cA, kExpR, kTol,
           ok ? "PASS" : "FAIL",
           injectBug ? " [inject=Iterations2]" : "");
  }

  // ── Sub-test B: non-zero coord (Scale=0, Phase=5 → pos=(0,0,0.5)) ──────────────────────────
  // Bites: hash33 MOD3 constants — at pos.z=0.5, frac(pos*MOD3) is non-zero, so hash33 output
  // depends on MOD3 constants. Any MOD3 typo shifts R outside the tight window → FAIL.
  //
  // Window [108,120] inherently bites MOD3 (no shader seam needed):
  //   Correct MOD3 (0.1031,0.11369,0.13787): independent double-precision derivation → R=114 (IN).
  //   Typo MOD3 (0.1031→0.1131): same derivation → R=101 (OUT). Separation=13 >> tolerance=6.
  //   Hence: if the shader's #define MOD3 constant were wrong, this sub-test FAILS automatically
  //   without any runtime inject-bug seam. The assertion window IS the MOD3 bite.
  //
  // injectBug Mode B: Iterations=2 → second loop iteration evaluates noise at a different pos
  //   → f shifts far from 0.4467 → R outside [108,120] → FAIL. Proves the test path is alive.
  {
    std::map<std::string, float> params;
    params["ColorA.r"] = 0.0f; params["ColorA.g"] = 0.0f;
    params["ColorA.b"] = 0.0f; params["ColorA.a"] = 1.0f;
    params["ColorB.r"] = 1.0f; params["ColorB.g"] = 1.0f;
    params["ColorB.b"] = 1.0f; params["ColorB.a"] = 1.0f;
    params["Scale"] = 0.0f;       // all pixels → pos.xy=(0,0), pos.z=Phase/10
    params["Stretch.x"] = 2.0f; params["Stretch.y"] = 2.0f;
    params["Offset.x"] = 0.0f; params["Offset.y"] = 0.0f;
    params["RandomPhase"] = 5.0f; // pos.z = 5.0/10 = 0.5 → MOD3-sensitive
    params["Iterations"] = injectBug ? 2.0f : 1.0f;  // inject: extra loop → R shifts far → FAIL
    params["GainAndBias.x"] = 0.5f; params["GainAndBias.y"] = 0.5f;
    params["WarpXY.x"] = 0.0f; params["WarpXY.y"] = 0.0f;
    params["WarpZ"] = 0.0f;

    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 2; c.inputTexture = nullptr; c.output = dst; c.params = &params;
    cookFractalNoise(c);

    std::vector<uint8_t> out((size_t)W * H * 4, 0);
    dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

    // Any pixel works (Scale=0 → all pixels identical). Use center.
    uint32_t cx = W / 2, cy = H / 2;
    int cR = readPixel(out, cx, cy, 0);
    int cG = readPixel(out, cx, cy, 1);
    int cB = readPixel(out, cx, cy, 2);
    int cA = readPixel(out, cx, cy, 3);

    // GOLDEN: R≈114, G≈114, B≈114, A=255.
    // Independent double-precision derivation: pos=(0,0,0.5), Iterations=1, GainBias=identity
    //   → f_outer=0.44671 → R=114. Tolerance ±6: window [108,120].
    // Sub-test B window [108,120] inherently bites hash33 MOD3 constants — no shader seam needed:
    //   correct MOD3=(0.1031,0.11369,0.13787) → R=114 (IN window);
    //   typo MOD3 (0.1031→0.1131) → R=101 (OUT, separation=13 >> tolerance=6).
    //   If #define MOD3 in fractalnoise.metal were wrong, this assertion would FAIL automatically.
    const int kExpR2 = 114, kTol2 = 6;
    bool ok2 = (cR >= kExpR2 - kTol2 && cR <= kExpR2 + kTol2 &&
                cG >= kExpR2 - kTol2 && cG <= kExpR2 + kTol2 &&
                cB >= kExpR2 - kTol2 && cB <= kExpR2 + kTol2 &&
                cA >= 250);
    // injectBug (Iterations=2 → extra loop iteration) → R shifts far outside [108,120] → FAIL.
    if (!ok2) failures++;
    printf("[selftest-fractalnoise] B nonzero(%d,%d) R=%d G=%d B=%d A=%d "
           "(expect R~%d±%d, MOD3-sensitive window) -> %s%s\n",
           cx, cy, cR, cG, cB, cA, kExpR2, kTol2,
           ok2 ? "PASS" : "FAIL",
           injectBug ? " [inject=Iterations2]" : "");
  }

  dst->release();
  lib->release(); q->release(); dev->release(); pool->release();

  if (failures == 0) {
    printf("[selftest-fractalnoise] ALL PASS\n");
    return 0;
  } else {
    printf("[selftest-fractalnoise] FAIL (%d sub-test(s) failed)\n", failures);
    return 1;
  }
}

}  // namespace sw
