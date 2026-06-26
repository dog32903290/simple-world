// MosiacTiling image-filter texture op (lane multi-image, image/fx/stylize) — the FX-MODULATED
// quadtree mosaic. SIBLING pattern of Displace (point_ops_displace.cpp): a 2nd texture input (FxImage)
// modulates the per-pixel result. Where Displace's 2nd input warps UVs, MosiacTiling's 2nd input
// DRIVES the recursive cell subdivision (its four corner samples decide whether a cell halves).
//
// TiXL authority:
//   external/tixl Operators/Lib/image/fx/stylize/MosiacTiling.cs — op ports: Image / FxTextures
//       (FIXED numbered Texture2D, NOT MultiInput) + Center(Vec2)/Stretch(Vec2)/Size/
//       SubdivisionThreshold/MaxSubdivisions(int)/Randomize/Padding/Feather/GapColor(Vec4)/MixOriginal.
//   .../MosiacTiling.t3 — STEP-0 verified: standard _multiImageFxSetup render-pipeline compound
//       wrapping the OWN pixel shader MosiacTiling.hlsl. The op's BEHAVIOR is the single .hlsl psMain —
//       ATOMIC. The cbuffer is fed by _multiImageFxSetup's FloatsToBuffer MultiInput; backward-tracing
//       the .t3 connection order into slot bcc7fb78 gives EXACTLY the cbuffer field order (Center.xy,
//       Stretch.xy, Size, SubdivisionThreshold, Padding, Feather, GapColor.rgba, MixOriginal,
//       IntToFloat(MaxSubdivisions), Randomize) — a 1:1 routing, NO intermediate math except a pure
//       IntToFloat cast (no scaling). NOT a DirectionalBlur FloatsToBuffer trap.
//       Sampler (_multiImageFxSetup.t3): ONE SamplerState s0 = Filter MinMagMipLinear, WrapMode
//       MirrorOnce → Metal linear + MirrorClampToEdge, used for BOTH Image (t0) and FxImage (t1).
//   .../MosiacTiling.hlsl — the pixel shader, ported VERBATIM into mosiactiling.metal.
//
// SEAM NOTE — ZERO shared-graph edit. The multi-image seam already gathers up to kMaxTexInputs=4
// Texture2D ports into TexCookCtx::inputTextures[] in spec port order, DENSE (each port occupies the
// next slot). So slot 0=Image, 1=FxImage always (Image is spec port 0, FxImage port 1).
//
// FORKS (named, DX11->Metal):
//   [fork-mosiac-mirror-once-sampler] — MosiacTiling.t3 WrapMode=MirrorOnce → MirrorClampToEdge in
//     Metal. LOAD-BEARING (cell-corner UVs go OOB near frame edges); matched faithfully in cookMosiacTiling.
//   [fork-mosiac-fmod-floor] — HLSL #define fmod(x,y)=x-y*floor(x/y) ported verbatim (swMod in the
//     shader), NOT Metal's truncated fmod — negatives wrap toward +y, which the cell math relies on.
//   [fork-mosiac-stretch-unused] — `Stretch2` rides in the cbuffer (offset 8) for byte-faithful layout
//     and is routed from the Stretch input (matching the .t3), but MosiacTiling.hlsl psMain never reads
//     it. The pixel result is independent of Stretch. Documented as intentionally dead (see params .h).
//   [fork-mosiac-unwired-fximage] — an unwired FxImage slot is null; a null FxImage can't drive
//     subdivision. We bind the Image in its place (keeps the picture, no crash) — same fork class as
//     Displace's unwired-DisplaceMap. With a flat substitute the corner distances are 0 → no
//     subdivision (faithful: TiXL would bind an empty texture → 0 samples → same no-subdivision path).
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/mosiactiling_params.h"        // MosiacTilingParams, MOSIACTILING_Params
#include "runtime/point_graph.h"
#include "runtime/tex_op_cache.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

void clearTexture(MTL::CommandQueue* q, MTL::Texture* out) {
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(out);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = q->commandBuffer();
  cmd->renderCommandEncoder(pass)->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

// injectBug hook (golden tooth, manual proof per task): drop the FxImage gather so the op cannot read
// the 2nd input → corner samples become the Image substitute (flat in the golden) → no subdivision.
bool g_mosiacIgnoreFxImage = false;

// MosiacTiling cook: read Image([0]) + FxImage([1]), build the cbuffer from the 15 params, one
// fullscreen pass into c.output.
void cookMosiacTiling(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  const MTL::Texture* image = c.inputTextureCount > 0 ? c.inputTextures[0] : nullptr;
  const MTL::Texture* fxImage = c.inputTextureCount > 1 ? c.inputTextures[1] : nullptr;
  if (g_mosiacIgnoreFxImage) fxImage = nullptr;  // golden tooth: drop the FxImage wire
  if (!image) { clearTexture(c.queue, c.output); return; }  // no Image -> nothing to tile
  if (!fxImage) fxImage = image;  // [fork-mosiac-unwired-fximage]

  // Build params from cookParam (MosiacTiling.cs/.t3 defaults). Field order = HLSL cbuffer layout.
  MosiacTilingParams p{};
  p.Center_x = cookParam(c, "Center.x", 0.0f);
  p.Center_y = cookParam(c, "Center.y", 0.0f);
  p.Stretch2_x = cookParam(c, "Stretch.x", 1.0f);  // FAITHFUL-UNUSED (routed, never read by psMain)
  p.Stretch2_y = cookParam(c, "Stretch.y", 1.0f);
  p.Size = cookParam(c, "Size", 0.2f);
  p.SubdivisionThreshold = cookParam(c, "SubdivisionThreshold", 0.0f);
  p.Padding = cookParam(c, "Padding", 0.0f);
  p.Feather = cookParam(c, "Feather", 0.0f);
  p.GapColor_r = cookParam(c, "GapColor.r", 0.0f);
  p.GapColor_g = cookParam(c, "GapColor.g", 0.0f);
  p.GapColor_b = cookParam(c, "GapColor.b", 0.0f);
  p.GapColor_a = cookParam(c, "GapColor.a", 1.0f);
  p.MixOriginal = cookParam(c, "MixOriginal", 1.0f);
  p.MaxSubdivisions = cookParam(c, "MaxSubdivisions", 4.0f);  // int input, cast to float (IntToFloat)
  p.Randomize = cookParam(c, "Randomize", 0.0f);
  p._pad = 0.0f;

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "mosiactiling_vs", "mosiactiling_fs", fmt);
  if (!rps) return;

  // texSampler (s0): linear, MirrorClampToEdge (MosiacTiling.t3 WrapMode MirrorOnce).
  // [fork-mosiac-mirror-once-sampler]
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeMirrorClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeMirrorClampToEdge);
  MTL::SamplerState* texSampler = c.dev->newSamplerState(sd);
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
  enc->setFragmentTexture(const_cast<MTL::Texture*>(image), 0);    // t0 = Image
  enc->setFragmentTexture(const_cast<MTL::Texture*>(fxImage), 1);  // t1 = FxImage
  enc->setFragmentSamplerState(texSampler, 0);
  enc->setFragmentBytes(&p, sizeof(MosiacTilingParams), MOSIACTILING_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  texSampler->release();  // rps is cache-owned (tex_op_cache)
}

}  // namespace

int runMosiacTilingSelfTest(bool injectBug);

// Self-registration. Ports 1:1 from MosiacTiling.cs — two FIXED Texture2D inputs (Image / FxImage),
// the out, then the scalar/vec params (Center Vec2, Stretch Vec2, Size, SubdivisionThreshold,
// MaxSubdivisions, Randomize, Padding, Feather, GapColor Vec4, MixOriginal) at their .t3 defaults,
// plus the standard Resolution/Custom engine resolution pins (mirrors Displace/CMC). Type
// name "MosiacTiling" (TiXL's spelling, sic).
static const ImageFilterOp _reg_mosiactiling{
    {"MosiacTiling", "MosiacTiling",
     {{"Image", "Image", "Texture2D", true},
      {"FxImage", "FxImage", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // Center (Vec2, TiXL default (0,0))
      {"Center.x", "Center", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Center.y", "Center.y", "Float", true, 0.0f, -1.0f, 1.0f, Widget::Vec, {}, true, 1},
      // Stretch (Vec2, TiXL default (1,1)) — FAITHFUL-UNUSED by the shader, routed for layout parity
      {"Stretch.x", "Stretch", "Float", true, 1.0f, 0.0f, 4.0f, Widget::Vec, {}, true, 2},
      {"Stretch.y", "Stretch.y", "Float", true, 1.0f, 0.0f, 4.0f, Widget::Vec, {}, true, 1},
      // Size (float, TiXL default 0.2)
      {"Size", "Size", "Float", true, 0.2f, 0.0f, 2.0f},
      // SubdivisionThreshold (float, TiXL default 0.0)
      {"SubdivisionThreshold", "SubdivisionThreshold", "Float", true, 0.0f, 0.0f, 2.0f},
      // MaxSubdivisions (int, TiXL default 4) — clamped 1..7 in the shader
      {"MaxSubdivisions", "MaxSubdivisions", "Float", true, 4.0f, 1.0f, 7.0f},
      // Randomize (float, TiXL default 0.0)
      {"Randomize", "Randomize", "Float", true, 0.0f, 0.0f, 2.0f},
      // Padding (float, TiXL default 0.0)
      {"Padding", "Padding", "Float", true, 0.0f, 0.0f, 1.0f},
      // Feather (float, TiXL default 0.0)
      {"Feather", "Feather", "Float", true, 0.0f, 0.0f, 1.0f},
      // GapColor (Vec4, TiXL default (0,0,0,1) black)
      {"GapColor.r", "GapColor", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"GapColor.g", "GapColor.g", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"GapColor.b", "GapColor.b", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"GapColor.a", "GapColor.a", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      // MixOriginal (float, TiXL default 1.0)
      {"MixOriginal", "MixOriginal", "Float", true, 1.0f, 0.0f, 1.0f},
      // Resolution / Custom (engine resolution plumbing, mirrors Displace/CMC)
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "MosiacTiling", cookMosiacTiling, "mosiactiling", runMosiacTilingSelfTest};

// --- MosiacTiling FX-DRIVEN-SUBDIVISION GOLDEN (closed-form, FxImage load-bearing) -------------------
// Config (hand-derived via the shader sim, verified at the EXACT 32x32 texel-center UV of texel (8,8)
// = uv (0.265625, 0.265625)):
//   Image    = FLAT solid (128,153,179) — value irrelevant at the assert pixel (gap=0, see below).
//   FxImage  = DIAGONAL gradient g=(u+v)/2 in RGB (so the cell's two DIAGONAL corner pairs DIFFER → a
//              non-zero corner distance → the FxImage drives subdivision). LOAD-BEARING.
//   Params:  Center=(0,0) Size=0.5 MaxSubdivisions=2 SubdivisionThreshold=0.3 Randomize=0
//            Padding=0.75 Feather=0.25 MixOriginal=1 GapColor=(0,0,1,1) (blue).
// At texel (8,8): WITH FxImage the corner distance (~0.18*sqrt term) exceeds neither... — the loop
// SUBDIVIDES once (step ends at 1). At the resulting finer cell CENTER, x = d/(step+1) = ~1/2 = 0.5,
// which is BELOW Padding-Feather=0.5 ramp start → gapFactor = 0 → output = pure GapColor = (0,0,255).
//   Expected center pixel = (0, 0, 255, 255)  (blue — the gap/edge color).
// TOOTH (manual proof, NO -bug flag per task): drop the FxImage read (g_mosiacIgnoreFxImage / break the
// fxImage.sample in the shader) → corner samples come from the FLAT Image substitute → distance 0 →
// the loop BREAKS at step 0 → x = d/1 = ~1.0 ≥ ramp → gapFactor ~ 1 → output ≈ Image (~122,146,182).
// That is a >100-LSB divergence on every channel → FAIL. So the 2nd input bites.
constexpr uint32_t kGW = 32, kGH = 32;
constexpr uint8_t kImg_r = 128, kImg_g = 153, kImg_b = 179;  // flat Image (value-independent at assert px)
constexpr uint8_t kExpR = 0, kExpG = 0, kExpB = 255, kExpA = 255;  // pure GapColor (blue) at texel(8,8)
constexpr uint32_t kPX = 8, kPY = 8;  // assert texel (uv center 0.265625,0.265625)

// Fill `t` with a flat RGBA8 solid.
static void fillSolid(MTL::Texture* t, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  for (size_t i = 0; i < (size_t)w * h; ++i) {
    px[i * 4 + 0] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255;
  }
  t->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

// Fill `t` with the diagonal gradient g=(u+v)/2 in RGB (u,v = texel-center coords). The FxImage that
// drives subdivision. Texel center uv = ((x+0.5)/w, (y+0.5)/h) matches the rasterizer's sample grid.
static void fillDiagGradient(MTL::Texture* t, uint32_t w, uint32_t h) {
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      const float u = ((float)x + 0.5f) / (float)w;
      const float v = ((float)y + 0.5f) / (float)h;
      const float g = (u + v) * 0.5f;
      const uint8_t gb = (uint8_t)std::lround(std::clamp(g, 0.0f, 1.0f) * 255.0f);
      size_t i = ((size_t)y * w + x) * 4;
      px[i + 0] = gb; px[i + 1] = gb; px[i + 2] = gb; px[i + 3] = 255;
    }
  }
  t->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

static bool mosiacCookPixel(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool ignoreFx,
                            uint8_t out[4]) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kGW, kGH, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* image = dev->newTexture(td);
  MTL::Texture* fx = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  fillSolid(image, kGW, kGH, kImg_r, kImg_g, kImg_b);
  fillDiagGradient(fx, kGW, kGH);

  std::map<std::string, float> params;
  params["Center.x"] = 0.0f;     params["Center.y"] = 0.0f;
  params["Stretch.x"] = 1.0f;    params["Stretch.y"] = 1.0f;
  params["Size"] = 0.5f;
  params["SubdivisionThreshold"] = 0.3f;
  params["MaxSubdivisions"] = 2.0f;
  params["Randomize"] = 0.0f;
  params["Padding"] = 0.75f;
  params["Feather"] = 0.25f;
  params["GapColor.r"] = 0.0f;   params["GapColor.g"] = 0.0f;
  params["GapColor.b"] = 1.0f;   params["GapColor.a"] = 1.0f;
  params["MixOriginal"] = 1.0f;

  g_mosiacIgnoreFxImage = ignoreFx;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.output = dst; c.params = &params;
  c.inputTextures[0] = image; c.inputTextures[1] = fx;
  c.inputTextureCount = 2;
  c.inputTexture = image;
  cookMosiacTiling(c);
  g_mosiacIgnoreFxImage = false;

  std::vector<uint8_t> px((size_t)kGW * kGH * 4, 0);
  dst->getBytes(px.data(), kGW * 4, MTL::Region::Make2D(0, 0, kGW, kGH), 0);
  size_t i = ((size_t)kPY * kGW + kPX) * 4;
  out[0] = px[i]; out[1] = px[i + 1]; out[2] = px[i + 2]; out[3] = px[i + 3];

  image->release(); fx->release(); dst->release();
  return true;
}

int runMosiacTilingSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-mosiactiling] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const int kTol = 2;
  uint8_t got[4] = {0, 0, 0, 0};
  mosiacCookPixel(dev, q, lib, /*ignoreFx=*/injectBug, got);

  int dR = std::abs((int)got[0] - (int)kExpR);
  int dG = std::abs((int)got[1] - (int)kExpG);
  int dB = std::abs((int)got[2] - (int)kExpB);
  int dA = std::abs((int)got[3] - (int)kExpA);
  bool match = dR <= kTol && dG <= kTol && dB <= kTol && dA <= kTol;

  bool pass = match;
  printf("[selftest-mosiactiling] want=(%u,%u,%u,%u) got=(%u,%u,%u,%u) d=(%d,%d,%d,%d) "
         "match(<=%d)=%d injectBug=%d -> %s\n",
         kExpR, kExpG, kExpB, kExpA, got[0], got[1], got[2], got[3], dR, dG, dB, dA, kTol,
         match ? 1 : 0, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release();
  clearTexOpCache();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
