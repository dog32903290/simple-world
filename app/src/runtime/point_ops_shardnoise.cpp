// ShardNoise image generator op (Phase C leaf).
// TiXL authority: Operators/Lib/image/generate/noise/ShardNoise.cs (slot declarations, types) +
//   ShardNoise.t3 (defaults: ColorA=(1e-6,1e-6,1e-6,1), ColorB=(1,1,1,1), Direction=(0,0),
//   Stretch=(2,2), Scale=10, Sharpness=1.0, Phase=0.0, Rate=2.0, Method=0,
//   GainAndBias=(0.5,0.5), Offset=(0,0), Resolution=256×256, GenerateMips=false) +
//   Assets/shaders/img/generate/ShardNoise.hlsl (3D Voronoi-style shard noise with
//   exp2-weighted tanh approximation, 3×3×3 cell iteration, ApplyGainAndBias, optional octaves).
//
// Port class: _ImageFxShaderSetupStatic (bd0b9c5b) → single-pass fragment shader.
//
// PORTABILITY HARDGATE (STEP-0 backward-trace):
//   ① Zero Texture2D inputs — ShardNoise.cs has NO Image [Input] slot. Pure generator.
//   ② No gradient-widget, no curve-LUT, no asset-texture, no mip-gen seam.
//   ③ _ImageFxShaderSetupStatic — single pass, direct cbuffer feed.
//   ④ .t3 backward-trace (connections to FloatsToBuffer slot 4ef6f204, document order):
//        Vector4Components(ColorA) → 4f
//        Vector4Components(ColorB) → 4f
//        Vector2Components(Direction) → 2f
//        Vector2Components(Stretch) → 2f
//        Scale direct → 1f
//        Sharpen direct → 1f
//        Phase direct → 1f
//        Rate direct → 1f
//        IntToFloat(Method) → 1f
//        Vector2Components(GainAndBias) → 2f
//        Vector2Components(Offset) → 2f
//        Value(0) explicit pad child → 1f   ← 22nd slot
//      22 fed floats → pad to 24 (96 bytes, 6 × 16-byte registers). NO intermediate math nodes.
//      Direct 1:1 mapping (NOT a _multiImageFxSetup routing trap).
//   ⑤ Noise is purely procedural: 3D spatial hash, 3×3×3 neighborhood. Fully deterministic.
//
// HLSL→MSL port forks (named):
//   [fork-bias-functions-inline]: ApplyGainAndBias from bias-functions.hlsl inlined in
//     shardnoise.metal (same pattern as fractalnoise.metal).
//   [fork-no-sampler-read]: ShardNoise.cs has no Image input; 1×1 dummy bound at t0
//     to satisfy Metal pipeline; shader never calls .sample(). [generator-dummy, Cut 61]
//   [fork-direction-flip-verbatim]: Direction.x flipped for UX (ported verbatim from TiXL).
//   [fork-offset-flip-verbatim]: Offset.x flipped (ported verbatim from TiXL).
//   [fork-method-round]: TiXL uses HLSL `switch(Method)` where Method arrives via IntToFloat.
//     MSL: `int method = (int)(p.Method + 0.5f)` to round before branching. Same semantics.
//
// HEADLESS GOLDEN (Sub-test A — Sharpness=0 saturation plateau):
//   When Sharpness=0 → _sharpness = 0*128 = 0 → for all 27 cells: s = 0 → v += 0 → v = 0.
//   shard_noise = (0 / t) * 0.5 + 0.5 = 0.5 for ALL pixels (t > 0 always).
//   ApplyGainAndBias(0.5, (0.5, 0.5)):
//     g=0.5 → else branch (not < 0.5):
//     GetSchlickBias(0.5, 0.5): x=0.5→ x=2*0.5-1=0; GetBias(0.5,0)=0; result=0.5*0+0.5=0.5.
//     GetBias(0.5, 0.5) = 0.5/((1/0.5-2)*(1-0.5)+1) = 0.5/(0*0.5+1) = 0.5.
//     → fBiased = 0.5.
//   lerp(ColorA=(0,0,0,1), ColorB=(1,1,1,1), 0.5) = (0.5, 0.5, 0.5, 1.0).
//   RGBA8: R=G=B=128, A=255. Every pixel identical.
//   injectBug: Sharpness=1.0 → noise becomes live → center pixel ≠ (128,128,128) → FAIL.
//
// HEADLESS GOLDEN (Sub-test B — noise-live, bites hash + _sharpness path):
//   Sharpness=1.0, Scale=5, Stretch=(1,1), Phase=0, Direction=(0,0), GainAndBias=(0.5,0.5),
//   ColorA=(0,0,0,1), ColorB=(1,1,1,1), Method=0.
//   With Sharpness=1 and non-zero Scale, shard_noise at center is NOT 0.5 (the 27-cell
//   v-sum is non-zero: the center cell alone contributes s=sharpness*dot(-h_center, h2-0.5)≠0).
//   Assertion: center pixel R ≠ 128 (noise is live, not stuck at midpoint).
//   injectBug: Sharpness=0 → shard_noise=0.5 → R=128 → assertion "R != 128" FAILS → RED.
//   Proves the _sharpness multiplication path is load-bearing.
//
// Self-contained leaf: cookShardNoise + ImageFilterOp self-registration + runShardNoiseSelfTest.
// CMake CONFIGURE_DEPENDS glob auto-picks point_ops_shardnoise.cpp and shaders/shardnoise.metal
// — no CMake edits needed. No shared header edited.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"               // TexCookCtx, cookParam
#include "runtime/shardnoise_params.h"         // ShardNoiseParams/Resolution, SHARDNOISE_* bindings
#include "runtime/tex_op_cache.h"              // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration so the file-scope ImageFilterOp registrar can reference runShardNoiseSelfTest.
int runShardNoiseSelfTest(bool injectBug);

namespace {

// Helper: 1×1 transparent-black dummy texture. ShardNoise has no Image input slot, but the
// Metal pipeline requires a bound texture at t0. The shader never samples it.
// [fork-no-sampler-read, generator-dummy convention, Cut 61]
static MTL::Texture* makeShardNoiseDummyTex(MTL::Device* dev) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  const uint8_t px[4] = {0, 0, 0, 0};
  t->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, px, 4);
  return t;
}

// ShardNoise generator op: single fullscreen fragment pass.
// No texture input (ShardNoise.cs has no Image slot). Runs unconditionally.
void cookShardNoise(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "shardnoise_vs", "shardnoise_fs", fmt);
  if (!rps) return;

  // --- b0 params (TiXL ShardNoise.cs/.t3 defaults) ---
  // cbuffer field order MUST match ShardNoise.hlsl cbuffer ParamConstants (STEP-0 traced).
  ShardNoiseParams p{};
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
  // Direction (Vec2, TiXL default (0,0))
  p.DirectionX = cookParam(c, "Direction.x", 0.0f);
  p.DirectionY = cookParam(c, "Direction.y", 0.0f);
  // Stretch (Vec2, TiXL default (2,2))
  p.StretchX = cookParam(c, "Stretch.x", 2.0f);
  p.StretchY = cookParam(c, "Stretch.y", 2.0f);
  // Scale (Single, TiXL default 10.0)
  p.Scale = cookParam(c, "Scale", 10.0f);
  // Sharpness / Sharpen (Single, TiXL default 1.0; shader: _sharpness = Sharpness * 128)
  p.Sharpness = cookParam(c, "Sharpness", 1.0f);
  // Phase (Single, TiXL default 0.0)
  p.Phase = cookParam(c, "Phase", 0.0f);
  // Rate (Single, TiXL default 2.0)
  p.Rate = cookParam(c, "Rate", 2.0f);
  // Method (Int→float via IntToFloat in .t3; TiXL default 0=Cubism)
  p.Method = cookParam(c, "Method", 0.0f);
  // GainAndBias (Vec2, TiXL default (0.5, 0.5) — neutral)
  p.GainX = cookParam(c, "GainAndBias.x", 0.5f);
  p.BiasY = cookParam(c, "GainAndBias.y", 0.5f);
  // Offset (Vec2, TiXL default (0,0))
  p.OffsetX = cookParam(c, "Offset.x", 0.0f);
  p.OffsetY = cookParam(c, "Offset.y", 0.0f);
  // _pad: explicit Value(0) child in .t3 maps to cbuffer pad slot after float2 Offset
  p._pad  = 0.0f;
  p._pad2[0] = p._pad2[1] = 0.0f;

  // b1 Resolution
  ShardNoiseResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // Bind 1×1 dummy (shader never samples it; needed for valid Metal pipeline).
  // [fork-no-sampler-read, generator-dummy convention]
  MTL::Texture* dummyTex = makeShardNoiseDummyTex(c.dev);

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  // Bind dummy at t0 — no sampler needed (shader doesn't call .sample()).
  enc->setFragmentTexture(dummyTex, 0);
  enc->setFragmentBytes(&p,   sizeof(ShardNoiseParams),     SHARDNOISE_Params);
  enc->setFragmentBytes(&res, sizeof(ShardNoiseResolution), SHARDNOISE_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  dummyTex->release();
}

}  // namespace

// Self-registration. File-scope static feeds imageFilterSpecSink() + texReg() +
// imageFilterSelfTests() during pre-main dynamic init. No shared file edited.
static const ImageFilterOp _reg_shardnoise{
    // ShardNoise (TiXL Lib.image.generate.noise.ShardNoise): 3D Voronoi-style shard noise
    // generator. Based on Shard Noise by @ENDESGA (shadertoy.com/view/dlKyWw).
    // Zero Texture2D inputs — pure generator. Texture2D out.
    // Kernel: shard_noise() with 3×3×3 cell iteration, exp2(-tau*r²) weights, tanh approx
    //   by @Xor. Three Method modes: Cubism / Cubism×Octaves / Octaves.
    // Params mirror ShardNoise.cs/.t3 verbatim.
    // FORKS (named): bias-functions-inline, no-sampler-read (generator dummy), direction-flip-
    //   verbatim, offset-flip-verbatim, method-round (IntToFloat round to int).
    {"ShardNoise", "ShardNoise",
     {// No Image input — pure generator. Only output pin.
      {"out", "out", "Texture2D", false},
      // ColorA (Vec4, TiXL default (1e-6, 1e-6, 1e-6, 1.0) — near-black)
      {"ColorA.r", "ColorA",   "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ColorA.g", "ColorA.g", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorA.b", "ColorA.b", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorA.a", "ColorA.a", "Float", true, 1.0f,  0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // ColorB (Vec4, TiXL default (1,1,1,1) — white)
      {"ColorB.r", "ColorB",   "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ColorB.g", "ColorB.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorB.b", "ColorB.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorB.a", "ColorB.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Scale (Single, TiXL default 10.0)
      {"Scale", "Scale", "Float", true, 10.0f, 0.01f, 32.0f, Widget::Slider},
      // Direction (Vec2, TiXL default (0,0); TiXL flips x for UX)
      {"Direction.x", "Direction",   "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Direction.y", "Direction.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Stretch (Vec2, TiXL default (2,2))
      {"Stretch.x", "Stretch",   "Float", true, 2.0f, 0.01f, 8.0f, Widget::Vec, {}, true, 2},
      {"Stretch.y", "Stretch.y", "Float", true, 2.0f, 0.01f, 8.0f, Widget::Vec, {}, true, 1},
      // Sharpness (Single, TiXL .cs name "Sharpen", default 1.0; shader: *128)
      {"Sharpness", "Sharpness", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Slider},
      // Phase (Single, TiXL default 0.0; controls directional offset + z animation)
      {"Phase", "Phase", "Float", true, 0.0f, -10.0f, 10.0f, Widget::Slider},
      // Rate (Single, TiXL default 2.0; Phase*0.05*Rate gives z coordinate)
      {"Rate", "Rate", "Float", true, 2.0f, 0.0f, 10.0f, Widget::Slider},
      // Method (enum int, TiXL default 0=Cubism; maps via IntToFloat)
      {"Method", "Method", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
       {"Cubism", "Cubism_x_Octaves", "Octaves"}},
      // GainAndBias (Vec2, TiXL default (0.5,0.5) — neutral)
      {"GainAndBias.x", "GainAndBias",   "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Offset (Vec2, TiXL default (0,0); TiXL flips x for UX)
      {"Offset.x", "Offset",   "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 2},
      {"Offset.y", "Offset.y", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "ShardNoise", cookShardNoise, "shardnoise", runShardNoiseSelfTest};

// --- ShardNoise MATH golden ------------------------------------------------------------------
//
// ── Sub-test A: Sharpness=0 saturation plateau (all pixels → gray midpoint) ──────────────────
//
// With Sharpness=0 → _sharpness = 0*128 = 0:
//   For every cell in the 3×3×3 loop: s = 0 * dot(...) = 0.
//   v contribution: w * 0 * rsqrt(1+0) = 0. So v = 0.
//   t = sum of all 27 weights w_i > 0 (always positive by exp2 construction).
//   shard_noise = (0/t) * 0.5 + 0.5 = 0.5  for EVERY pixel regardless of coord/Scale/Offset.
//
// ApplyGainAndBias(0.5, (0.5, 0.5)):
//   g=0.5, b=0.5. g not < 0.5 → else branch.
//   GetSchlickBias(0.5, 0.5):
//     x=0.5 → not < 0.5 branch: x = 2*0.5-1 = 0.
//     GetBias(1.0-0.5=0.5, 0) = 0 / ((1/0.5-2)*(1-0)+1) = 0/(0+1) = 0.
//     result = 0.5*0 + 0.5 = 0.5.
//   GetBias(b=0.5, 0.5) = 0.5 / ((1/0.5-2)*(1-0.5)+1) = 0.5/(0*0.5+1) = 0.5.
//   → fBiased = 0.5.
//
// lerp(ColorA=(0,0,0,1), ColorB=(1,1,1,1), 0.5) = (0.5, 0.5, 0.5, 1.0).
// RGBA8Unorm: R=G=B=128, A=255. All pixels identical.
//
// injectBug (Sub-test A): Sharpness=1.0 instead of 0.0.
//   → _sharpness=128 → noise terms s≠0 → v≠0 → shard_noise≠0.5 → R≠128 → FAIL.
//   Proves the Sharpness=0 plateau is the right test boundary.
//
// ── Sub-test B: noise-live (Sharpness=1 proves hash path is alive) ───────────────────────────
//
// Configuration: Sharpness=1.0, Scale=5.0, Stretch=(1,1), Direction=(0,0), Phase=0,
//   Offset=(0,0), GainAndBias=(0.5,0.5), ColorA=(0,0,0,1), ColorB=(1,1,1,1), Method=0.
//   Square 256×256 (aspectRatio=1.0).
//
// The center pixel (128,128) has UV≈(0.5,0.5) → after centering → (0,0).
// uv.x *= 1 (aspect), /= (1,1) → coord = (0,0,0) (Phase=0,Direction=0).
// shard_noise(5*(0,0,0), 128) = shard_noise((0,0,0), 128).
//
// At p=(0,0,0): ip=(0,0,0), fp=(0,0,0).
// Center cell (x=y=z=0): io=(0,0,0), h=hash(0,0,0)=(0,0,0) (sin(0)*43758=0, frac=0).
//   r = fp - (o+h) = (0,0,0). w = exp2(-tau*0) = 1. s = 128*dot((0,0,0), hash((11,31,47))-0.5).
//   h2 = hash((11,31,47)) = frac(sin(dot((11,31,47), (127.1,311.7,74.7)), ...) * 43758.5453123).
//   dot((11,31,47),(127.1,311.7,74.7)) = 1398.1+9662.7+3510.9 = 14571.7.
//   sin(14571.7) ≠ 0 → h2.x ≠ 0 → h2 - 0.5 ≠ (0,0,0) → but dot(r=(0,0,0), ...) = 0 anyway!
//   So the center cell STILL contributes s=0.
//
// Neighbor cells with o≠(0,0,0) and fp=(0,0,0):
//   r = -(o + h), where h = hash(o) depends on o.
//   For o=(1,0,0): h=hash(1,0,0)=frac(sin(127.1, 269.5, 113.5)*43758.5453123).
//   r = -(1+h.x, 0+h.y, 0+h.z). dot(r, r) > 0 → w = exp2(-tau*dot(r,r)) < 1 (small).
//   s = 128*dot(-o-h, hash(o+(11,31,47))-0.5) ≠ 0 in general → v ≠ 0.
//
// Conclusion: shard_noise((0,0,0), 128) ≠ 0.5 (dominant contribution from (0,0,0) cell is
//   zero, but the 26 neighbor cells with non-zero r and non-zero s give non-zero v/t → ≠ 0.5).
// → center pixel R ≠ 128.
//
// injectBug (Sub-test B): Sharpness=0 → shard_noise=0.5 → R=128 → "R != 128" FAILS → RED.
//   Proves the _sharpness multiplication path (and implicitly the 27-cell hash loop) is live.
int runShardNoiseSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-shardnoise] FAIL: no metallib\n");
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

  // ── Sub-test A: Sharpness=0 plateau — all pixels must be (128,128,128,255) ────────────────
  // Sharpness=0 → _sharpness=0 → v=0 → shard_noise=0.5 → lerp(A,B,0.5)=(0.5,0.5,0.5,1).
  // RGBA8: R=G=B=128, A=255 for EVERY pixel regardless of Scale/coord.
  // injectBug: Sharpness=1.0 → noise active → center R≠128 → FAIL.
  {
    std::map<std::string, float> params;
    params["ColorA.r"] = 0.0f; params["ColorA.g"] = 0.0f;
    params["ColorA.b"] = 0.0f; params["ColorA.a"] = 1.0f;
    params["ColorB.r"] = 1.0f; params["ColorB.g"] = 1.0f;
    params["ColorB.b"] = 1.0f; params["ColorB.a"] = 1.0f;
    params["Direction.x"] = 0.0f; params["Direction.y"] = 0.0f;
    params["Stretch.x"]   = 2.0f; params["Stretch.y"]   = 2.0f;
    params["Scale"]       = 10.0f;
    // injectBug: use Sharpness=1 (noise kicks in) instead of 0 (silent midpoint).
    params["Sharpness"]   = injectBug ? 1.0f : 0.0f;
    params["Phase"]       = 0.0f;
    params["Rate"]        = 2.0f;
    params["Method"]      = 0.0f;
    params["GainAndBias.x"] = 0.5f; params["GainAndBias.y"] = 0.5f;
    params["Offset.x"] = 0.0f; params["Offset.y"] = 0.0f;

    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = nullptr; c.output = dst; c.params = &params;
    cookShardNoise(c);

    std::vector<uint8_t> out((size_t)W * H * 4, 0);
    dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

    // Center pixel — with Sharpness=0 must be (128,128,128,255).
    uint32_t cx = W / 2, cy = H / 2;
    int cR = readPixel(out, cx, cy, 0);
    int cG = readPixel(out, cx, cy, 1);
    int cB = readPixel(out, cx, cy, 2);
    int cA = readPixel(out, cx, cy, 3);

    // GOLDEN: R=G=B=128 (±2 for fp rounding), A=255.
    const int kExp = 128, kTol = 2;
    bool ok = (cR >= kExp - kTol && cR <= kExp + kTol &&
               cG >= kExp - kTol && cG <= kExp + kTol &&
               cB >= kExp - kTol && cB <= kExp + kTol &&
               cA >= 253);
    // injectBug (Sharpness=1) → noise active → R shifts away from 128 → ok=false → FAIL.
    if (!ok) failures++;
    printf("[selftest-shardnoise] A plateau center(%u,%u) R=%d G=%d B=%d A=%d "
           "(expect R~%d±%d) -> %s%s\n",
           cx, cy, cR, cG, cB, cA, kExp, kTol,
           ok ? "PASS" : "FAIL",
           injectBug ? " [inject=Sharpness1]" : "");
  }

  // ── Sub-test B: noise-live — proves Sharpness path is load-bearing ───────────────────────
  // Sharpness=1, Scale=5, Stretch=(1,1): center pixel coord=(0,0,0) in 3D space.
  // Center cell (0,0,0) contributes s=0 (r=0 → dot(r,...)=0); neighbor cells have r≠0 and
  // s = 128*dot(-o-h, h2-0.5) ≠ 0 → v ≠ 0 → shard_noise ≠ 0.5 → R ≠ 128.
  // injectBug: Sharpness=0 → shard_noise=0.5 → R=128 → "R != 128" FAILS → RED.
  {
    std::map<std::string, float> params;
    params["ColorA.r"] = 0.0f; params["ColorA.g"] = 0.0f;
    params["ColorA.b"] = 0.0f; params["ColorA.a"] = 1.0f;
    params["ColorB.r"] = 1.0f; params["ColorB.g"] = 1.0f;
    params["ColorB.b"] = 1.0f; params["ColorB.a"] = 1.0f;
    params["Direction.x"] = 0.0f; params["Direction.y"] = 0.0f;
    params["Stretch.x"]   = 1.0f; params["Stretch.y"]   = 1.0f;  // Stretch=(1,1) for clarity
    params["Scale"]       = 5.0f;
    // injectBug: use Sharpness=0 (silent midpoint) instead of 1 (noise live).
    params["Sharpness"]   = injectBug ? 0.0f : 1.0f;
    params["Phase"]       = 0.0f;
    params["Rate"]        = 2.0f;
    params["Method"]      = 0.0f;  // Cubism
    params["GainAndBias.x"] = 0.5f; params["GainAndBias.y"] = 0.5f;
    params["Offset.x"] = 0.0f; params["Offset.y"] = 0.0f;

    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 2; c.inputTexture = nullptr; c.output = dst; c.params = &params;
    cookShardNoise(c);

    std::vector<uint8_t> out((size_t)W * H * 4, 0);
    dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

    uint32_t cx = W / 2, cy = H / 2;
    int cR = readPixel(out, cx, cy, 0);
    int cG = readPixel(out, cx, cy, 1);
    int cB = readPixel(out, cx, cy, 2);
    int cA = readPixel(out, cx, cy, 3);

    // GOLDEN: noise is live → R ≠ 128 (noise shifts the output away from the midpoint gray).
    // Tolerance: R must NOT be in [126, 130] (pure midpoint ±2). Any real noise value deviates.
    // injectBug (Sharpness=0) → shard_noise=0.5 → R=128 → falls IN [126,130] → ok=false → FAIL.
    bool ok = (cR < 126 || cR > 130);  // noise-live: R deviates from midpoint
    if (!ok) failures++;
    printf("[selftest-shardnoise] B noise-live center(%u,%u) R=%d G=%d B=%d A=%d "
           "(expect R outside [126,130]) -> %s%s\n",
           cx, cy, cR, cG, cB, cA,
           ok ? "PASS" : "FAIL",
           injectBug ? " [inject=Sharpness0]" : "");
  }

  dst->release();
  lib->release(); q->release(); dev->release(); pool->release();

  if (failures == 0) {
    printf("[selftest-shardnoise] ALL PASS\n");
    return 0;
  } else {
    printf("[selftest-shardnoise] FAIL (%d sub-test(s) failed)\n", failures);
    return 1;
  }
}

}  // namespace sw
