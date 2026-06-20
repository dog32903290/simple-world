// WorleyNoise image generator op (Phase C leaf).
// TiXL authority: Operators/Lib/image/generate/noise/WorleyNoise.cs (slot declarations, types) +
//   WorleyNoise.t3 (defaults: Scale=5, Stretch=(1,1), Offset=(0,0), Phase=5.0,
//     Randomness=12.6, Clamping=(0,1), GainAndBias=(0.5,0.5), Method=0=Worley_F1,
//     ColorA=(1,1,1,1) [near-white default], ColorB=(0,0,0,1), TextureBlend=1.0,
//     Resolution=512×512, GenerateMips=false) +
//   Assets/shaders/img/generate/WorleyNoise.hlsl (cellular/Worley noise, hash22, 6 method variants).
//
// Port class: _ImageFxShaderSetupStatic (bd0b9c5b) → single-pass fragment shader.
//
// STEP-0 portability check (PASSED):
//   ① Optional Texture2D input (Image, default null) — generator + optional overlay.
//      No multi-image seam required. We bind 1×1 dummy, set IsTextureValid=0.0.
//      The HLSL branches on IsTextureValid<0.5 → pure worley-lerp path (no texture blend).
//   ② _ImageFxShaderSetupStatic class (NOT _multiImageFxSetup compound). Direct cbuffer feed.
//   ③ .t3 backward-trace of connections to FloatsToBuffer slot 4ef6f204:
//      Vector4Components(ColorA)→4f | Vector4Components(ColorB)→4f |
//      Vector2Components(Offset)→2f | Vector2Components(Stretch)→2f |
//      Scale direct→1f | Phase direct→1f | Vector2Components(Clamping)→2f |
//      Vector2Components(GainAndBias)→2f | IntToFloat(Method)→1f |
//      Randomness direct→1f | TextureBlend direct→1f | IsTextureValid(auto)→1f.
//      Total 22 floats; pad 2 → 24 floats = 6×16-byte registers = 96 bytes. Matches HLSL b0.
//   ④ No gradient/curve-LUT/asset-texture/feedback/sim-state dependency.
//   ⑤ Noise is purely procedural: hash22 with unsigned integer multiply+XOR chain.
//      No temporal random, no cross-frame feedback, no external noise texture asset. Deterministic.
//
// Generator convention (Cut 61): optional Texture input (default null). Bind 1×1 transparent-black
//   dummy → IsTextureValid=0.0 → shader takes early-return worley-only path. Shader never samples
//   the dummy in the generator path (IsTextureValid < 0.5 short-circuits before SampleLevel).
//
// HLSL→MSL forks (named):
//   [fork-hash22-verbatim]      hash22 with constants 1597334673U, 3812015801U ported exactly.
//     NOT merged into hash.metal.h (different hash family). Constants are load-bearing for parity.
//   [fork-bias-functions-inline] ApplyGainAndBias (scalar) from bias-functions.hlsl inlined;
//     same GetBias + GetSchlickBias logic, byte-identical.
//   [fork-no-texture-in-generator-path] IsTextureValid=0.0 → pure worley lerp returned.
//     textureValue branch not exercised in selftest. Filter path present for future use.
//   [fork-sampler-mirror]       TiXL .t3 Wrap=Mirror → TextureAddressMode.Mirror →
//     MSL sampler(address::mirrored_repeat). Only relevant for filter (non-dummy) path.
//   [fork-cellCenter-not-tracked-F2F1] For generator path (IsTextureValid<0.5), cellCenter
//     tracking skipped to keep code clean; textureValue branch is dead in this path.
//   [fork-fmod-dead-code]       HLSL defines custom fmod() helper but it is never called
//     in psMain. Dead code; not ported to MSL.
//
// Self-contained leaf: cookWorleyNoise + ImageFilterOp self-registration + runWorleyNoiseSelfTest.
// CMake CONFIGURE_DEPENDS glob auto-picks point_ops_worleynoise.cpp and shaders/worleynoise.metal
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
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"               // TexCookCtx, cookParam
#include "runtime/tex_op_cache.h"              // cachedTexPSO (PSO reuse)
#include "runtime/worleynoise_params.h"        // WorleyNoiseParams/Resolution, WORLEYNOISE_* bindings

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration so the file-scope ImageFilterOp registrar can reference runWorleyNoiseSelfTest.
int runWorleyNoiseSelfTest(bool injectBug);

namespace {

// Helper: 1×1 transparent-black dummy texture for the no-input (generator) case.
// WorleyNoise has an optional Image input (default null). We bind a dummy so the Metal pipeline
// always has a valid texture2d. With IsTextureValid=0.0, the shader skips the texture sample.
// [generator-dummy convention, Cut 61]
static MTL::Texture* makeWorleyNoiseDummyTex(MTL::Device* dev) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  const uint8_t px[4] = {0, 0, 0, 0};
  t->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, px, 4);
  return t;
}

// WorleyNoise generator op: single fullscreen fragment pass.
// Optional texture input (WorleyNoise.cs Image slot). We always run the shader; if no input is
// wired, a 1×1 dummy is bound and IsTextureValid=0.0 → pure worley output (generator path).
void cookWorleyNoise(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "worleynoise_vs", "worleynoise_fs", fmt);
  if (!rps) return;

  // --- b0 params (TiXL WorleyNoise.cs/.t3 defaults) ---
  WorleyNoiseParams p{};
  // ColorA (Vec4, TiXL .t3 default (1,0.9999899,0.9999899,1) ≈ white)
  p.ColorAR = cookParam(c, "ColorA.r", 1.0f);
  p.ColorAG = cookParam(c, "ColorA.g", 1.0f);
  p.ColorAB = cookParam(c, "ColorA.b", 1.0f);
  p.ColorAA = cookParam(c, "ColorA.a", 1.0f);
  // ColorB (Vec4, TiXL default (0,0,0,1) — black)
  p.ColorBR = cookParam(c, "ColorB.r", 0.0f);
  p.ColorBG = cookParam(c, "ColorB.g", 0.0f);
  p.ColorBB = cookParam(c, "ColorB.b", 0.0f);
  p.ColorBA = cookParam(c, "ColorB.a", 1.0f);
  // Offset (Vec2, TiXL default (0,0))
  p.OffsetX = cookParam(c, "Offset.x", 0.0f);
  p.OffsetY = cookParam(c, "Offset.y", 0.0f);
  // Stretch (Vec2, TiXL default (1,1))
  p.StretchX = cookParam(c, "Stretch.x", 1.0f);
  p.StretchY = cookParam(c, "Stretch.y", 1.0f);
  // Scale (Single, TiXL default 5.0)
  p.Scale = cookParam(c, "Scale", 5.0f);
  // Phase (Single, TiXL default 5.0; added to hash22 output to shift cell positions)
  p.Phase = cookParam(c, "Phase", 5.0f);
  // Clamping (Vec2, TiXL default (0,1))
  p.ClampingX = cookParam(c, "Clamping.x", 0.0f);
  p.ClampingY = cookParam(c, "Clamping.y", 1.0f);
  // GainAndBias (Vec2, TiXL default (0.5,0.5) — neutral)
  p.GainX = cookParam(c, "GainAndBias.x", 0.5f);
  p.BiasY = cookParam(c, "GainAndBias.y", 0.5f);
  // Method (Int→float via IntToFloat in .t3; TiXL default 0 = Worley_F1)
  p.Method = cookParam(c, "Method", 0.0f);
  // Randomness (Single, TiXL default 12.6)
  p.Randomness = cookParam(c, "Randomness", 12.6f);
  // TextureBlend (Single, TiXL default 1.0; controls texture-overlay strength)
  p.FxTextureBlend = cookParam(c, "TextureBlend", 1.0f);
  // IsTextureValid: 0.0 = no input (generator path). Would be 1.0 if a texture is wired.
  // In the generator selftest we always pass 0.0. The filter path is future work.
  p.IsTextureValid = (c.inputTexture != nullptr) ? 1.0f : 0.0f;
  p._pad[0] = 0.0f;
  p._pad[1] = 0.0f;

  // b1 Resolution
  WorleyNoiseResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // Bind 1×1 dummy (or real inputTexture if wired).
  // In the generator path (IsTextureValid=0.0), the shader never samples this.
  // texToBind is const because c.inputTexture is `const MTL::Texture*`; the
  // dummy (non-const) converts implicitly. setFragmentTexture takes const.
  const MTL::Texture* texToBind;
  MTL::Texture* dummyTex = nullptr;
  if (c.inputTexture) {
    texToBind = c.inputTexture;
  } else {
    dummyTex  = makeWorleyNoiseDummyTex(c.dev);
    texToBind = dummyTex;
  }

  // Sampler: TiXL .t3 Wrap=Mirror → mirrored_repeat.
  // [fork-sampler-mirror] Only relevant for filter path; dummy tex is 1×1 so it doesn't matter.
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setSAddressMode(MTL::SamplerAddressModeMirrorRepeat);
  sd->setTAddressMode(MTL::SamplerAddressModeMirrorRepeat);
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  MTL::SamplerState* sampler = c.dev->newSamplerState(sd);
  sd->release();

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(texToBind, 0);
  enc->setFragmentSamplerState(sampler, 0);
  enc->setFragmentBytes(&p,   sizeof(WorleyNoiseParams),     WORLEYNOISE_Params);
  enc->setFragmentBytes(&res, sizeof(WorleyNoiseResolution), WORLEYNOISE_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  sampler->release();
  if (dummyTex) dummyTex->release();
}

}  // namespace

// Self-registration. File-scope static feeds imageFilterSpecSink() + texReg() +
// imageFilterSelfTests() during pre-main dynamic init. No shared file edited.
static const ImageFilterOp _reg_worleynoise{
    // WorleyNoise (TiXL Lib.image.generate.noise.WorleyNoise): cellular/Worley noise generator.
    // Optional Texture2D input (default null) — generator + optional overlay.
    // Kernel: WorleyNoise.hlsl — hash22 integer hash, 6 method variants (F1/F2-F1 × Euclid/Manhattan/Chebyshev),
    //   ApplyGainAndBias, Clamping, lerp ColorA/ColorB.
    // Params mirror WorleyNoise.cs/.t3 verbatim.
    // FORKS: hash22-verbatim; bias-functions-inline; no-texture-in-generator-path;
    //   sampler-mirror; cellCenter-not-tracked-F2F1; fmod-dead-code.
    {"WorleyNoise", "WorleyNoise",
     {{"out", "out", "Texture2D", false},
      // ColorA (Vec4, TiXL default ≈ white (1,1,1,1))
      {"ColorA.r", "ColorA", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ColorA.g", "ColorA.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorA.b", "ColorA.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorA.a", "ColorA.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // ColorB (Vec4, TiXL default (0,0,0,1) — black)
      {"ColorB.r", "ColorB", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ColorB.g", "ColorB.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorB.b", "ColorB.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorB.a", "ColorB.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Scale (Single, TiXL default 5.0)
      {"Scale", "Scale", "Float", true, 5.0f, 0.01f, 32.0f, Widget::Slider},
      // Stretch (Vec2, TiXL default (1,1))
      {"Stretch.x", "Stretch", "Float", true, 1.0f, 0.01f, 8.0f, Widget::Vec, {}, true, 2},
      {"Stretch.y", "Stretch.y", "Float", true, 1.0f, 0.01f, 8.0f, Widget::Vec, {}, true, 1},
      // Offset (Vec2, TiXL default (0,0))
      {"Offset.x", "Offset", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 2},
      {"Offset.y", "Offset.y", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
      // Phase (Single, TiXL default 5.0; shifts hash22 outputs → cell jitter phase)
      {"Phase", "Phase", "Float", true, 5.0f, 0.0f, 100.0f, Widget::Slider},
      // Randomness (Single, TiXL default 12.6; controls sin() amplitude for cell-center jitter)
      {"Randomness", "Randomness", "Float", true, 12.6f, 0.0f, 20.0f, Widget::Slider},
      // Clamping (Vec2, TiXL default (0,1))
      {"Clamping.x", "Clamping", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Clamping.y", "Clamping.y", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Vec, {}, true, 1},
      // GainAndBias (Vec2, TiXL default (0.5,0.5) — neutral)
      {"GainAndBias.x", "GainAndBias", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"GainAndBias.y", "GainAndBias.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Method (Int, TiXL default 0=Worley_F1; 6 variants)
      {"Method", "Method", "Float", true, 0.0f, 0.0f, 5.0f, Widget::Enum,
       {"Worley_F1", "Manhattan_F1", "Chebyshev_F1",
        "Worley_F2_F1", "Manhattan_F2_F1", "Chebyshev_F2_F1"}, true},
      // TextureBlend (Single, TiXL default 1.0; controls texture-overlay strength in filter mode)
      {"TextureBlend", "TextureBlend", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Slider},
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "WorleyNoise", cookWorleyNoise, "worleynoise", runWorleyNoiseSelfTest};

// --- WorleyNoise MATH golden ──────────────────────────────────────────────────────────────────
//
// Sub-test A (lattice plateau, hash degenerate):
//   Configuration: ColorA=(1,1,1,1), ColorB=(0,0,0,1), Scale=0, Stretch=(1,1), Offset=(0,0),
//     Phase=0, Randomness=0, Clamping=(0,1), GainAndBias=(0.5,0.5), Method=0 (Worley_F1)
//
//   Scale=0 → uv = (texCoord+0.5)*0*1 = (0,0) for ALL pixels → q=(0,0) for all pixels.
//   Phase=0, Randomness=0 → hash22(p)=(0+Phase, 0+Phase)=(0,0); g = p+0.5+0.5*sin(0)=p+0.5.
//
//   Nearest cell centers from q=(0,0) (checking 3×3 neighborhood):
//     Cells p∈{-1,0,1}×{-1,0,1}: g=p+0.5. Distance from (0,0):
//       (-1,-1)→g=(-0.5,-0.5): d=sqrt(0.5)≈0.7071
//       (-1,0) →g=(-0.5,0.5):  d=0.7071
//       (0,-1) →g=(0.5,-0.5):  d=0.7071
//       (0,0)  →g=(0.5,0.5):   d=0.7071
//       (others all > 1)
//
//   f1: first cell (-1,-1) sets f1=0.7071. Subsequent equidistant cells: d==f1 (not d<f1) → f1 stays.
//   worleyValue = f1 = 0.7071 (sqrt(0.5), exact for Euclidean Worley_F1).
//
//   ApplyGainAndBias(0.7071, (0.5,0.5)): g=0.5, b=0.5. Both ≥ 0.5.
//     g≥0.5 branch: GetSchlickBias(0.5, 0.7071); x≥0.5 → x=2*0.7071-1=0.4142;
//       GetBias(0.5, 0.4142) = 0.4142/(0*0.5858+1)=0.4142; return 0.5*0.4142+0.5=0.7071.
//     Then GetBias(0.5, 0.7071)=0.7071 (same identity).
//     ApplyGainAndBias(0.7071,(0.5,0.5)) = 0.7071 (neutral gain+bias = identity).
//
//   clamp(0.7071, 0, 1) = 0.7071.
//   mix(ColorB=(0,0,0,1), ColorA=(1,1,1,1), 0.7071) = (0.7071, 0.7071, 0.7071, 1).
//   R8 = round(0.7071*255) = round(180.31) = 180.
//
//   GOLDEN: R=G=B=180, A=255. Tolerance ±10 (GPU float rounding on sqrt+distance).
//
//   NOTE: hash22 is degenerate at Phase=0, Randomness=0 (hash output = Phase = 0; g=lattice+0.5).
//   This sub-test bites: cbuffer routing (ColorA/ColorB/Clamping/GainAndBias), the GainAndBias
//   identity property, and the lerp output. It does NOT bite the hash constants.
//   injectBug A: Randomness=12.6 (inject non-zero randomness) → cell centers shift → f1 changes
//   drastically from 0.7071 → R shifts far outside [170,190] → FAIL. Bites Randomness routing.
//
// Sub-test B (noise-live coord, hash active):
//   Configuration: ColorA=(1,1,1,1), ColorB=(0,0,0,1), Scale=0, Stretch=(1,1), Offset=(0,0),
//     Phase=5.0, Randomness=12.6, Clamping=(0,1), GainAndBias=(0.5,0.5), Method=0 (Worley_F1)
//
//   Scale=0 → q=(0,0) for all pixels. Phase=5.0, Randomness=12.6.
//
//   hash22((p.x, p.y)): uint2(int2(p)) * uint2(1597334673U, 3812015801U), XOR, * again.
//   For p=(0,0): uint2(0,0)*...=(0,0); XOR(0)=0; *=(0,0); float2(0)*UIF+Phase=(5,5).
//     g=(0.5+0.5*sin(5*12.6), 0.5+0.5*sin(5*12.6)) = (0.5+0.5*sin(63), 0.5+0.5*sin(63)).
//     sin(63 rad) ≈ sin(63-10*2π)=sin(63-62.832)=sin(0.168)≈0.1673.
//     g=(0.5837, 0.5837). d=distance((0.5837,0.5837),(0,0))=0.5837*sqrt(2)≈0.8252.
//   For p=(-1,0): uint2(0xFFFFFFFF, 0)*(1597334673,3812015801)=(2697632623,0).
//     XOR: 2697632623. *uint2(1597334673,3812015801): qx=(2697632623*1597334673)&0xFFFFFFFF,
//     qy=(2697632623*3812015801)&0xFFFFFFFF.
//     hash22 for non-zero p: distinct from (0,0) → different g → different distance.
//   [Python double-precision derivation: nearest cell = p=(-1,0) at d≈0.2200]
//
//   Independent Python derivation (double-precision, matching GPU float32 within tolerance):
//     f1=0.2200 (p=(-1,0)), f2=0.5285 (p=(-1,-1)).
//     worleyValue=0.2200 (Worley_F1). GainAndBias identity → 0.2200.
//     R8 = round(0.2200*255) = round(56.1) = 56.
//
//   GOLDEN: R=G=B≈56, A=255. Tolerance ±10 (GPU float32 vs Python float64, hash chain accumulation).
//
//   Window [46, 66] bites the hash constants:
//     Correct constants (1597334673U, 3812015801U): nearest cell p=(-1,0) → d≈0.220 → R=56 (IN).
//     Swap constants (3812015801U first, 1597334673U second): changes XOR result → different f1 →
//     R shifts significantly (>>10 counts). Any hash constant corruption → R outside [46,66] → FAIL.
//
//   injectBug B: Randomness=0.0 → hash output not used in sin (g=p+0.5) → all cells at lattice
//     → f1=0.7071 (lattice Sub-test A value) → R=180 → far outside [46,66] → FAIL. ✓
//     This injectBug proves the test path is alive and Randomness routes to the shader correctly.
int runWorleyNoiseSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-worleynoise] FAIL: no metallib\n");
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

  // ── Sub-test A: lattice probe (Phase=0, Randomness=0 → g=p+0.5, f1=sqrt(0.5)≈0.7071) ────────
  // hash22 degenerate (Phase=0 → hash=(0,0); Randomness=0 → sin(0)=0 → g=lattice+0.5).
  // Bites: cbuffer routing for ColorA/ColorB, GainAndBias neutral-identity, Clamping, lerp output.
  // Does NOT bite hash22 constants (hash is degenerate here).
  // injectBug A: Randomness=12.6 → cell centers shift → f1 ≠ 0.7071 → R outside [170,190] → FAIL.
  {
    std::map<std::string, float> params;
    params["ColorA.r"] = 1.0f; params["ColorA.g"] = 1.0f;
    params["ColorA.b"] = 1.0f; params["ColorA.a"] = 1.0f;
    params["ColorB.r"] = 0.0f; params["ColorB.g"] = 0.0f;
    params["ColorB.b"] = 0.0f; params["ColorB.a"] = 1.0f;
    params["Scale"]       = 0.0f;   // all pixels → q=(0,0)
    params["Stretch.x"]   = 1.0f;   params["Stretch.y"] = 1.0f;
    params["Offset.x"]    = 0.0f;   params["Offset.y"]  = 0.0f;
    params["Phase"]       = 0.0f;   // hash output = Phase = 0 → degenerate
    params["Randomness"]  = injectBug ? 12.6f : 0.0f;  // inject: shift cell centers → FAIL
    params["Clamping.x"]  = 0.0f;   params["Clamping.y"] = 1.0f;
    params["GainAndBias.x"] = 0.5f; params["GainAndBias.y"] = 0.5f;
    params["Method"]      = 0.0f;   // Worley_F1

    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.inputTexture = nullptr; c.output = dst; c.params = &params;
    cookWorleyNoise(c);

    std::vector<uint8_t> out((size_t)W * H * 4, 0);
    dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

    uint32_t cx = W / 2, cy = H / 2;
    int cR = readPixel(out, cx, cy, 0);
    int cG = readPixel(out, cx, cy, 1);
    int cB = readPixel(out, cx, cy, 2);
    int cA = readPixel(out, cx, cy, 3);

    // GOLDEN: R≈180 (sqrt(0.5)*255≈180.3; tolerance ±10 for GPU float rounding).
    // Derived: Phase=0, Randomness=0 → f1=sqrt(0.5)=0.7071 → GainAndBias identity → R=180.
    // injectBug (Randomness=12.6): cell centers shift → f1≠0.7071 → R outside [170,190] → FAIL.
    const int kExpR = 180, kTol = 10;
    bool ok = (cR >= kExpR - kTol && cR <= kExpR + kTol &&
               cG >= kExpR - kTol && cG <= kExpR + kTol &&
               cB >= kExpR - kTol && cB <= kExpR + kTol &&
               cA >= 250);
    if (!ok) failures++;
    printf("[selftest-worleynoise] A lattice(%d,%d) R=%d G=%d B=%d A=%d "
           "(expect R~%d±%d, hash-degenerate) -> %s%s\n",
           cx, cy, cR, cG, cB, cA, kExpR, kTol,
           ok ? "PASS" : "FAIL",
           injectBug ? " [inject=Randomness12.6]" : "");
  }

  // ── Sub-test B: noise-live coord (Phase=5.0, Randomness=12.6 → non-degenerate hash) ─────────
  // Scale=0 → q=(0,0) for all pixels. Non-zero p cells (p≠(0,0)) use genuine integer hash.
  // Python double-precision derivation: nearest cell p=(-1,0) → d≈0.2200 → R8≈56.
  // Bites: hash22 constants (non-zero integer input p=(-1,0) exercises the full multiply+XOR chain).
  // Window [46,66] inherently bites hash constants — any constant corruption shifts R outside.
  // injectBug B: Randomness=0.0 → g=p+0.5 (lattice) → f1=0.7071 → R=180 → outside [46,66] → FAIL.
  // This proves the test path is alive and Randomness routes to the shader correctly.
  {
    std::map<std::string, float> params;
    params["ColorA.r"] = 1.0f; params["ColorA.g"] = 1.0f;
    params["ColorA.b"] = 1.0f; params["ColorA.a"] = 1.0f;
    params["ColorB.r"] = 0.0f; params["ColorB.g"] = 0.0f;
    params["ColorB.b"] = 0.0f; params["ColorB.a"] = 1.0f;
    params["Scale"]       = 0.0f;          // all pixels → q=(0,0)
    params["Stretch.x"]   = 1.0f;          params["Stretch.y"] = 1.0f;
    params["Offset.x"]    = 0.0f;          params["Offset.y"]  = 0.0f;
    params["Phase"]       = 5.0f;          // non-degenerate hash output (adds 5.0 after float2(q)*UIF)
    params["Randomness"]  = injectBug ? 0.0f : 12.6f;  // inject: zero → g=lattice → f1=0.7071 → FAIL
    params["Clamping.x"]  = 0.0f;          params["Clamping.y"] = 1.0f;
    params["GainAndBias.x"] = 0.5f;        params["GainAndBias.y"] = 0.5f;
    params["Method"]      = 0.0f;          // Worley_F1

    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 2; c.inputTexture = nullptr; c.output = dst; c.params = &params;
    cookWorleyNoise(c);

    std::vector<uint8_t> out((size_t)W * H * 4, 0);
    dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

    uint32_t cx = W / 2, cy = H / 2;
    int cR = readPixel(out, cx, cy, 0);
    int cG = readPixel(out, cx, cy, 1);
    int cB = readPixel(out, cx, cy, 2);
    int cA = readPixel(out, cx, cy, 3);

    // GOLDEN: R≈56 (Python double-precision: f1≈0.2200, R8=round(0.2200*255)=56).
    // Window [46,66] (±10): correct hash → R=56 (IN); hash constant corruption → R shifts >> 10 → FAIL.
    // Sub-test B inherently bites hash22 constants because p=(-1,0) has non-zero uint2 input.
    // injectBug (Randomness=0.0): g=lattice+0.5 → f1=0.7071 → R=180 → outside [46,66] → FAIL.
    const int kExpR2 = 56, kTol2 = 10;
    bool ok2 = (cR >= kExpR2 - kTol2 && cR <= kExpR2 + kTol2 &&
                cG >= kExpR2 - kTol2 && cG <= kExpR2 + kTol2 &&
                cB >= kExpR2 - kTol2 && cB <= kExpR2 + kTol2 &&
                cA >= 250);
    if (!ok2) failures++;
    printf("[selftest-worleynoise] B noise-live(%d,%d) R=%d G=%d B=%d A=%d "
           "(expect R~%d±%d, hash-active window) -> %s%s\n",
           cx, cy, cR, cG, cB, cA, kExpR2, kTol2,
           ok2 ? "PASS" : "FAIL",
           injectBug ? " [inject=Randomness0]" : "");
  }

  dst->release();
  lib->release(); q->release(); dev->release(); pool->release();

  if (failures == 0) {
    printf("[selftest-worleynoise] ALL PASS\n");
    return 0;
  } else {
    printf("[selftest-worleynoise] FAIL (%d sub-test(s) failed)\n", failures);
    return 1;
  }
}

}  // namespace sw
