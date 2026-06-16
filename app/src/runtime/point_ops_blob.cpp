// Blob image generator op (Phase C leaf, C-5).
// TiXL authority: Operators/Lib/image/generate/basic/Blob.cs (inputs/GUIDs/defaults) +
// Blob.t3 (defaults: Scale=0.5, Stretch=(1,1), Rotate=0.0, Feather=1.0, FeatherBias=0.0,
// Position=(0,0), Color=(1,1,1,1), Background=(1,1,1,0), BlendMode=0, Image=null,
// GenerateMips=false, TextureFormat=R16G16B16A16_Float) +
// Assets/shaders/img/generate/Blob.hlsl (single-pass radial SDF with smoothstep feather,
// pow GradientBias, optional BlendColors blend over Image input).
//
// PORTABILITY HARDGATE (STEP-0 backward-trace, Cut-55/58 discipline):
//   - Blob.t3 slot 4ef6f204 connections (document order):
//       31ff2db0=Vector4Components(Color→Fill) x4
//       29bc4f56=Vector4Components(Background) x4
//       c5c91c08=Vector2Components(Stretch) x2
//       d74b78a8=Vector2Components(Position) x2
//       33f31c62=Scale (direct) x1
//       f0c128b1=Feather (direct) x1
//       0c49c872=FeatherBias→GradientBias (direct) x1
//       77544b82=Rotate (direct) x1
//       842128f1=IntToFloat(BlendMode) x1
//       (IsTextureValid injected by _ImageFxShaderSetupStatic infra, not in .t3)
//     ZERO intermediate math nodes → no routing trap. Direct 1:1 mapping. cbuffer order exact.
//   - Image input is OPTIONAL (TiXL default null). When null, bind 1x1 transparent-black dummy
//     (per Cut 61 generator convention, same as SinForm). IsTextureValid=0.0 → shader returns c.
//   - No gradient / asset-texture / mip / multi-image seam dependency. Pure R1 leaf.
//   - _ImageFxShaderSetupStatic (SymbolId bd0b9c5b).
//
// HLSL→MSL port forks (named):
//   fork[rotation-verbatim]: TiXL double-negation rotation kept verbatim (see blob.metal).
//   fork[blend-functions-inline]: shared/blend-functions.hlsl inlined in blob.metal.
//   fork[clamp-10]: pow bias negative path: clamp(1-d, 0, 10) kept verbatim.
//   fork[sampler-linear-wrap]: Standard image-filter linear+Repeat sampler (for orgColor readback).
//     TiXL _ImageFxShaderSetupStatic default sampler is linear; wrap mode matches.
//
// HEADLESS GOLDEN (against TiXL, not self):
//   On a 256×256 square (aspectRatio=1) with no Image input:
//
//   Default params: Scale=0.5, Stretch=(1,1), Position=(0,0), Feather=1.0, GradientBias=0,
//   Rotate=0, Fill=(1,1,1,1), Background=(1,1,1,0), BlendMode=0, IsTextureValid=0.
//
//   PIN_A: center pixel (W/2, H/2) → uv=(0,0), d=0, f=0.25,
//     smoothstep(0.0, 0.5, 0) = 0, dBiased=pow(0,1)=0,
//     c = mix(Fill, Background, 0) = Fill = (1,1,1,1) → WHITE opaque (alpha=255 in RGBA8).
//
//   PIN_B: far corner pixel (W*3/4, H*3/4) → uv after centering = (W*3/4/W-0.5, ...) = (0.25,0.25),
//     d = length(0.25, 0.25) = 0.3536, f=0.25,
//     smoothstep(0.25-0.25=0, 0.25+0.25=0.5, 0.3536) = ((0.3536/0.5)^2)*(3-2*(0.3536/0.5))
//     = (0.7072)^2*(3-2*0.7072) = 0.5001 * 1.5856 ≈ 0.793 → dBiased≈0.793,
//     c = mix((1,1,1,1),(1,1,1,0), 0.793) → alpha = mix(1, 0, 0.793) = 0.207 → alpha≈53 in RGBA8.
//     So PIN_B is partially transparent: alpha < 128.
//
//   PIN_C: corner pixel (1, 1) (top-left) → uv=(-0.498,-0.498), d≈0.704 >> Scale/2=0.25+0.25=0.5,
//     smoothstep(0, 0.5, 0.704) ≈ 1 (well past upper edge) → dBiased≈1,
//     c ≈ Background = (1,1,1,0) → nearly transparent (alpha < 20).
//
//   injectBug: set Fill.a=0 → Fill=(1,1,1,0) → center c=Fill=(1,1,1,0) → alpha~0 in RGBA8.
//     Assertion PIN_A "alpha>200" FAILS → RED.
//
// Self-contained leaf: cookBlob + ImageFilterOp self-registration + runBlobSelfTest.
// CMake glob auto-picks (CMakeLists.txt CONFIGURE_DEPENDS point_ops_*.cpp). No shared header edited.
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/blob_params.h"             // BlobParams, BlobResolution, BLOB_* bindings
#include "runtime/eval_context.h"
#include "runtime/image_filter_op_registry.h" // ImageFilterOp self-registration
#include "runtime/point_graph.h"              // TexCookCtx, cookParam
#include "runtime/tex_op_cache.h"             // cachedTexPSO (PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {

// Forward declaration so the file-scope ImageFilterOp registrar can reference runBlobSelfTest.
int runBlobSelfTest(bool injectBug);

namespace {

// Helper: create a 1×1 transparent-black dummy texture for the no-input case.
// Blob is a generator (TiXL Image=null default). We must still bind a valid texture2d handle
// so the shader sees a valid t0 (even though IsTextureValid<0.5 → orgColor is discarded).
// Same pattern as SinForm (Cut 63 generator convention).
static MTL::Texture* makeDummyBlackTexture(MTL::Device* dev) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, 1, 1, false);
  td->setUsage(MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* t = dev->newTexture(td);
  const uint8_t px[4] = {0, 0, 0, 0};
  t->replaceRegion(MTL::Region::Make2D(0, 0, 1, 1), 0, px, 4);
  return t;
}

// Blob texture op: single render pass. Optional Image input (may be null → dummy).
// Output = blob colour when no Image; blend over Image when wired.
void cookBlob(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "blob_vs", "blob_fs", fmt);
  if (!rps) return;

  // Sampler: linear + Repeat (standard image-filter sampler for reading orgColor).
  // fork[sampler-linear-wrap]: TiXL uses the default texSampler from _ImageFxShaderSetupStatic;
  // we use linear+Repeat (same as tint.metal, sinform.metal etc. — the common default).
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeRepeat);
  sd->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // Build BlobParams from cookParam (TiXL Blob.t3 defaults).
  // cbuffer b0 field order matches Blob.hlsl cbuffer ParamConstants exactly (STEP-0 traced).
  BlobParams p{};
  // Fill (Vec4, Color in .cs, TiXL default (1,1,1,1))
  p.FillR = cookParam(c, "Fill.r", 1.0f);
  p.FillG = cookParam(c, "Fill.g", 1.0f);
  p.FillB = cookParam(c, "Fill.b", 1.0f);
  p.FillA = cookParam(c, "Fill.a", 1.0f);
  // Background (Vec4, TiXL default (1,1,1,0) — white transparent)
  p.BgR = cookParam(c, "Background.r", 1.0f);
  p.BgG = cookParam(c, "Background.g", 1.0f);
  p.BgB = cookParam(c, "Background.b", 1.0f);
  p.BgA = cookParam(c, "Background.a", 0.0f);
  // Stretch (Vec2, TiXL default (1,1))
  p.StretchX = cookParam(c, "Stretch.x", 1.0f);
  p.StretchY = cookParam(c, "Stretch.y", 1.0f);
  // Position (Vec2, TiXL default (0,0))
  p.PositionX = cookParam(c, "Position.x", 0.0f);
  p.PositionY = cookParam(c, "Position.y", 0.0f);
  // Scale (float, TiXL default 0.5)
  p.Scale = cookParam(c, "Scale", 0.5f);
  // Feather (float, TiXL default 1.0)
  p.Feather = cookParam(c, "Feather", 1.0f);
  // GradientBias / FeatherBias (float, TiXL default 0.0)
  p.GradientBias = cookParam(c, "GradientBias", 0.0f);
  // Rotate (float, TiXL default 0.0)
  p.Rotate = cookParam(c, "Rotate", 0.0f);
  // BlendMode (int via IntToFloat, TiXL default 0=normal)
  p.BlendMode = cookParam(c, "BlendMode", 0.0f);

  // IsTextureValid: 1.0 if Image is wired, 0.0 if null (generator mode).
  // fork[IsTextureValid-host]: TiXL _ImageFxShaderSetupStatic injects this; we replicate here.
  p.IsTextureValid = (c.inputTexture != nullptr) ? 1.0f : 0.0f;

  // Resolution cbuffer (b1).
  BlobResolution res{};
  res.TargetWidth  = (float)c.output->width();
  res.TargetHeight = (float)c.output->height();

  // Bind input texture (or 1×1 black dummy when no Image is wired).
  // fork[dummy-1x1]: TiXL Image=null default → IsTextureValid=0 → orgColor discarded anyway;
  // dummy gives blob_fs a valid texture2d binding (Metal requires bound textures for all slots).
  MTL::Texture* dummyTex = nullptr;
  const MTL::Texture* inputTex = c.inputTexture;
  if (!inputTex) {
    dummyTex = makeDummyBlackTexture(c.dev);
    inputTex = dummyTex;
  }

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 0.0));  // transparent clear
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(inputTex), BLOB_Texture);
  enc->setFragmentSamplerState(samp, BLOB_Sampler);
  enc->setFragmentBytes(&p,   sizeof(BlobParams),     BLOB_Params);
  enc->setFragmentBytes(&res, sizeof(BlobResolution), BLOB_Resolution);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
  if (dummyTex) dummyTex->release();
}

}  // namespace

// Self-registration. File-scope static ImageFilterOp feeds imageFilterSpecSink() + texReg()
// + imageFilterSelfTests() during pre-main dynamic init. No shared file edited.
static const ImageFilterOp _reg_blob{
    // Blob (TiXL Lib.image.generate.basic.Blob): radial SDF blob generator with optional
    // Image input. Generates a smooth elliptical blob via smoothstep feather + pow GradientBias.
    // Optional Image in → BlendColors blend over it. Texture2D out.
    // Params mirror Blob.cs/.t3: Color/Fill(Vec4), Background(Vec4), Scale, Stretch(Vec2),
    // Rotate, Feather, FeatherBias/GradientBias, Position(Vec2), BlendMode.
    // Resolution/GenerateMips: standard pins (GenerateMips=false per TiXL default).
    // FORKS (named): rotation-verbatim, blend-functions-inline, clamp-10,
    //   sampler-linear-wrap, dummy-1x1 (see cook), IsTextureValid-host.
    {"Blob", "Blob",
     {// Optional Image input (TiXL default null; wired → blob blended over it).
      {"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Fill / Color (Vec4, TiXL Color default (1,1,1,1) white)
      {"Fill.r", "Fill",   "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Fill.g", "Fill.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Fill.b", "Fill.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Fill.a", "Fill.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Background (Vec4, TiXL default (1,1,1,0) white transparent)
      {"Background.r", "Background",   "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"Background.g", "Background.g", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.b", "Background.b", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Background.a", "Background.a", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Scale (float, TiXL default 0.5)
      {"Scale", "Scale", "Float", true, 0.5f, 0.0f, 2.0f, Widget::Slider},
      // Stretch (Vec2, TiXL default (1,1))
      {"Stretch.x", "Stretch",   "Float", true, 1.0f, 0.1f, 4.0f, Widget::Vec, {}, true, 2},
      {"Stretch.y", "Stretch.y", "Float", true, 1.0f, 0.1f, 4.0f, Widget::Vec, {}, true, 1},
      // Rotate (float, TiXL default 0.0, degrees)
      {"Rotate", "Rotate", "Float", true, 0.0f, -180.0f, 180.0f, Widget::Slider},
      // Feather (float, TiXL default 1.0)
      {"Feather", "Feather", "Float", true, 1.0f, 0.0f, 2.0f, Widget::Slider},
      // GradientBias / FeatherBias (float, TiXL default 0.0)
      {"GradientBias", "GradientBias", "Float", true, 0.0f, -3.0f, 3.0f, Widget::Slider},
      // Position (Vec2, TiXL default (0,0))
      {"Position.x", "Position",   "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Position.y", "Position.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // BlendMode (enum int, TiXL default 0=normal; maps via IntToFloat)
      {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 9.0f, Widget::Enum,
       {"Normal", "Screen", "Multiply", "Overlay", "Difference", "UseA", "UseB",
        "ColorDodge", "LinearDodge", "MultiplyAlpha"}},
      // Resolution (standard image-filter enum)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "Blob", cookBlob, "blob", runBlobSelfTest};

// --- Blob MATH golden -------------------------------------------------------------------------
// Reference: Blob.hlsl psMain. All derivations are independent of the port.
//
// Test configuration: 256×256 square (aspect=1), no Image input (IsTextureValid=0.0).
// Defaults: Scale=0.5, Stretch=(1,1), Position=(0,0), Feather=1.0, GradientBias=0,
//   Rotate=0, Fill=(1,1,1,1), Background=(1,1,1,0), BlendMode=0.
//
// PIN_A: center pixel (W/2, H/2) → UV≈(0.5,0.5).
//   uv = (0.5-0.5, 0.5-0.5) = (0, 0).
//   uv.x *= aspect(1) = (0, 0).
//   Rotate=0 → imageRotationRad = (-0-90)/180*pi = -pi/2.
//   sina = sin(-(-pi/2)-pi/2) = sin(0) = 0.
//   cosa = cos(0) = 1.
//   uv unchanged after rotation (no-op). /= Stretch (1,1) → (0,0). -= Position*(1,-1) → (0,0).
//   d = length(0,0) = 0.
//   f = Feather(1.0) * Scale(0.5) / 2 = 0.25.
//   smoothstep(Scale/2-f=0.0, Scale/2+f=0.5, 0) = 0.
//   dBiased = pow(0, 0+1) = 0.
//   c = mix(Fill=(1,1,1,1), Bg=(1,1,1,0), 0) = (1,1,1,1) WHITE OPAQUE.
//   IsTextureValid=0 → return c = (1,1,1,1).
//   Expected PIN_A in RGBA8Unorm: R≥240, G≥240, B≥240, alpha≥240.
//
// PIN_B: corner pixel (1, 1) (top-left, UV≈(0, 0)).
//   uv = (0-0.5, 0-0.5) = (-0.5, -0.5). uv.x *= 1 = (-0.5, -0.5).
//   Rotation no-op (Rotate=0). /= (1,1) → (-0.5,-0.5). -= (0,0) → (-0.5,-0.5).
//   d = length(-0.5,-0.5) = 0.7071.
//   smoothstep(0.0, 0.5, 0.7071): input(0.7071) > edge1(0.5) → result = 1.
//   dBiased = pow(1.0, 1) = 1.
//   c = mix(Fill=(1,1,1,1), Bg=(1,1,1,0), 1) = (1,1,1,0) TRANSPARENT.
//   Expected PIN_B in RGBA8Unorm: alpha < 10.
//
// PIN_C (GradientBias test): same as PIN_A but GradientBias=2.0.
//   d=0 → smoothstep=0 → dBiased=pow(0,3)=0 → c=Fill=(1,1,1,1) still WHITE OPAQUE.
//   (At center, bias has no effect — the blob interior is still Fill.)
//   Expected PIN_C: alpha≥240 (confirms GradientBias path doesn't break center pixel).
//
// injectBug: set Fill.a=0 → Fill=(1,1,1,0).
//   PIN_A: center c=mix((1,1,1,0),(1,1,1,0),0)=(1,1,1,0) → alpha=0 in RGBA8.
//   Assertion PIN_A "alpha≥240" FAILS → confirms the bug is detected (RED).
int runBlobSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-blob] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // RGBA8Unorm output texture (256×256, shared memory for readback).
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* dst = dev->newTexture(td);

  // ---- CASE A+B: default blob, no Image input ------------------------------------------------
  // Fill=(1,1,1,1) white opaque; Background=(1,1,1,0) white transparent.
  // injectBug: Fill.a=0 → center becomes transparent → PIN_A fails.
  std::map<std::string, float> params;
  params["Fill.r"] = 1.0f; params["Fill.g"] = 1.0f;
  params["Fill.b"] = 1.0f;
  params["Fill.a"] = injectBug ? 0.0f : 1.0f;  // injectBug: alpha=0 → PIN_A alpha<10 (RED)
  params["Background.r"] = 1.0f; params["Background.g"] = 1.0f;
  params["Background.b"] = 1.0f; params["Background.a"] = 0.0f;
  params["Scale"]        = 0.5f;
  params["Stretch.x"]    = 1.0f; params["Stretch.y"] = 1.0f;
  params["Rotate"]       = 0.0f;
  params["Feather"]      = 1.0f;
  params["GradientBias"] = 0.0f;
  params["Position.x"]   = 0.0f; params["Position.y"] = 0.0f;
  params["BlendMode"]    = 0.0f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1;
  c.inputTexture = nullptr;  // GENERATOR MODE: no Image wired
  c.output = dst;
  c.params = &params;
  cookBlob(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // PIN_A: center pixel → Fill = (1,1,1,1) → alpha≥240.
  const uint32_t px_a = W / 2, py_a = H / 2;
  size_t i_a = ((size_t)py_a * W + px_a) * 4;
  int aR = out[i_a], aG = out[i_a+1], aB = out[i_a+2], aAlpha = out[i_a+3];
  bool pinA = (aR >= 240 && aG >= 240 && aB >= 240 && aAlpha >= 240);
  // Under injectBug (Fill.a=0): center alpha→0 → pinA fails.

  // PIN_B: top-left pixel (1,1) → Background = (1,1,1,0) → alpha < 10.
  const uint32_t px_b = 1, py_b = 1;
  size_t i_b = ((size_t)py_b * W + px_b) * 4;
  int bAlpha = out[i_b+3];
  bool pinB = (bAlpha < 10);

  printf("[selftest-blob] AB center(%d,%d) RGBA=(%d,%d,%d,%d) pinA=%d "
         "| corner(%d,%d) alpha=%d pinB=%d\n",
         px_a, py_a, aR, aG, aB, aAlpha, pinA ? 1 : 0,
         px_b, py_b, bAlpha, pinB ? 1 : 0);

  // ---- CASE C: GradientBias path — positive bias at center pixel stays Fill -----------------
  // GradientBias=2.0: at d=0, pow(0, 3)=0 → c=Fill=(1,1,1,1) → center still white opaque.
  // Confirms the GradientBias path doesn't corrupt the centre-pixel calculation.
  // (We do NOT injectBug here — just run the extra golden case to cover the pow branch.)
  std::map<std::string, float> paramsC;
  paramsC["Fill.r"] = 1.0f; paramsC["Fill.g"] = 1.0f;
  paramsC["Fill.b"] = 1.0f; paramsC["Fill.a"] = 1.0f;
  paramsC["Background.r"] = 0.0f; paramsC["Background.g"] = 0.0f;
  paramsC["Background.b"] = 0.0f; paramsC["Background.a"] = 0.0f;
  paramsC["Scale"]        = 0.5f;
  paramsC["Stretch.x"]    = 1.0f; paramsC["Stretch.y"] = 1.0f;
  paramsC["Rotate"]       = 0.0f;
  paramsC["Feather"]      = 1.0f;
  paramsC["GradientBias"] = 2.0f;  // pow(d, 3) — at center d=0 → still 0
  paramsC["Position.x"]   = 0.0f; paramsC["Position.y"] = 0.0f;
  paramsC["BlendMode"]    = 0.0f;

  MTL::Texture* dstC = dev->newTexture(td);
  TexCookCtx c2;
  c2.dev = dev; c2.lib = lib; c2.queue = q;
  c2.nodeId = 2;
  c2.inputTexture = nullptr;
  c2.output = dstC;
  c2.params = &paramsC;
  cookBlob(c2);

  std::vector<uint8_t> outC((size_t)W * H * 4, 0);
  dstC->getBytes(outC.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  size_t i_c = ((size_t)(H/2) * W + (W/2)) * 4;
  int cAlpha = outC[i_c+3];
  bool pinC = (cAlpha >= 240);  // GradientBias=2 at center: still white opaque

  printf("[selftest-blob] C GradientBias=2 center alpha=%d pinC=%d (expect white opaque)\n",
         cAlpha, pinC ? 1 : 0);

  bool pass = pinA && pinB && pinC;
  printf("[selftest-blob] -> %s\n", pass ? "PASS" : "FAIL");

  dstC->release();
  dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
