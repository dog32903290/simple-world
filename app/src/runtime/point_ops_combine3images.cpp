// Combine3Images image-filter texture op (lane multi-image, image/use) — the THIRD consumer of the
// multi-image seam and the FIRST op with THREE Texture2D inputs. TiXL authority:
//   external/tixl Operators/Lib/image/use/Combine3Images.cs  — op ports (ImageA/B/C, ColorA/B/C,
//       SelectChannel_R/G/B, SelectAlphaChannel, GenerateMips) + the two enums (15-way channel select,
//       5-way alpha).
//   .../Combine3Images.t3  — _trippleImageFxSetup compound. STEP-0 BACKWARD-TRACE of the three shader
//       SRV slots (the Cut-58 lesson: trace each binding back to its true source):
//         t0 (ImageA in the .hlsl) <- root op port ImageA (81e08eab) -> _trippleImageFxSetup.ImageA slot.
//         t1 (ImageB in the .hlsl) <- root op port ImageB (d42bf975) -> _trippleImageFxSetup.ImageB slot.
//         t2 (ImageC in the .hlsl) <- root op port ImageC (8950b878) -> _trippleImageFxSetup.ImageC slot.
//       (Each image port wires DIRECTLY into the matching _trippleImageFxSetup.Image{A,B,C} input — no
//       SrvFromTexture2d indirection here, unlike DistortAndShade. _trippleImageFxSetup binds
//       ImageA->t0, ImageB->t1, ImageC->t2, matching img-combine-3.hlsl register(t0/t1/t2).) All three
//       are graph-wired op ports — NOT assets, NOT dangling.
//   .../img-combine-3.hlsl — the pixel shader (ported 1:1 -> combine3images.metal).
//
// PARAM ROUTING (STEP-0, the Cut-55 .t3 FloatsToBuffer connection-order rule — verified CLEAN 1:1, no
// math junctions): the _trippleImageFxSetup FloatParams MultiInput is fed by 16 connections whose order
// EXACTLY matches the .hlsl cbuffer field order: Vector4Components(ColorA).{X,Y,Z,W},
// Vector4Components(ColorB).{X,Y,Z,W}, Vector4Components(ColorC).{X,Y,Z,W}, IntToFloat(SelectChannel_R),
// IntToFloat(SelectChannel_G), IntToFloat(SelectChannel_B), IntToFloat(SelectAlphaChannel). The
// Vector4Components are identity splitters (X=cfb58526 Y=2f8e90dd Z=162bb4fe W=e1dede5f); each IntToFloat
// is an identity int->float cast. So ImageAColor<-ColorA, ImageBColor<-ColorB, ImageCColor<-ColorC,
// Select_R<-SelectChannel_R, Select_G<-SelectChannel_G, Select_B<-SelectChannel_B, AlphaMode<-
// SelectAlphaChannel. (See combine3images_params.h for the field<-source table + the static_assert.)
//
// SEAM NOTE — the multi-image seam is ALREADY OPEN for N inputs. cookTexNode (flat point_graph.cpp +
// resident point_graph_resident.cpp) recurses EVERY Texture2D input port in spec order into
// TexCookCtx::inputTextures[] / inputTextureCount, capped at kMaxTexInputs=4. A leaf with THREE
// Texture2D ports therefore gets all three upstream textures cooked-and-bound with ZERO shared-file edit.
// This leaf is the first 3-input proof of that seam (Displace/DistortAndShade = 2 inputs).
//
// FORK (named): an unwired ImageB or ImageC -> the op falls back to sampling ImageA (an unwired
// Texture2D slot is null; a null texture can't sample, so reusing ImageA keeps the picture visible
// rather than crashing — same fork class as Displace's unwired-DisplaceMap / DistortAndShade's
// unwired-ImageB). At the .t3 default selects (R<-ImageA_R, G<-ImageB_G, B<-ImageC_B) an unwired B/C
// makes G/B read from ImageA instead — a visible but non-crashing degradation.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/combine3images_params.h"  // Combine3ImagesParams, COMBINE3IMAGES_Params
#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"
#include "runtime/tex_op_cache.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// Clear `out` to black (no ImageA -> nothing to render; mirrors cookDistortAndShade's empty path).
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

// injectBug hook (golden only): when set, the ImageC gather is IGNORED (treated as if ImageC were
// unwired -> falls back to ImageA). A real wiring perturbation: the THIRD-image input is gone, so the
// B-output channel (default B<-ImageC_B) reads ImageA.b instead -> the pinned blue value misses.
bool g_c3IgnoreImageC = false;

// Combine3Images texture op: read ImageA (inputTextures[0]) + ImageB ([1]) + ImageC ([2]), one
// fullscreen pass into c.output. ImageA @ texture(0), ImageB @ texture(1), ImageC @ texture(2).
void cookCombine3Images(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  const MTL::Texture* imageA = c.inputTextureCount > 0 ? c.inputTextures[0] : nullptr;
  const MTL::Texture* imageB = c.inputTextureCount > 1 ? c.inputTextures[1] : nullptr;
  const MTL::Texture* imageC = c.inputTextureCount > 2 ? c.inputTextures[2] : nullptr;
  if (g_c3IgnoreImageC) imageC = nullptr;  // golden injectBug: drop the third image
  if (!imageA) { clearTexture(c.queue, c.output); return; }  // no ImageA -> nothing to render
  if (!imageB) imageB = imageA;  // fork: unwired ImageB -> sample ImageA (keeps picture, no crash)
  if (!imageC) imageC = imageA;  // fork: unwired ImageC -> sample ImageA

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "combine3images_vs", "combine3images_fs", fmt);
  if (!rps) return;

  // Sampler: linear + Repeat (TiXL _trippleImageFxSetup.WrapMode default "Wrap"; D3D11 WRAP = Metal
  // Repeat). NOT load-bearing — all three images sample at the same in-[0,1] texCoord (no warp/OOB) —
  // bound faithfully anyway.
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeRepeat);
  sd->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL params (Combine3Images.cs / .t3 defaults). ColorA (1,0,0,1), ColorB (0,1,0,1), ColorC
  // (0,0,1,1); SelectChannel_R 0, SelectChannel_G 6, SelectChannel_B 12, SelectAlphaChannel 4.
  // Routing verified 1:1 (see header).
  Combine3ImagesParams p{};
  p.ImageAColorR = cookParam(c, "ColorA.x", 1.0f);
  p.ImageAColorG = cookParam(c, "ColorA.y", 0.0f);
  p.ImageAColorB = cookParam(c, "ColorA.z", 0.0f);
  p.ImageAColorA = cookParam(c, "ColorA.w", 1.0f);
  p.ImageBColorR = cookParam(c, "ColorB.x", 0.0f);
  p.ImageBColorG = cookParam(c, "ColorB.y", 1.0f);
  p.ImageBColorB = cookParam(c, "ColorB.z", 0.0f);
  p.ImageBColorA = cookParam(c, "ColorB.w", 1.0f);
  p.ImageCColorR = cookParam(c, "ColorC.x", 0.0f);
  p.ImageCColorG = cookParam(c, "ColorC.y", 0.0f);
  p.ImageCColorB = cookParam(c, "ColorC.z", 1.0f);
  p.ImageCColorA = cookParam(c, "ColorC.w", 1.0f);
  p.Select_R = std::round(cookParam(c, "SelectChannel_R", 0.0f));
  p.Select_G = std::round(cookParam(c, "SelectChannel_G", 6.0f));
  p.Select_B = std::round(cookParam(c, "SelectChannel_B", 12.0f));
  p.AlphaMode = std::round(cookParam(c, "SelectAlphaChannel", 4.0f));

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(imageA), 0);  // texture(0) = ImageA (t0)
  enc->setFragmentTexture(const_cast<MTL::Texture*>(imageB), 1);  // texture(1) = ImageB (t1)
  enc->setFragmentTexture(const_cast<MTL::Texture*>(imageC), 2);  // texture(2) = ImageC (t2)
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(Combine3ImagesParams), COMBINE3IMAGES_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

int runCombine3ImagesSelfTest(bool injectBug);

// Self-registration. Ports 1:1 from Combine3Images.cs: ImageA/B/C (three Texture2D inputs) + ColorA/B/C
// (Vec4) + SelectChannel_R/G/B (15-way enum) + SelectAlphaChannel (5-way enum). Defaults verbatim from
// Combine3Images.t3. FORKS (named): TiXL's WrapMode "Wrap" -> Repeat sampler (in cook, not load-bearing);
// GenerateMips host plumbing omitted (same fork class as every image filter — no mips); an unwired
// ImageB/C samples ImageA. The two enum widgets carry TiXL's exact label sets.
static const ImageFilterOp _reg_combine3images{
    {"Combine3Images", "Combine3Images",
     {{"ImageA", "ImageA", "Texture2D", true},
      {"ImageB", "ImageB", "Texture2D", true},
      {"ImageC", "ImageC", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      {"ColorA.x", "ColorA", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ColorA.y", "ColorA.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"ColorA.z", "ColorA.z", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"ColorA.w", "ColorA.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorB.x", "ColorB", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ColorB.y", "ColorB.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"ColorB.z", "ColorB.z", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"ColorB.w", "ColorB.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorC.x", "ColorC", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ColorC.y", "ColorC.y", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"ColorC.z", "ColorC.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"ColorC.w", "ColorC.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"SelectChannel_R", "SelectChannel_R", "Float", true, 0.0f, 0.0f, 14.0f, Widget::Enum,
       {"ImageA_R", "ImageA_G", "ImageA_B", "ImageA_Average", "ImageA_Brightness", "ImageB_R",
        "ImageB_G", "ImageB_B", "ImageB_Average", "ImageB_Brightness", "ImageC_R", "ImageC_G",
        "ImageC_B", "ImageC_Average", "ImageC_Brightness"}, true},
      {"SelectChannel_G", "SelectChannel_G", "Float", true, 6.0f, 0.0f, 14.0f, Widget::Enum,
       {"ImageA_R", "ImageA_G", "ImageA_B", "ImageA_Average", "ImageA_Brightness", "ImageB_R",
        "ImageB_G", "ImageB_B", "ImageB_Average", "ImageB_Brightness", "ImageC_R", "ImageC_G",
        "ImageC_B", "ImageC_Average", "ImageC_Brightness"}, true},
      {"SelectChannel_B", "SelectChannel_B", "Float", true, 12.0f, 0.0f, 14.0f, Widget::Enum,
       {"ImageA_R", "ImageA_G", "ImageA_B", "ImageA_Average", "ImageA_Brightness", "ImageB_R",
        "ImageB_G", "ImageB_B", "ImageB_Average", "ImageB_Brightness", "ImageC_R", "ImageC_G",
        "ImageC_B", "ImageC_Average", "ImageC_Brightness"}, true},
      {"SelectAlphaChannel", "SelectAlphaChannel", "Float", true, 4.0f, 0.0f, 4.0f, Widget::Enum,
       {"UseImageA_Alpha", "UseImageB_Alpha", "UseImageC_Alpha", "SetToZero", "SetToOne"}, true},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "Combine3Images", cookCombine3Images, "combine3images", runCombine3ImagesSelfTest};

// --- Combine3Images FLAT CHANNEL-PACK GOLDEN (closed-form, d=0 saturated solid) -------------------
// Three FLAT solid images with DISTINCT, known channel values; identity tint colors (1,1,1,1) so the
// `* ImageColor` multiply is a no-op. Default selects (R<-ImageA_R, G<-ImageB_G, B<-ImageC_B) +
// AlphaMode=SetToOne(4). So the output is a single solid color, hand-computable EXACTLY:
//   out.R = ImageA.r,  out.G = ImageB.g,  out.B = ImageC.b,  out.A = 1.0
// We pick A.r=200, B.g=180, C.b=150 (all other channels small/distinct) -> out = (200,180,150,255).
// This is a d=0 plateau (every pixel identical, non-degenerate: R!=G!=B!=0) — no filtering ambiguity.
//
// injectBug (g_c3IgnoreImageC) drops ImageC -> the fork samples ImageA, so out.B reads ImageA.b (=10)
// instead of ImageC.b (=150): a 140-LSB miss on the B channel (R/G unchanged) -> RED. This directly
// exercises the THIRD-image gather (the third Texture2D port must really be threaded into t2).
constexpr uint32_t kGW = 32, kGH = 32;
// Distinct solid channel values per image (chosen so each selected channel is unique + non-degenerate).
constexpr uint8_t kA_r = 200, kA_g = 10,  kA_b = 10;
constexpr uint8_t kB_r = 10,  kB_g = 180, kB_b = 10;
constexpr uint8_t kC_r = 10,  kC_g = 10,  kC_b = 150;
// Expected packed output: R<-A.r, G<-B.g, B<-C.b, A<-1.
constexpr uint8_t kExpR = kA_r, kExpG = kB_g, kExpB = kC_b, kExpA = 255;

static void fillSolid(MTL::Texture* t, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  for (size_t i = 0; i < (size_t)w * h; ++i) {
    px[i * 4 + 0] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255;
  }
  t->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

// Cook Combine3Images on the three controlled solids; read back the center pixel (the whole frame is
// uniform). ignoreC -> injectBug path (ImageC dropped -> B reads ImageA.b).
static bool c3CookCenter(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool ignoreC,
                         uint8_t out[4]) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kGW, kGH, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* imageA = dev->newTexture(td);
  MTL::Texture* imageB = dev->newTexture(td);
  MTL::Texture* imageC = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  fillSolid(imageA, kGW, kGH, kA_r, kA_g, kA_b);
  fillSolid(imageB, kGW, kGH, kB_r, kB_g, kB_b);
  fillSolid(imageC, kGW, kGH, kC_r, kC_g, kC_b);

  std::map<std::string, float> params;
  // Identity tint colors so the `* ImageColor` multiply is a no-op (override the red/green/blue defaults).
  params["ColorA.x"] = 1.0f; params["ColorA.y"] = 1.0f; params["ColorA.z"] = 1.0f; params["ColorA.w"] = 1.0f;
  params["ColorB.x"] = 1.0f; params["ColorB.y"] = 1.0f; params["ColorB.z"] = 1.0f; params["ColorB.w"] = 1.0f;
  params["ColorC.x"] = 1.0f; params["ColorC.y"] = 1.0f; params["ColorC.z"] = 1.0f; params["ColorC.w"] = 1.0f;
  params["SelectChannel_R"] = 0.0f;   // ImageA_R
  params["SelectChannel_G"] = 6.0f;   // ImageB_G
  params["SelectChannel_B"] = 12.0f;  // ImageC_B
  params["SelectAlphaChannel"] = 4.0f;  // SetToOne
  g_c3IgnoreImageC = ignoreC;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.output = dst; c.params = &params;
  c.inputTextures[0] = imageA; c.inputTextures[1] = imageB; c.inputTextures[2] = imageC;
  c.inputTextureCount = 3;
  c.inputTexture = imageA;
  cookCombine3Images(c);
  g_c3IgnoreImageC = false;

  std::vector<uint8_t> px((size_t)kGW * kGH * 4, 0);
  dst->getBytes(px.data(), kGW * 4, MTL::Region::Make2D(0, 0, kGW, kGH), 0);
  const uint32_t cx = kGW / 2, cy = kGH / 2;
  size_t i = ((size_t)cy * kGW + cx) * 4;
  out[0] = px[i]; out[1] = px[i + 1]; out[2] = px[i + 2]; out[3] = px[i + 3];

  imageA->release(); imageB->release(); imageC->release(); dst->release();
  return true;
}

int runCombine3ImagesSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-combine3images] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const int kTol = 2;
  uint8_t got[4] = {0, 0, 0, 0};
  c3CookCenter(dev, q, lib, /*ignoreC=*/injectBug, got);

  // INVARIANT (non-degenerate): the three packed channels are genuinely different (R!=G!=B), proving the
  // op picks a distinct channel from each image, not a constant.
  bool nonDegenerate = (kExpR != kExpG) && (kExpG != kExpB) && (kExpR != kExpB);

  int dR = std::abs((int)got[0] - (int)kExpR);
  int dG = std::abs((int)got[1] - (int)kExpG);
  int dB = std::abs((int)got[2] - (int)kExpB);
  int dA = std::abs((int)got[3] - (int)kExpA);
  bool match = dR <= kTol && dG <= kTol && dB <= kTol && dA <= kTol;

  bool pass = nonDegenerate && match;
  printf("[selftest-combine3images] want=(%u,%u,%u,%u) got=(%u,%u,%u,%u) "
         "d=(%d,%d,%d,%d) match(<=%d)=%d nonDeg=%d injectBug=%d -> %s\n",
         kExpR, kExpG, kExpB, kExpA, got[0], got[1], got[2], got[3], dR, dG, dB, dA,
         kTol, match ? 1 : 0, nonDegenerate ? 1 : 0, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release();
  clearTexOpCache();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
