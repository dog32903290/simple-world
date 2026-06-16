// NGon image generator op (Phase C leaf).
// TiXL authority: Operators/Lib/image/generate/basic/NGon.cs (slot declarations + types) +
//   NGon.t3 (defaults: Sides=3, Radius=0.25, Rotate=-90, Feather=0.05, Round=0.0,
//   FeatherBias=0.0, Curvature=0.0, Blades=0.0, Fill=(1,1,1,1), Background=(0,0,0,0),
//   Position=(0,0), BlendMode=0, Image=null) +
//   Assets/shaders/img/generate/NGon.hlsl (single-pass SDF n-gon generator with optional
//   BlendMode composite over an upstream image).
//
// Port class: _ImageFxShaderSetupStatic (bd0b9c5b) → single-pass fragment shader (ngon_vs/ngon_fs).
//
// STEP-0 portability check (PASSED):
//   ① Single optional Texture2D input (Image, default null). No multi-image seam.
//   ② No gradient-widget, no curve-LUT, no asset-texture, no mip-gen seam.
//   ③ Not a .t3 compound — _ImageFxShaderSetupStatic with direct cbuffer feed.
//   ④ STEP-0 backward-trace of NGon.t3 connections to slot 4ef6f204 (FloatsToBuffer):
//      children = Vector4Components (Fill→4 floats), Vector4Components (Background→4 floats),
//      Vector2Components×2 (Position→2 floats, ac43f0fa/f4e274bb), IntToFloat (BlendMode→float).
//      NO intermediate math nodes (no Multiply, no IntToFloat for anything except BlendMode).
//      cbuffer order matches NGon.hlsl b0 verbatim. NOT the DirectionalBlur trap.
//
// Generator convention (Cut 61): null-input → host binds 1×1 transparent-black dummy, same as
//   SinForm / Rings. Shader always receives a valid texture2d handle.
//
// Forks (named):
//   [fork-IsTextureValid]  _ImageFxShaderSetupStatic injects IsTextureValid at runtime. We
//     replicate: host sets 1.0 if c.inputTexture != null, else 0.0.
//   [fork-sampler-repeat]  Linear+Repeat sampler for upstream ImageA, matching TiXL
//     _ImageFxShaderSetupStatic.t3 defaults (AddressU/V=Wrap, Filter=MinMagMipLinear).
//   [fork-mod-floor]  NGon.hlsl defines mod() as floor-based; ported to sw_mod() in ngon.metal.
//     The HLSL `%` operator is fmod; the explicit mod() macro is sw_mod. See ngon.metal notes.
//   [fork-blend-inline]  BlendColors inlined verbatim (same as rings.metal/starglowstreaks.metal).
//
// Self-contained leaf: cookNGon + ImageFilterOp self-registration + runNGonSelfTest.
// CMake CONFIGURE_DEPENDS glob auto-picks point_ops_ngon.cpp and shaders/ngon.metal — no
// CMake edits needed.
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
#include "runtime/ngon_params.h"               // NGonParams/Resolution, NGON_* bindings
#include "runtime/point_graph.h"               // TexCookCtx, cookParam
#include "runtime/tex_op_cache.h"              // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration so the file-scope ImageFilterOp registrar can reference runNGonSelfTest.
int runNGonSelfTest(bool injectBug);

namespace {

// Helper: create a 1×1 transparent-black dummy texture for the no-input case.
// NGon is a generator (TiXL Image=null default). We must still run the shader;
// a 1×1 dummy gives ngon_fs a valid texture2d handle → orgColor=(0,0,0,0).
// [generator-dummy convention, Cut 61 — same as sinform, rings]
static MTL::Texture* makeNGonDummyTex(MTL::Device* dev) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  const uint8_t px[4] = {0, 0, 0, 0};
  t->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, px, 4);
  return t;
}

// NGon texture op: single fullscreen pass. Optionally reads c.inputTexture (if wired), always
// writes c.output. If no upstream: N-gon pattern on transparent black.
void cookNGon(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "ngon_vs", "ngon_fs", fmt);
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

  // --- b0 params (TiXL NGon.cs/.t3 defaults) ---
  NGonParams p{};
  // Fill (Vec4, TiXL default (1,1,1,1) — white)
  p.FillR = cookParam(c, "Fill.r", 1.0f);
  p.FillG = cookParam(c, "Fill.g", 1.0f);
  p.FillB = cookParam(c, "Fill.b", 1.0f);
  p.FillA = cookParam(c, "Fill.a", 1.0f);
  // Background (Vec4, TiXL default (0,0,0,0) — transparent)
  p.BgR = cookParam(c, "Background.r", 0.0f);
  p.BgG = cookParam(c, "Background.g", 0.0f);
  p.BgB = cookParam(c, "Background.b", 0.0f);
  p.BgA = cookParam(c, "Background.a", 0.0f);
  // Position (Vec2, TiXL default (0,0))
  p.PositionX = cookParam(c, "Position.x", 0.0f);
  p.PositionY = cookParam(c, "Position.y", 0.0f);
  // Shape params
  p.Round       = cookParam(c, "Round",       0.0f);
  p.Feather     = cookParam(c, "Feather",     0.05f);
  p.GradientBias= cookParam(c, "FeatherBias", 0.0f);   // FeatherBias in .cs = GradientBias in hlsl
  p.Rotate      = cookParam(c, "Rotate",     -90.0f);  // TiXL t3 default -90.0 degrees
  p.Sides       = cookParam(c, "Sides",       3.0f);   // triangle
  p.Radius      = cookParam(c, "Radius",      0.25f);
  p.Curvature   = cookParam(c, "Curvature",   0.0f);
  p.Blades      = cookParam(c, "Blades",      0.0f);
  // BlendMode (Int→float via IntToFloat in .t3, TiXL default 0 = normal blend)
  p.BlendMode   = cookParam(c, "BlendMode",   0.0f);
  // IsTextureValid: 1.0 if upstream wired, 0.0 if not. [fork-IsTextureValid]
  p.IsTextureValid = (c.inputTexture != nullptr) ? 1.0f : 0.0f;

  // b1 Resolution
  NGonResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // Bind input texture (or 1×1 black dummy when no upstream is wired). [generator-dummy, Cut 61]
  MTL::Texture* dummyTex = nullptr;
  const MTL::Texture* inputTex = c.inputTexture;
  if (!inputTex) {
    dummyTex = makeNGonDummyTex(c.dev);
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
  enc->setFragmentBytes(&p,   sizeof(NGonParams),     NGON_Params);
  enc->setFragmentBytes(&res, sizeof(NGonResolution), NGON_Resolution);
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
static const ImageFilterOp _reg_ngon{
    // NGon (TiXL Lib.image.generate.basic.NGon): SDF-based N-sided polygon generator.
    // Optional Texture2D in (Image, default null) → Texture2D out. When no Image: polygon on black.
    // Kernel: NGon.hlsl — sdNgon SDF with radial-repeat, blades/curvature, smoothstep feather
    //   (Round+Feather), GradientBias ramp, lerp Fill/Background, BlendColors composite.
    // Params mirror NGon.cs/.t3 verbatim.
    // FORKS (named): generator dummy; floor-mod in sdNgon; BlendColors inline; IsTextureValid host-set.
    {"NGon", "NGon",
     {// Optional Image input (TiXL default null — generator mode draws on transparent black)
      {"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Fill (Vec4, TiXL t3 default (1,1,1,1) — white polygon interior)
      {"Fill.r", "Fill", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Fill.g", "Fill.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Fill.b", "Fill.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Fill.a", "Fill.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Background (Vec4, TiXL t3 default (0,0,0,0) — transparent exterior)
      {"Background.r", "Background", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Background.g", "Background.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.b", "Background.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.a", "Background.a", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Sides (Single, TiXL t3 default 3.0 — triangle; 6=hex, etc.)
      {"Sides", "Sides", "Float", true, 3.0f, 3.0f, 20.0f, Widget::Slider},
      // Radius (Single, TiXL t3 default 0.25)
      {"Radius", "Radius", "Float", true, 0.25f, 0.0f, 1.0f, Widget::Slider},
      // Curvature (Single, TiXL t3 default 0.0; pinches/bulges sides)
      {"Curvature", "Curvature", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider},
      // Blades (Single, TiXL t3 default 0.0; iris-blade star effect)
      {"Blades", "Blades", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Slider},
      // Feather (Single, TiXL t3 default 0.05; edge softness)
      {"Feather", "Feather", "Float", true, 0.05f, 0.0f, 0.5f, Widget::Slider},
      // Round (Single, TiXL t3 default 0.0; SDF offset = corner rounding)
      {"Round", "Round", "Float", true, 0.0f, -0.5f, 0.5f, Widget::Slider},
      // FeatherBias (Single, TiXL t3 default 0.0; GradientBias in shader = power-curve ramp)
      {"FeatherBias", "FeatherBias", "Float", true, 0.0f, -8.0f, 8.0f, Widget::Slider},
      // Position (Vec2, TiXL t3 default (0,0))
      {"Position.x", "Position", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Position.y", "Position.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Rotate (Single, TiXL t3 default -90.0; degrees; default orients triangle upward)
      {"Rotate", "Rotate", "Float", true, -90.0f, -180.0f, 180.0f},
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
    "NGon", cookNGon, "ngon", runNGonSelfTest};

// --- NGon MATH golden -------------------------------------------------------------------------
// Ground truth independently derived from TiXL NGon.hlsl (hand-calculation), NOT from the port.
//
// Configuration: default params (Fill=white, Background=transparent, Sides=3, Radius=0.25,
// Round=0, Feather=0.05, FeatherBias=0, Rotate=-90, Curvature=0, Blades=0, Position=(0,0)).
// No input texture (generator mode: IsTextureValid=0). Square 128×128 texture (aspectRatio=1.0).
//
// ── Rotation pre-calc for Rotate=-90 degrees ──────────────────────────────────────────────
// imageRotationRad = (-(-90) - 90) / 180 * π = 0/180 * π = 0
// sina = sin(-0 - π/2) = sin(-π/2) = -1
// cosa = cos(-0 - π/2) = cos(-π/2) = 0
// Rotation matrix applied to (px, py):
//   new_px = cosa*px - sina*py = 0*px - (-1)*py = py
//   new_py = cosa*py + sina*px = 0*py + (-1)*px = -px
// So p = (py, -px) after the rotation. With p initially = texCoord - (0.5, 0.5):
//   p_rot = (py_init, -px_init) + Position.yx = (py_init, -px_init) + (0, 0) = (py_init, -px_init)
//
// ── PIN A: center pixel at pixel (64,64), UV=(0.5, 0.5) ───────────────────────────────────
// p_init = (0.5-0.5, 0.5-0.5) = (0, 0); p_rot = (0, 0).
// sdNgon((0,0), 0.25, 3, 0, 0):
//   polar: atan2(0,0) = 0, len = 0. rp = (0, 0).
//   sw_mod(0 + 1/6, 1/3) - 1/6 = sw_mod(1/6, 1/3) - 1/6 = 1/6 - 1/6 = 0.
//   rp.x *= (0>0 ? ... : 1.0) = 0.
//   rp.y = saturate(lerp(0, 0.25, 0)) = 0.
//   rp.x *= TAU = 0.
//   p_back = (cos(0), sin(0)) * 0 = (0, 0).
//   b = (0.25, 0.25 * tan(π/3)) = (0.25, 0.4330).
//   d = abs((0,0)) - (0.25, 0.4330) = (-0.25, -0.4330); both negative.
//   max(d, 0) = (0, 0); min(d.x, 0) = -0.25.
//   sd = 0 + (-0.25) = -0.25   [well inside triangle].
// d = smoothstep(0 - 0.05/4, 0 + 0.05/4, -0.25)
//   = smoothstep(-0.0125, 0.0125, -0.25) = 0.0   [edge-value below lower bound → 0]
// dBiased = pow(0.0, 1.0) = 0.0.
// c = mix(Fill=(1,1,1,1), Background=(0,0,0,0), 0.0) = (1,1,1,1)   WHITE.
// IsTextureValid=0 → return c = (1,1,1,1) WHITE.
//
// GOLDEN ASSERTION A: center pixel (64,64) is WHITE (R≥240, G≥240, B≥240, A≥240).
//
// ── PIN B: corner pixel at pixel (4,4), UV≈(0.039, 0.039) ────────────────────────────────
// p_init = (-0.461, -0.461); p_rot = (-0.461, 0.461).
// sdNgon((-0.461, 0.461), 0.25, 3, ...): len = sqrt(0.461²+0.461²) ≈ 0.652 >> Radius=0.25.
// The polygon boundary is at most Radius=0.25 from center; distance to boundary ≈ 0.652-0.25 = 0.4.
// sd ≈ +0.4  (positive, far outside).
// d = smoothstep(-0.0125, 0.0125, 0.4) = 1.0.
// dBiased = 1.0.
// c = mix(Fill, Background, 1.0) = Background = (0,0,0,0)   TRANSPARENT.
// GOLDEN ASSERTION B: corner pixel (4,4) is TRANSPARENT (A<10, R<10).
//
// ── injectBug ─────────────────────────────────────────────────────────────────────────────
// Set Fill = (0,0,0,0) (transparent). Then at center: c = mix((0,0,0,0), Background, 0) = (0,0,0,0).
// Center pixel becomes transparent → assertion A (R≥240) FAILS → RED (test catches the bug).
int runNGonSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 128, H = 128;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-ngon] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(td);

  // Default params — triangle on transparent black.
  // injectBug: Fill=transparent → center pixel goes dark → assertion A FAILS.
  std::map<std::string, float> params;
  params["Fill.r"] = injectBug ? 0.0f : 1.0f;
  params["Fill.g"] = injectBug ? 0.0f : 1.0f;
  params["Fill.b"] = injectBug ? 0.0f : 1.0f;
  params["Fill.a"] = injectBug ? 0.0f : 1.0f;
  params["Background.r"] = 0.0f; params["Background.g"] = 0.0f;
  params["Background.b"] = 0.0f; params["Background.a"] = 0.0f;
  params["Sides"]     = 3.0f;
  params["Radius"]    = 0.25f;
  params["Round"]     = 0.0f;
  params["Feather"]   = 0.05f;
  params["FeatherBias"]= 0.0f;
  params["Rotate"]    = -90.0f;
  params["Curvature"] = 0.0f;
  params["Blades"]    = 0.0f;
  params["Position.x"]= 0.0f; params["Position.y"]= 0.0f;
  params["BlendMode"] = 0.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1;
  c.inputTexture = nullptr;  // generator mode: no upstream Image
  c.output = dst;
  c.params = &params;
  cookNGon(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  auto readPx = [&](uint32_t x, uint32_t y, int ch) -> int {
    return (int)out[((size_t)y * W + x) * 4 + ch];
  };

  // PIN A: center pixel (64,64) — inside the triangle → white (RGBA all ≥ 240).
  // Hand-derived: sd=-0.25 → smoothstep=0 → d=0 → c=Fill=white (injectBug: Fill=transparent→FAIL).
  int aR = readPx(64, 64, 0), aG = readPx(64, 64, 1);
  int aB = readPx(64, 64, 2), aA = readPx(64, 64, 3);

  // PIN B: corner pixel (4,4) — well outside the triangle → transparent (R<10, A<10).
  // Hand-derived: sd≈+0.4 → smoothstep=1 → d=1 → c=Background=(0,0,0,0).
  int bR = readPx(4, 4, 0), bA = readPx(4, 4, 3);

  bool pinA = (aR >= 240 && aG >= 240 && aB >= 240 && aA >= 240);  // white interior
  bool pinB = (bR < 10 && bA < 10);                                  // transparent exterior

  bool pass = pinA && pinB;
  printf("[selftest-ngon] "
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
