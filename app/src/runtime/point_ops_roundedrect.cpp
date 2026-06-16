// RoundedRect image generator op (Phase C leaf).
// TiXL authority: Operators/Lib/image/generate/basic/RoundedRect.cs (slot declarations + types) +
//   RoundedRect.t3 (defaults: Color=(1,1,1,1), Background=(0,0,0,0), StrokeColor=(1,1,1,1),
//   Position=(0,0), Stretch=(1,1), Scale=0.5, Rotate=0.0, Round=0.5, Stroke=0.0, Feather=0.0,
//   FeatherBias=-0.001, Image=null) +
//   Assets/shaders/img/generate/RoundedRect.hlsl (single-pass SDF rounded-rectangle generator
//   with optional composite over an upstream image).
//
// Port class: _ImageFxShaderSetupStatic (bd0b9c5b) → single-pass fragment shader
//   (roundedrect_vs / roundedrect_fs).
//
// STEP-0 portability check (PASSED):
//   ① Single optional Texture2D input (Image, TiXL default null). No multi-image seam.
//   ② No gradient-widget, no curve-LUT, no asset-texture, no mip-gen seam.
//   ③ Not a .t3 compound — _ImageFxShaderSetupStatic with direct cbuffer feed.
//   ④ STEP-0 backward-trace of RoundedRect.t3 connections to slot 4ef6f204 (FloatsToBuffer):
//      children = Vector4Components×3 (Color/StrokeColor/Background → 4 floats each),
//               + Vector2Components×2 (Stretch/Position → 2 floats each),
//               + direct float×6 (Scale/Round/Stroke/Feather/FeatherBias/Rotate).
//      NO intermediate math nodes (no Multiply, no IntToFloat). Direct feed — NOT the
//      DirectionalBlur / _multiImageFxSetup trap.
//      cbuffer order matches RoundedRect.hlsl b0 verbatim.
//
// cbuffer b0 field order (RoundedRect.hlsl lines 1-18, confirmed by .t3 connection order):
//   float4 Fill       (Color, (1,1,1,1))
//   float4 OutlineColor (StrokeColor, (1,1,1,1))
//   float4 Background ((0,0,0,0))
//   float2 Stretch    ((1,1))
//   float2 Center     (Position, (0,0))
//   float  Scale      (0.5)
//   float  Round      (0.5)
//   float  Stroke     (0.0)
//   float  Feather    (0.0)
//   float  GradientBias (FeatherBias, -0.001)
//   float  Rotate     (0.0)
//   float  IsTextureValid (host-injected)
//   float  _pad       → 92 bytes → pad to 96 (6 × 16-byte registers)
//
// Generator convention (Cut 61): null-input → host binds 1×1 transparent-black dummy, same as
//   NGon / SinForm / Rings. Shader always receives a valid texture2d handle.
//
// Forks (named):
//   [fork-IsTextureValid]  _ImageFxShaderSetupStatic injects IsTextureValid at runtime. We
//     replicate: host sets 1.0 if c.inputTexture != null, else 0.0.
//   [fork-sampler-repeat]  Linear+Repeat sampler for upstream ImageA, matching TiXL
//     _ImageFxShaderSetupStatic.t3 defaults (AddressU/V=Wrap, Filter=MinMagMipLinear).
//   [fork-GradientBias-branch]  TiXL default FeatherBias=-0.001 (negative branch of the pow
//     expression). Ported verbatim: negative → 1-pow(clamp(1-d,0,10),-GradientBias+1).
//   [fork-generator-dummy]  1×1 transparent-black dummy for the no-input case.
//
// Self-contained leaf: cookRoundedRect + ImageFilterOp self-registration + runRoundedRectSelfTest.
// CMake CONFIGURE_DEPENDS glob auto-picks point_ops_roundedrect.cpp and shaders/roundedrect.metal
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
#include "runtime/image_filter_op_registry.h"   // ImageFilterOp self-registration
#include "runtime/point_graph.h"                // TexCookCtx, cookParam
#include "runtime/roundedrect_params.h"         // RoundedRectParams/Resolution, RRECT_* bindings
#include "runtime/tex_op_cache.h"               // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration so the file-scope ImageFilterOp registrar can reference runRoundedRectSelfTest.
int runRoundedRectSelfTest(bool injectBug);

namespace {

// Helper: create a 1×1 transparent-black dummy texture for the no-input case.
// RoundedRect is a generator (TiXL Image=null default). We must still run the shader;
// a 1×1 dummy gives roundedrect_fs a valid texture2d handle → orgColor=(0,0,0,0).
// [fork-generator-dummy, Cut 61 — same as NGon / SinForm / Rings]
static MTL::Texture* makeRoundedRectDummyTex(MTL::Device* dev) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  const uint8_t px[4] = {0, 0, 0, 0};
  t->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, px, 4);
  return t;
}

// RoundedRect texture op: single fullscreen pass. Optionally reads c.inputTexture (if wired),
// always writes c.output. If no upstream: rounded rectangle on transparent black.
void cookRoundedRect(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "roundedrect_vs", "roundedrect_fs", fmt);
  if (!rps) return;

  // Sampler: linear+Repeat. TiXL _ImageFxShaderSetupStatic.t3 defaults:
  //   AddressU/V = Wrap (DX TextureAddressMode.Wrap = repeat), Filter = MinMagMipLinear.
  // [fork-sampler-repeat]
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeRepeat);
  sd->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // --- b0 params (TiXL RoundedRect.cs/.t3 defaults) ---
  RoundedRectParams p{};
  // Fill (Color, Vec4, TiXL default (1,1,1,1) — white)
  p.FillR = cookParam(c, "Color.r", 1.0f);
  p.FillG = cookParam(c, "Color.g", 1.0f);
  p.FillB = cookParam(c, "Color.b", 1.0f);
  p.FillA = cookParam(c, "Color.a", 1.0f);
  // OutlineColor (StrokeColor, Vec4, TiXL default (1,1,1,1) — white)
  p.OutlineR = cookParam(c, "StrokeColor.r", 1.0f);
  p.OutlineG = cookParam(c, "StrokeColor.g", 1.0f);
  p.OutlineB = cookParam(c, "StrokeColor.b", 1.0f);
  p.OutlineA = cookParam(c, "StrokeColor.a", 1.0f);
  // Background (Vec4, TiXL default (0,0,0,0) — transparent)
  p.BgR = cookParam(c, "Background.r", 0.0f);
  p.BgG = cookParam(c, "Background.g", 0.0f);
  p.BgB = cookParam(c, "Background.b", 0.0f);
  p.BgA = cookParam(c, "Background.a", 0.0f);
  // Stretch (Vec2, TiXL default (1,1))
  p.StretchX = cookParam(c, "Stretch.x", 1.0f);
  p.StretchY = cookParam(c, "Stretch.y", 1.0f);
  // Center / Position (Vec2, TiXL default (0,0))
  p.CenterX = cookParam(c, "Position.x", 0.0f);
  p.CenterY = cookParam(c, "Position.y", 0.0f);
  // Shape scalars
  p.Scale        = cookParam(c, "Scale",       0.5f);
  p.Round        = cookParam(c, "Round",        0.5f);
  p.Stroke       = cookParam(c, "Stroke",       0.0f);
  p.Feather      = cookParam(c, "Feather",      0.0f);
  p.GradientBias = cookParam(c, "FeatherBias", -0.001f);  // FeatherBias in .cs = GradientBias in hlsl
  p.Rotate       = cookParam(c, "Rotate",       0.0f);    // TiXL t3 default 0.0 degrees
  // IsTextureValid: 1.0 if upstream wired, 0.0 if not. [fork-IsTextureValid]
  p.IsTextureValid = (c.inputTexture != nullptr) ? 1.0f : 0.0f;
  p._pad = 0.0f;

  // b1 Resolution
  RoundedRectResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // Bind input texture (or 1×1 black dummy when no upstream is wired). [fork-generator-dummy]
  MTL::Texture* dummyTex = nullptr;
  const MTL::Texture* inputTex = c.inputTexture;
  if (!inputTex) {
    dummyTex = makeRoundedRectDummyTex(c.dev);
    inputTex = dummyTex;
  }

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(inputTex), 0);
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p,   sizeof(RoundedRectParams),     RRECT_Params);
  enc->setFragmentBytes(&res, sizeof(RoundedRectResolution), RRECT_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();
  if (dummyTex) dummyTex->release();
}

}  // namespace

// Self-registration. File-scope static feeds imageFilterSpecSink() + texReg() +
// imageFilterSelfTests() during pre-main dynamic init. No shared file edited.
static const ImageFilterOp _reg_roundedrect{
    // RoundedRect (TiXL Lib.image.generate.basic.RoundedRect): SDF rounded-rectangle generator.
    // Optional Texture2D in (Image, default null) → Texture2D out.
    // When no Image: rounded rectangle composited on transparent black.
    // Kernel: RoundedRect.hlsl — sdBox SDF + roundOffset corner rounding, GradientBias ramp,
    //   feather smoothstep, stroke/outline composite, optional IsTextureValid alpha-blend.
    // Params mirror RoundedRect.cs/.t3 verbatim.
    // FORKS (named): generator dummy; fork-sampler-repeat; fork-IsTextureValid; fork-GradientBias-branch.
    {"RoundedRect", "RoundedRect",
     {// Optional Image input (TiXL default null — generator mode draws on transparent black)
      {"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Color / Fill (Vec4, TiXL t3 default (1,1,1,1) — white interior)
      {"Color.r", "Color", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Color.g", "Color.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Color.b", "Color.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Color.a", "Color.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // StrokeColor / OutlineColor (Vec4, TiXL t3 default (1,1,1,1) — white stroke)
      {"StrokeColor.r", "StrokeColor", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"StrokeColor.g", "StrokeColor.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"StrokeColor.b", "StrokeColor.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"StrokeColor.a", "StrokeColor.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Background (Vec4, TiXL t3 default (0,0,0,0) — transparent exterior)
      {"Background.r", "Background", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Background.g", "Background.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.b", "Background.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.a", "Background.a", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Position / Center (Vec2, TiXL t3 default (0,0))
      {"Position.x", "Position", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Position.y", "Position.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Stretch (Vec2, TiXL t3 default (1,1))
      {"Stretch.x", "Stretch", "Float", true, 1.0f, 0.0f, 4.0f, Widget::Vec, {}, true, 2},
      {"Stretch.y", "Stretch.y", "Float", true, 1.0f, 0.0f, 4.0f, Widget::Vec, {}, true, 1},
      // Scale (Single, TiXL t3 default 0.5)
      {"Scale", "Scale", "Float", true, 0.5f, 0.0f, 2.0f, Widget::Slider},
      // Rotate (Single, TiXL t3 default 0.0; degrees)
      {"Rotate", "Rotate", "Float", true, 0.0f, -180.0f, 180.0f},
      // Round (Single, TiXL t3 default 0.5; 0=sharp corners, 1=full circle)
      {"Round", "Round", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Slider},
      // Stroke (Single, TiXL t3 default 0.0; outline width relative to minSize)
      {"Stroke", "Stroke", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider},
      // Feather (Single, TiXL t3 default 0.0; edge softness)
      {"Feather", "Feather", "Float", true, 0.0f, 0.0f, 0.5f, Widget::Slider},
      // FeatherBias (Single, TiXL t3 default -0.001; GradientBias in shader = power-curve ramp)
      {"FeatherBias", "FeatherBias", "Float", true, -0.001f, -8.0f, 8.0f, Widget::Slider},
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "RoundedRect", cookRoundedRect, "roundedrect", runRoundedRectSelfTest};

// --- RoundedRect MATH golden -------------------------------------------------------------------------
// Ground truth independently derived from TiXL RoundedRect.hlsl (hand-calculation), NOT self-capture.
//
// Configuration: default params (Color=(1,1,1,1), Background=(0,0,0,0), StrokeColor=(1,1,1,1),
// Position=(0,0), Stretch=(1,1), Scale=0.5, Rotate=0.0, Round=0.5, Stroke=0.0, Feather=0.0,
// FeatherBias=-0.001). No input texture (generator mode: IsTextureValid=0.0).
// Square 128×128 texture (aspectRatio=1.0).
//
// ── Geometry pre-calc ─────────────────────────────────────────────────────────────────────────
// Rotate=0.0 → imageRotationRad = (-0-90)/180 * π = -π/2
// sina = sin(-(-π/2) - π/2) = sin(0) = 0
// cosa = cos(0) = 1
// Rotation matrix: p → (1*px - 0*py, 1*py + 0*px) = (px, py)  [identity at Rotate=0]
// Center=(0,0) → p -= Center*(1,-1) = no-op
//
// size = Stretch * Scale = (1,1) * 0.5 = (0.5, 0.5)
// minSize = 0.5
// roundOffset = minSize * Round = 0.5 * 0.5 = 0.25
// rsize = size - roundOffset = (0.25, 0.25)
// sdBox half-extents = rsize/2 = (0.125, 0.125)
//
// ── PIN A: center pixel (64,64), UV=(0.5, 0.5) ─────────────────────────────────────────────
// p_init = (0.5-0.5, 0.5-0.5) = (0, 0); p_rot = (0, 0).
// sdBox((0,0), (0.125,0.125)):
//   d = abs((0,0)) - (0.125,0.125) = (-0.125,-0.125)
//   max(d,0) = (0,0); length=0; min(max(-0.125,-0.125),0) = -0.125
//   d_box = -0.125   [well inside rectangle]
// GradientBias=-0.001 (negative branch):
//   d = 1 - pow(clamp(1-(-0.125),0,10), 0.001+1)
//     = 1 - pow(1.125, 1.001) ≈ 1 - 1.125125 ≈ -0.125125
// feather = 0.5 * 0.0 / 2 = 0   → smoothstep with feather=0 = step function
// dInside = smoothstep(0, 0, -0.125125 - 0.125) = smoothstep(0,0,-0.250125) = 0.0 [< 0]
// stroke = max(0.0 * 0.5, 0) = 0; showStroke = 0
// outlineColor = mix(Fill, OutlineColor, 0) = Fill = (1,1,1,1)
// dStroke = smoothstep(0, 0, -0.125125 - 0.125 - 0) = 0.0
// cInside = mix(Fill, Fill, 0.0) = Fill = (1,1,1,1)
// cStroke = mix(Background, Fill, 1.0) = Fill = (1,1,1,1)
// c = mix(cInside, cStroke, 0.0) = cInside = (1,1,1,1)
// IsTextureValid=0 → return (1,1,1,1)  →  WHITE
// GOLDEN ASSERTION A: center pixel (64,64) is WHITE (R≥240, G≥240, B≥240, A≥240).
//
// ── PIN B: corner pixel (4,4), UV≈(0.0313, 0.0313) ─────────────────────────────────────────
// p_init = (0.03125-0.5, 0.03125-0.5) = (-0.46875, -0.46875)
// sdBox((-0.46875,-0.46875), (0.125,0.125)):
//   d = (0.46875-0.125, 0.46875-0.125) = (0.34375, 0.34375)
//   length(max(d,0)) = 0.34375*√2 ≈ 0.4861; min(max(d.x,d.y),0) = 0  [since 0.34375>0]
//   d_box = 0.4861   [far outside rectangle]
// GradientBias=-0.001 (negative branch):
//   d = 1 - pow(clamp(1-0.4861,0,10), 1.001) ≈ 1 - pow(0.5139, 1.001) ≈ 1 - 0.513 ≈ 0.487
// dInside = smoothstep(0, 0, 0.487 - 0.125) = step(0, 0.362) = 1.0   [> 0]
// dStroke = smoothstep(0, 0, 0.487 - 0.125 - 0) = 1.0
// cInside = mix(Fill, Fill, 1.0) = Fill = (1,1,1,1)   [outlineColor=Fill since showStroke=0]
// cStroke = mix(Background, Fill, 1-1.0) = mix(Background, Fill, 0) = Background = (0,0,0,0)
// c = mix(cInside, cStroke, 1.0) = cStroke = Background = (0,0,0,0)
// IsTextureValid=0 → return (0,0,0,0)  →  TRANSPARENT
// GOLDEN ASSERTION B: corner pixel (4,4) is TRANSPARENT (R<10, A<10).
//
// ── injectBug ───────────────────────────────────────────────────────────────────────────────
// Inject: Background = (1,1,1,1). At corner (4,4): cStroke = Background = (1,1,1,1).
// c = cStroke = (1,1,1,1) → A=255 → bA < 10 FAILS → RED.
// Assertion B catches the bug; assertion A still passes (center unaffected: dInside=0).
int runRoundedRectSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-roundedrect] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(td);

  // Default params — rounded rectangle on transparent black.
  // injectBug: Background=white → corner becomes white → assertion B (A<10) FAILS.
  std::map<std::string, float> params;
  params["Color.r"] = 1.0f; params["Color.g"] = 1.0f;
  params["Color.b"] = 1.0f; params["Color.a"] = 1.0f;
  params["StrokeColor.r"] = 1.0f; params["StrokeColor.g"] = 1.0f;
  params["StrokeColor.b"] = 1.0f; params["StrokeColor.a"] = 1.0f;
  // injectBug flips Background to opaque white → corner TRANSPARENT assertion fails
  params["Background.r"] = injectBug ? 1.0f : 0.0f;
  params["Background.g"] = injectBug ? 1.0f : 0.0f;
  params["Background.b"] = injectBug ? 1.0f : 0.0f;
  params["Background.a"] = injectBug ? 1.0f : 0.0f;
  params["Position.x"] = 0.0f; params["Position.y"] = 0.0f;
  params["Stretch.x"]  = 1.0f; params["Stretch.y"]  = 1.0f;
  params["Scale"]      = 0.5f;
  params["Rotate"]     = 0.0f;
  params["Round"]      = 0.5f;
  params["Stroke"]     = 0.0f;
  params["Feather"]    = 0.0f;
  params["FeatherBias"]= -0.001f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1;
  c.inputTexture = nullptr;  // generator mode: no upstream Image
  c.output = dst;
  c.params = &params;
  cookRoundedRect(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  auto readPx = [&](uint32_t x, uint32_t y, int ch) -> int {
    return (int)out[((size_t)y * W + x) * 4 + ch];
  };

  // PIN A: center pixel (64,64) — inside the rounded rect → WHITE (RGBA all ≥ 240).
  // Hand-derived: sdBox=-0.125 → d≈-0.125 → dInside=0 → c=Fill=white.
  // injectBug changes Background (not Fill) → center stays white → A passes.
  int aR = readPx(64, 64, 0), aG = readPx(64, 64, 1);
  int aB = readPx(64, 64, 2), aA = readPx(64, 64, 3);

  // PIN B: corner pixel (4,4) — well outside the rounded rect → TRANSPARENT (R<10, A<10).
  // Hand-derived: sdBox≈0.486 → d≈0.487 → dInside=1, dStroke=1 → c=Background=(0,0,0,0).
  // injectBug makes Background=(1,1,1,1) → corner becomes white → bA<10 FAILS → RED.
  int bR = readPx(4, 4, 0), bA = readPx(4, 4, 3);

  bool pinA = (aR >= 240 && aG >= 240 && aB >= 240 && aA >= 240);  // white interior
  bool pinB = (bR < 10 && bA < 10);                                  // transparent exterior

  bool pass = pinA && pinB;
  printf("[selftest-roundedrect] "
         "pinA(center 64,64) R=%d G=%d B=%d A=%d (want white ≥240) %s | "
         "pinB(corner 4,4) R=%d A=%d (want transparent <10) %s "
         "-> %s\n",
         aR, aG, aB, aA, pinA ? "OK" : "FAIL",
         bR, bA, pinB ? "OK" : "FAIL",
         pass ? "PASS" : "FAIL");

  dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
