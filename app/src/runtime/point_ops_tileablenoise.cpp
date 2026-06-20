// TileableNoise image generator op (Phase C leaf).
// TiXL authority: Operators/Lib/image/generate/noise/TileableNoise.cs (slot declarations, types)
//   + TileableNoise.t3 (defaults: ColorA=(1e-6,~0,~0,1), ColorB=(1,1,1,1), Scale=1.0,
//     Offset=(0,0), RandomPhase=5.0, Gain=0.5, Lacunarity=2.0, Contrast=1.7,
//     GainAndBias=(0.5,0.5), Detail=1, Octaves=2, Resolution=1024×1024, GenerateMips=false,
//     OutputFormat=R16G16B16A16_Float)
//   + Assets/shaders/img/generate/PerlinNoise2d.hlsl (tileable Perlin fBm generator with
//     hash33 gradient noise, period-wrapped corners, fbmPerlinTileable, ApplyGainAndBias,
//     lerp ColorA/ColorB).
//
// Port class: _ImageFxShaderSetupStatic (bd0b9c5b) → single-pass fragment shader.
//
// STEP-0 portability check (PASSED):
//   ① Zero Texture2D inputs — TileableNoise.cs has NO Image [Input] slot. Pure generator.
//   ② No gradient-widget, no curve-LUT, no asset-texture, no mip-gen seam.
//   ③ _ImageFxShaderSetupStatic with direct cbuffer feed. NOT a _multiImageFxSetup compound.
//   ④ STEP-0 backward-trace of TileableNoise.t3 connections:
//      Float slot 4ef6f204: Vector4Components(ColorA)→ Vector4Components(ColorB)→
//        Vector2Components(Offset)→ Scale direct→ RandomPhase direct→
//        Vector2Components(GainAndBias)→ Gain direct→ Lacunarity direct→ Contrast direct.
//      Int slot 86fe6e64: ClampInt(Octaves,min=1,max=10)→ Detail direct.
//      NO intermediate math nodes (no Multiply, no extra IntToFloat between params and buffer).
//   ⑤ Tileable Perlin: procedural hash33+gradient, no temporal random, no external asset.
//
// HLSL→MSL forks (named):
//   [fork-hash33-tileable]    hash33 uses frac(p*0.1031)+dot(p,p.yzx+33.33) from
//     PerlinNoise2d.hlsl exactly. This is a DIFFERENT hash than FractalNoise.hlsl's hash33
//     (which uses MOD3 float3(0.1031,0.11369,0.13787) pattern). Not merged into hash.metal.h.
//   [fork-bias-functions-inline]  ApplyGainAndBias inlined (same as fractalnoise.metal).
//   [fork-no-sampler]         Pure generator — no input texture or sampler bound.
//   [fork-int-buffer2]        HLSL b2 (IntParams: Iterations, Detail) maps to MSL [[buffer(2)]].
//     In Metal, all three buffers (0, 1, 2) must be explicitly bound by the host.
//   [fork-generator-dummy]    No Image input slot in TileableNoise.cs → no texture bound at all.
//     Unlike Rings (optional input), TileableNoise has zero texture inputs, so no dummy needed.
//
// Self-contained leaf: cookTileableNoise + ImageFilterOp self-registration +
// runTileableNoiseSelfTest. CMake CONFIGURE_DEPENDS glob auto-picks this file and
// shaders/tileablenoise.metal — no CMake edits needed.

#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h"   // ImageFilterOp self-registration
#include "runtime/point_graph.h"                // TexCookCtx, cookParam
#include "runtime/tex_op_cache.h"               // cachedTexPSO (PSO reuse)
#include "runtime/tileablenoise_params.h"       // TileableNoiseFloatParams etc.

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration so the file-scope ImageFilterOp registrar can reference the selftest.
int runTileableNoiseSelfTest(bool injectBug);

namespace {

// TileableNoise generator op: single fullscreen fragment pass.
// No texture input (TileableNoise.cs has no Image slot). [fork-generator-dummy]
void cookTileableNoise(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "tileablenoise_vs", "tileablenoise_fs", fmt);
  if (!rps) return;

  // --- b0 FloatParams (TiXL TileableNoise.cs/.t3 defaults, PerlinNoise2d.hlsl order) ---
  TileableNoiseFloatParams fp{};
  // ColorA (Vec4, TiXL default (~1e-6,~1e-6,~1e-6,1.0) — near-black)
  fp.ColorAR = cookParam(c, "ColorA.r", 1e-6f);
  fp.ColorAG = cookParam(c, "ColorA.g", 1e-6f);
  fp.ColorAB = cookParam(c, "ColorA.b", 1e-6f);
  fp.ColorAA = cookParam(c, "ColorA.a", 1.0f);
  // ColorB (Vec4, TiXL default (1.0, ~1.0, ~1.0, 1.0) — near-white)
  fp.ColorBR = cookParam(c, "ColorB.r", 1.0f);
  fp.ColorBG = cookParam(c, "ColorB.g", 0.99999f);
  fp.ColorBB = cookParam(c, "ColorB.b", 0.99999f);
  fp.ColorBA = cookParam(c, "ColorB.a", 1.0f);
  // Offset (Vec2, TiXL default (0,0))
  fp.OffsetX = cookParam(c, "Offset.x", 0.0f);
  fp.OffsetY = cookParam(c, "Offset.y", 0.0f);
  // Scale (Single, TiXL default 1.0)
  fp.Scale = cookParam(c, "Scale", 1.0f);
  // RandomPhase (Single, TiXL default 5.0; shader uses directly as p.z)
  fp.Phase = cookParam(c, "RandomPhase", 5.0f);
  // GainAndBias (Vec2, TiXL default (0.5, 0.5) — neutral)
  fp.GainAndBiasX = cookParam(c, "GainAndBias.x", 0.5f);
  fp.GainAndBiasY = cookParam(c, "GainAndBias.y", 0.5f);
  // Gain (Single, TiXL default 0.5 — per-octave amplitude decay)
  fp.Gain = cookParam(c, "Gain", 0.5f);
  // Lacunarity (Single, TiXL default 2.0 — per-octave frequency multiplier)
  fp.Lacunarity = cookParam(c, "Lacunarity", 2.0f);
  // Contrast (Single, TiXL default 1.7)
  fp.Contrast = cookParam(c, "Contrast", 1.7f);
  fp._pad[0] = fp._pad[1] = fp._pad[2] = 0.0f;

  // --- b1 Resolution ---
  TileableNoiseResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // --- b2 IntParams ---
  // Octaves → clamped 1..10 (ClampInt in .t3); Detail → tile repetition scale.
  TileableNoiseIntParams ip{};
  int rawOctaves = (int)cookParam(c, "Octaves", 2.0f);
  ip.Iterations = rawOctaves < 1 ? 1 : (rawOctaves > 10 ? 10 : rawOctaves);
  ip.Detail = (int)cookParam(c, "Detail", 1.0f);
  ip._pad[0] = ip._pad[1] = 0;

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);

  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  // No texture input (pure generator). Bind three constant buffers. [fork-int-buffer2]
  enc->setFragmentBytes(&fp,  sizeof(TileableNoiseFloatParams),  TILEABLENOISE_FloatParams);
  enc->setFragmentBytes(&res, sizeof(TileableNoiseResolution),   TILEABLENOISE_Resolution);
  enc->setFragmentBytes(&ip,  sizeof(TileableNoiseIntParams),    TILEABLENOISE_IntParams);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

}  // namespace

// Self-registration. File-scope static feeds imageFilterSpecSink() + texReg() +
// imageFilterSelfTests() during pre-main dynamic init. No shared file edited.
static const ImageFilterOp _reg_tileablenoise{
    // TileableNoise (TiXL Lib.image.generate.noise.TileableNoise): tileable Perlin fBm generator.
    // Zero Texture2D inputs — pure generator. Texture2D out.
    // Kernel: PerlinNoise2d.hlsl — hash33 gradient noise, period-wrapped corners,
    //   fbmPerlinTileable, ApplyGainAndBias, lerp ColorA/ColorB.
    // Params mirror TileableNoise.cs/.t3 verbatim.
    // FORKS (named): hash33-tileable; bias-functions-inline; no-sampler; int-buffer2; generator-dummy.
    {"TileableNoise", "TileableNoise",
     {// No Image input — pure generator.
      {"out", "out", "Texture2D", false},
      // ColorA (Vec4, TiXL default (~0,~0,~0,1) — near-black)
      {"ColorA.r", "ColorA", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ColorA.g", "ColorA.g", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorA.b", "ColorA.b", "Float", true, 1e-6f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorA.a", "ColorA.a", "Float", true, 1.0f,  0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // ColorB (Vec4, TiXL default (~1,~1,~1,1) — near-white)
      {"ColorB.r", "ColorB", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ColorB.g", "ColorB.g", "Float", true, 0.99999f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorB.b", "ColorB.b", "Float", true, 0.99999f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorB.a", "ColorB.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Detail (Int, TiXL default 1; tile repetition count)
      {"Detail", "Detail", "Float", true, 1.0f, 1.0f, 16.0f, Widget::Slider},
      // Octaves (Int→ClampInt, TiXL default 2, clamp 1..10; feeds Iterations in shader)
      {"Octaves", "Octaves", "Float", true, 2.0f, 1.0f, 10.0f, Widget::Slider},
      // Gain (Single, TiXL default 0.5 — per-octave amplitude decay)
      {"Gain", "Gain", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Slider},
      // Lacunarity (Single, TiXL default 2.0 — per-octave frequency multiplier)
      {"Lacunarity", "Lacunarity", "Float", true, 2.0f, 0.5f, 4.0f, Widget::Slider},
      // RandomPhase (Single, TiXL default 5.0; p.z in shader)
      {"RandomPhase", "RandomPhase", "Float", true, 5.0f, 0.0f, 100.0f, Widget::Slider},
      // Scale (Single, TiXL default 1.0)
      {"Scale", "Scale", "Float", true, 1.0f, 0.01f, 16.0f, Widget::Slider},
      // Offset (Vec2, TiXL default (0,0))
      {"Offset.x", "Offset", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 2},
      {"Offset.y", "Offset.y", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
      // Contrast (Single, TiXL default 1.7)
      {"Contrast", "Contrast", "Float", true, 1.7f, 0.0f, 5.0f, Widget::Slider},
      // GainAndBias (Vec2, TiXL default (0.5,0.5) — neutral)
      {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 1024.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 1024.0f, 1.0f, 8192.0f}},
     nullptr},
    "TileableNoise", cookTileableNoise, "tileablenoise", runTileableNoiseSelfTest};

// --- TileableNoise GOLDEN PROBES ---------------------------------------------------------------
//
// Sub-test A: d=0 saturated plateau (Contrast=0 → noise*0.5*0+0.5 = 0.5 always)
//   Config: ColorA=(0,0,0,1), ColorB=(1,1,1,1), Contrast=0, GainAndBias=(0.5,0.5) identity.
//   noise→0.5 always (Contrast=0 collapses all variation to constant 0.5).
//   ApplyGainAndBias(0.5,(0.5,0.5)): g=0.5 (not<0.5) → GetSchlickBias(0.5,0.5):
//     x=0.5 not<0.5 → x=2*0.5-1=0, GetBias(0.5,0)=0, x=0.5*0+0.5=0.5.
//     GetBias(0.5, 0.5) = 0.5/((2-2)*0.5+1)=0.5/1=0.5. fBiased=0.5.
//   Output: mix(black,white,0.5) = (0.5,0.5,0.5,1) → R=G=B=128, A=255.
//   injectBug: Contrast=1.7 (default) → noise path active, output ≠ 128 → FAIL.
//   Bites: Contrast param routing, entire noise-output chain.
//
// Sub-test B: noise-live coord — BITES the HLSL z-period scalar-truncation (PerlinNoise2d.hlsl:115)
//   Config: ColorA=(0,0,0,1), ColorB=(1,1,1,1), Scale=8, Detail=4, Phase=9.1, Octaves=1,
//           Gain=0.5, Lacunarity=2.0, Contrast=1.7, GainAndBias=(0.5,0.5). Probe pixel (100,150).
//
//   ★ This probe is specifically placed to expose the z-period truncation fork. In HLSL,
//     `float per = basePeriod * freq` declares `per` SCALAR → the float3 (Detail,Detail,1024)
//     is TRUNCATED to .x → per = Detail*freq, then splatted to float3(per,per,per) when passed
//     to perlinTileable's `float3 period`. So the z basePeriod (1024) is DISCARDED: the actual
//     period is float3(4,4,4), NOT float3(4,4,1024). An earlier sw port kept the float3 (z=1024)
//     → parity bug. This probe distinguishes the two:
//
//     Independent double-precision Python reference (replicates HLSL math exactly, incl. fmod
//     trunc semantics + the scalar truncation):
//       TiXL-faithful (z-period=4):  R = 193   ← GOLDEN
//       buggy (z-period=1024):       R = 97    ← old port; 96 counts away → caught by ±15 window
//
//   At pixel (100,150): texCoord≈((100.5)/256, (150.5)/256)=(0.3926,0.5879). uv=texCoord*4.
//     p = float3(uv*8 + 666, 9.1). Scale≠0 here activates x/y gradients (f.xy≠0) as well as z,
//     so the period truncation on z genuinely changes which cells fmod() wraps to.
//
//   injectBug: Phase=3.0 → f.z shifts → different z-cell gradients → R=58 (135 counts away from
//     193) → outside window → FAIL. Proves Phase routing + path liveness.
//   This probe BITES: z-period truncation parity, Phase routing, hash33 z-axis gradients.
//
int runTileableNoiseSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-tileablenoise] FAIL: no metallib\n");
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

  // ── Sub-test A: d=0 saturated plateau (Contrast=0 → R=128 always) ───────────────────────────
  // Bites: Contrast param routing + ApplyGainAndBias + output lerp chain.
  // injectBug: Contrast=1.7 (non-zero) → noise path active, R ≠ 128 → FAIL.
  {
    std::map<std::string, float> params;
    params["ColorA.r"] = 0.0f; params["ColorA.g"] = 0.0f;
    params["ColorA.b"] = 0.0f; params["ColorA.a"] = 1.0f;
    params["ColorB.r"] = 1.0f; params["ColorB.g"] = 1.0f;
    params["ColorB.b"] = 1.0f; params["ColorB.a"] = 1.0f;
    params["Scale"]    = 1.0f;
    params["RandomPhase"] = 5.0f;
    params["Gain"]     = 0.5f;
    params["Lacunarity"] = 2.0f;
    params["Contrast"] = injectBug ? 1.7f : 0.0f;  // inject: Contrast non-zero → noise active → ≠128
    params["GainAndBias.x"] = 0.5f;
    params["GainAndBias.y"] = 0.5f;
    params["Offset.x"] = 0.0f; params["Offset.y"] = 0.0f;
    params["Octaves"]  = 2.0f;
    params["Detail"]   = 1.0f;

    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = nullptr; c.output = dst; c.params = &params;
    cookTileableNoise(c);

    std::vector<uint8_t> out((size_t)W * H * 4, 0);
    dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

    uint32_t cx = W / 2, cy = H / 2;
    int cR = readPixel(out, cx, cy, 0);
    int cG = readPixel(out, cx, cy, 1);
    int cB = readPixel(out, cx, cy, 2);
    int cA = readPixel(out, cx, cy, 3);

    // GOLDEN: Contrast=0 → noise*0.5*0+0.5=0.5 → ApplyGainAndBias(0.5,(0.5,0.5))=0.5
    // → mix(black,white,0.5) = (0.5,0.5,0.5,1) → R=G=B=128, A=255.
    // Tolerance ±3 for float rounding (very tight: Contrast=0 is degenerate-insensitive).
    const int kExpR = 128, kTol = 3;
    bool ok = (cR >= kExpR - kTol && cR <= kExpR + kTol &&
               cG >= kExpR - kTol && cG <= kExpR + kTol &&
               cB >= kExpR - kTol && cB <= kExpR + kTol &&
               cA >= 250);
    if (!ok) failures++;
    printf("[selftest-tileablenoise] A plateau(%d,%d) R=%d G=%d B=%d A=%d "
           "(expect R=%d±%d) -> %s%s\n",
           cx, cy, cR, cG, cB, cA, kExpR, kTol,
           ok ? "PASS" : "FAIL",
           injectBug ? " [inject=Contrast1.7]" : "");
  }

  // ── Sub-test B: z-period truncation probe (Detail=4, Scale=8, Phase=9.1, Octaves=1) ──────────
  // Scale≠0 → x/y gradients active (f.xy≠0). The HLSL scalar-truncation forces period=(4,4,4),
  //   NOT (4,4,1024); fmod() on the z corner wraps to period 4. Probe pixel (100,150).
  // GOLDEN (Python double-precision, replicating HLSL incl. scalar truncation + fmod trunc):
  //   TiXL-faithful (z-period=4):  R=193.   buggy (z-period=1024): R=97 (96 counts away → caught).
  // injectBug: Phase=3.0 → R=58 (135 counts away) → outside window → FAIL (proves path alive).
  {
    std::map<std::string, float> params;
    params["ColorA.r"] = 0.0f; params["ColorA.g"] = 0.0f;
    params["ColorA.b"] = 0.0f; params["ColorA.a"] = 1.0f;
    params["ColorB.r"] = 1.0f; params["ColorB.g"] = 1.0f;
    params["ColorB.b"] = 1.0f; params["ColorB.a"] = 1.0f;
    params["Scale"]    = 8.0f;   // ≠0 → x/y gradients active alongside z (period-trunc visible)
    params["RandomPhase"] = injectBug ? 3.0f : 9.1f;  // inject: Phase=3.0 → R=58 → FAIL
    params["Gain"]     = 0.5f;
    params["Lacunarity"] = 2.0f;
    params["Contrast"] = 1.7f;
    params["GainAndBias.x"] = 0.5f;
    params["GainAndBias.y"] = 0.5f;
    params["Offset.x"] = 0.0f; params["Offset.y"] = 0.0f;
    params["Octaves"]  = 1.0f;
    params["Detail"]   = 4.0f;   // basePeriod=(4,4,1024) → HLSL truncates z to 4

    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 2; c.inputTexture = nullptr; c.output = dst; c.params = &params;
    cookTileableNoise(c);

    std::vector<uint8_t> out((size_t)W * H * 4, 0);
    dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

    // Probe pixel (100,150) — chosen so the z-period truncation produces a large gap between
    // faithful (R=193) and the old z=1024 bug (R=97).
    uint32_t cx = 100, cy = 150;
    int cR = readPixel(out, cx, cy, 0);
    int cG = readPixel(out, cx, cy, 1);
    int cB = readPixel(out, cx, cy, 2);
    int cA = readPixel(out, cx, cy, 3);

    // GOLDEN: R=193 (TiXL-faithful, z-period truncated to 4). Window ±15 (GPU float32 vs Python
    //   float64 + sqrt/normalize accumulation). Window [178,208]:
    //     - excludes the z=1024 bug value (97) by 81 counts;
    //     - excludes the injectBug Phase=3.0 value (58) by 120 counts.
    const int kExpR_B = 193, kTol_B = 15;
    bool ok2 = (cR >= kExpR_B - kTol_B && cR <= kExpR_B + kTol_B &&
                cG >= kExpR_B - kTol_B && cG <= kExpR_B + kTol_B &&
                cB >= kExpR_B - kTol_B && cB <= kExpR_B + kTol_B &&
                cA >= 250);
    if (!ok2) failures++;
    printf("[selftest-tileablenoise] B z-period(%d,%d) R=%d G=%d B=%d A=%d "
           "(expect R=%d±%d, z-trunc=4 not 1024) -> %s%s\n",
           cx, cy, cR, cG, cB, cA, kExpR_B, kTol_B,
           ok2 ? "PASS" : "FAIL",
           injectBug ? " [inject=Phase3.0]" : "");
  }

  dst->release();
  lib->release(); q->release(); dev->release(); pool->release();

  if (failures == 0) {
    printf("[selftest-tileablenoise] ALL PASS\n");
    return 0;
  } else {
    printf("[selftest-tileablenoise] FAIL (%d sub-test(s) failed)\n", failures);
    return 1;
  }
}

}  // namespace sw
