// CombineMaterialChannels2 image-filter texture op (lane multi-image, image/use) — the PBR twin of
// Combine3Images: it packs three material maps (e.g. roughness / metallic / ambient-occlusion) into the
// R/G/B/A channels of one texture for SetMaterial. TiXL authority:
//   external/tixl Operators/Lib/image/use/CombineMaterialChannels2.cs  — op ports (ImageA/B/C, ColorA/B/C,
//       SelectChannel_R/G/B, SelectAlphaChannel, GenerateMips) + the two enums (15-way channel select,
//       5-way alpha) — BYTE-FOR-BYTE the same port set + enum tables as Combine3Images (only the GUIDs
//       differ; GUIDs are irrelevant in sw, which keys by type name + port id).
//   .../CombineMaterialChannels2.t3  — _trippleImageFxSetup compound. STEP-0 BACKWARD-TRACE + the Cut-55
//       FloatsToBuffer connection-order rule (verified by direct .t3 parse, CLEAN 1:1, no math junctions):
//         FloatParams MultiInput order = Vector4Components(ColorA).{X,Y,Z,W},
//         Vector4Components(ColorB).{X,Y,Z,W}, Vector4Components(ColorC).{X,Y,Z,W},
//         IntToFloat(SelectChannel_R), IntToFloat(SelectChannel_G), IntToFloat(SelectChannel_B),
//         IntToFloat(SelectAlphaChannel) -> EXACTLY the img-combine-3.hlsl cbuffer field order.
//         Image ports wire directly: ImageA->_trippleImageFxSetup.ImageA(t0), ImageB->.ImageB(t1),
//         ImageC->.ImageC(t2). Identical structure to Combine3Images.t3 (same _trippleImageFxSetup, same
//         3×Vector4Components + 4×IntToFloat).
//   .../img-combine-3.hlsl — the SAME pixel shader as Combine3Images (.t3 ShaderPath ==
//       "Lib:shaders/img/use/img-combine-3.hlsl"). REUSED verbatim: this leaf binds the SAME ported
//       shader (combine3images_vs/_fs in combine3images.metal) + the SAME params struct
//       (Combine3ImagesParams, combine3images_params.h). No second shader/header — one kernel, two ops
//       (mirrors how TiXL points both .t3 at the same .hlsl).
//
// SEAM NOTE — see point_ops_combine3images.cpp: the multi-image seam already gathers up to
// kMaxTexInputs=4 Texture2D ports into TexCookCtx::inputTextures[] in spec order (flat + resident), so a
// 3-input leaf needs ZERO shared-graph edit.
//
// FORK (named): unwired ImageB/ImageC -> sample ImageA (same fork class as Combine3Images / Displace /
// DistortAndShade — a null texture can't sample, reusing ImageA keeps the picture, no crash). TiXL's
// GenerateMips + WrapMode("Wrap"->Repeat) host plumbing handled identically to Combine3Images.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/combine3images_params.h"  // Combine3ImagesParams, COMBINE3IMAGES_Params (REUSED)
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

// injectBug hook (golden only): drop the ImageC gather (-> falls back to ImageA). The B output (default
// B<-ImageC_B) then reads ImageA.b -> the pinned value misses. Directly exercises the 3rd Texture2D port.
bool g_cmc2IgnoreImageC = false;

// CombineMaterialChannels2 cook: read ImageA([0])+ImageB([1])+ImageC([2]), one fullscreen pass into
// c.output. Binds the SAME ported shader + params as Combine3Images.
void cookCombineMaterialChannels2(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  const MTL::Texture* imageA = c.inputTextureCount > 0 ? c.inputTextures[0] : nullptr;
  const MTL::Texture* imageB = c.inputTextureCount > 1 ? c.inputTextures[1] : nullptr;
  const MTL::Texture* imageC = c.inputTextureCount > 2 ? c.inputTextures[2] : nullptr;
  if (g_cmc2IgnoreImageC) imageC = nullptr;  // golden injectBug: drop the third image
  if (!imageA) { clearTexture(c.queue, c.output); return; }
  if (!imageB) imageB = imageA;  // fork: unwired ImageB -> ImageA
  if (!imageC) imageC = imageA;  // fork: unwired ImageC -> ImageA

  // SAME ported shader as Combine3Images (one kernel, two ops).
  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "combine3images_vs", "combine3images_fs", fmt);
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeRepeat);  // TiXL WrapMode "Wrap"; not load-bearing (in-[0,1] uv)
  sd->setTAddressMode(MTL::SamplerAddressModeRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // Defaults verbatim from CombineMaterialChannels2.t3 (identical to Combine3Images.t3).
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
  enc->setFragmentTexture(const_cast<MTL::Texture*>(imageA), 0);  // t0 = ImageA
  enc->setFragmentTexture(const_cast<MTL::Texture*>(imageB), 1);  // t1 = ImageB
  enc->setFragmentTexture(const_cast<MTL::Texture*>(imageC), 2);  // t2 = ImageC
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(Combine3ImagesParams), COMBINE3IMAGES_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache)
}

}  // namespace

int runCombineMaterialChannels2SelfTest(bool injectBug);

// Self-registration. Ports 1:1 from CombineMaterialChannels2.cs — IDENTICAL set to Combine3Images.cs
// (same enums, same defaults). Type name "CombineMaterialChannels2" (its own Add-menu identity). FORKS:
// same as Combine3Images (Repeat sampler not load-bearing; GenerateMips omitted; unwired B/C -> ImageA).
static const ImageFilterOp _reg_combinematerialchannels2{
    {"CombineMaterialChannels2", "CombineMaterialChannels2",
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
    "CombineMaterialChannels2", cookCombineMaterialChannels2, "combinematerialchannels2",
    runCombineMaterialChannels2SelfTest};

// --- CombineMaterialChannels2 FLAT CHANNEL-PACK GOLDEN (closed-form, d=0 saturated solid) ----------
// Same kernel as Combine3Images; a PBR-flavored controlled input set so it's an independent witness.
// Three FLAT solids: ImageA="roughness" (r=120), ImageB="metallic" (g=64), ImageC="ao" (b=210). Identity
// tints, default selects (R<-ImageA_R, G<-ImageB_G, B<-ImageC_B), AlphaMode=SetToOne(4) -> the packed
// material = (120, 64, 210, 255). d=0 plateau, non-degenerate (R!=G!=B). injectBug drops ImageC -> B
// reads ImageA.b (=10) instead of 210 -> RED (200-LSB miss on B).
constexpr uint32_t kGW = 32, kGH = 32;
constexpr uint8_t kA_r = 120, kA_g = 10,  kA_b = 10;   // ImageA roughness
constexpr uint8_t kB_r = 10,  kB_g = 64,  kB_b = 10;   // ImageB metallic
constexpr uint8_t kC_r = 10,  kC_g = 10,  kC_b = 210;  // ImageC ao
constexpr uint8_t kExpR = kA_r, kExpG = kB_g, kExpB = kC_b, kExpA = 255;

static void fillSolid(MTL::Texture* t, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b) {
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  for (size_t i = 0; i < (size_t)w * h; ++i) {
    px[i * 4 + 0] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255;
  }
  t->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

static bool cmc2CookCenter(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool ignoreC,
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
  params["ColorA.x"] = 1.0f; params["ColorA.y"] = 1.0f; params["ColorA.z"] = 1.0f; params["ColorA.w"] = 1.0f;
  params["ColorB.x"] = 1.0f; params["ColorB.y"] = 1.0f; params["ColorB.z"] = 1.0f; params["ColorB.w"] = 1.0f;
  params["ColorC.x"] = 1.0f; params["ColorC.y"] = 1.0f; params["ColorC.z"] = 1.0f; params["ColorC.w"] = 1.0f;
  params["SelectChannel_R"] = 0.0f;   // ImageA_R
  params["SelectChannel_G"] = 6.0f;   // ImageB_G
  params["SelectChannel_B"] = 12.0f;  // ImageC_B
  params["SelectAlphaChannel"] = 4.0f;  // SetToOne
  g_cmc2IgnoreImageC = ignoreC;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.output = dst; c.params = &params;
  c.inputTextures[0] = imageA; c.inputTextures[1] = imageB; c.inputTextures[2] = imageC;
  c.inputTextureCount = 3;
  c.inputTexture = imageA;
  cookCombineMaterialChannels2(c);
  g_cmc2IgnoreImageC = false;

  std::vector<uint8_t> px((size_t)kGW * kGH * 4, 0);
  dst->getBytes(px.data(), kGW * 4, MTL::Region::Make2D(0, 0, kGW, kGH), 0);
  const uint32_t cx = kGW / 2, cy = kGH / 2;
  size_t i = ((size_t)cy * kGW + cx) * 4;
  out[0] = px[i]; out[1] = px[i + 1]; out[2] = px[i + 2]; out[3] = px[i + 3];

  imageA->release(); imageB->release(); imageC->release(); dst->release();
  return true;
}

int runCombineMaterialChannels2SelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-combinematerialchannels2] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const int kTol = 2;
  uint8_t got[4] = {0, 0, 0, 0};
  cmc2CookCenter(dev, q, lib, /*ignoreC=*/injectBug, got);

  bool nonDegenerate = (kExpR != kExpG) && (kExpG != kExpB) && (kExpR != kExpB);
  int dR = std::abs((int)got[0] - (int)kExpR);
  int dG = std::abs((int)got[1] - (int)kExpG);
  int dB = std::abs((int)got[2] - (int)kExpB);
  int dA = std::abs((int)got[3] - (int)kExpA);
  bool match = dR <= kTol && dG <= kTol && dB <= kTol && dA <= kTol;

  bool pass = nonDegenerate && match;
  printf("[selftest-combinematerialchannels2] want=(%u,%u,%u,%u) got=(%u,%u,%u,%u) "
         "d=(%d,%d,%d,%d) match(<=%d)=%d nonDeg=%d injectBug=%d -> %s\n",
         kExpR, kExpG, kExpB, kExpA, got[0], got[1], got[2], got[3], dR, dG, dB, dA,
         kTol, match ? 1 : 0, nonDegenerate ? 1 : 0, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release();
  clearTexOpCache();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
