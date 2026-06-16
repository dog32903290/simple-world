// DistortAndShade image-filter texture op (lane multi-image, image/fx/distort) — the SECOND consumer
// of the multi-image seam (Displace was the first). TiXL authority:
//   external/tixl Operators/Lib/image/fx/distort/DistortAndShade.cs  — op ports (ImageA, ImageB,
//       Displacement, Center, Shading, ShadeColor).
//   .../DistortAndShade.t3   — _multiImageFxSetup compound. STEP-0 BACKWARD-TRACE of the two shader
//       SRV slots (the Cut-58 lesson: trace each binding back to its true source, do NOT forward-assume):
//         t0 (ImageA in the .hlsl) <- SrvFromTexture2d(88206ecf) <- root op port ImageA (824e1ad4).
//         t1 (ImageB in the .hlsl) <- SrvFromTexture2d(633ea694) <- root op port ImageB (2596e8fb).
//       (Both SrvFromTexture2d feed the SAME SetPixelAndVertexShaderStage SRV-array slot 83fdb275; the
//       connection ORDER 88206ecf-then-633ea694 puts ImageA at SRV index 0 = t0 and ImageB at index 1 =
//       t1, matching the .hlsl register(t0)/register(t1).) BOTH are graph-wired op ports — NOT assets,
//       NOT dangling. (Two ColorGrade children on ImageA/ImageB DANGLE — their outputs connect to
//       nothing — so they don't touch the image path.)
//   .../DistortAndShade.hlsl — the pixel shader (ported 1:1 -> distortandshade.metal).
//
// PARAM ROUTING (STEP-0, the Cut-55 .t3 connection-order rule — verified clean 1:1, no math nodes):
// the _multiImageFxSetup FloatsToBuffer MultiInput is fed by 8 connections whose order EXACTLY matches
// the .hlsl cbuffer field order: Vector4Components(ShadeColor).{X,Y,Z,W}, root.Displacement,
// root.Shading, Vector2Components(Center).{X,Y}. The Vector*Components are identity splitters. So
// ShadeColor<-ShadeColor, Displacement<-Displacement, Shade<-Shading, Center<-Center. (See
// distortandshade_params.h for the field<-source table + the static_assert.)
//
// SEAM NOTE — the multi-image seam is ALREADY OPEN and CONSUMED. cookTexNode (flat point_graph.cpp +
// resident point_graph_resident.cpp) already recurses EVERY Texture2D input port in spec order into
// TexCookCtx::inputTextures[] / inputTextureCount (Displace shipped that). A leaf with TWO Texture2D
// ports therefore gets both upstream textures cooked-and-bound with ZERO shared-file edit. This leaf is
// the second proof of that seam (Displace = Image+DisplaceMap; here = ImageA+ImageB).
//
// FORK (named): an unwired ImageB -> the op falls back to sampling ImageA as the displace source (an
// unwired Texture2D slot is null; a null texture can't sample, so reusing ImageA keeps the picture
// visible rather than crashing — same fork class as Displace's unwired-DisplaceMap). With ImageA as
// its own displaceAmount the warp is content-driven but harmless; at the .t3 default Shading=0 the
// shade term is also zero, so an unwired-ImageB DistortAndShade at default is ~ImageA passthrough +
// a self-referential warp scaled by Displacement.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/distortandshade_params.h"  // DistortAndShadeParams, DISTORTANDSHADE_Params
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

// Clear `out` to black (no ImageA -> nothing to render; mirrors cookDisplace's empty path).
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

// injectBug hook (golden only): when set, the ImageB displacement input is IGNORED (treated as if
// ImageB were unwired -> falls back to ImageA). A real wiring perturbation: the second-image warp
// source is gone, so the displaced output collapses toward the unwarped/self-warped path and the
// pinned pixels (which ride the ImageB-driven uv2 shift) miss.
bool g_dasIgnoreImageB = false;

// DistortAndShade texture op: read ImageA (inputTextures[0]) + ImageB (inputTextures[1]), one
// fullscreen pass into c.output. ImageA @ texture(0), ImageB @ texture(1) (the .hlsl t0/t1).
void cookDistortAndShade(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  const MTL::Texture* imageA = c.inputTextureCount > 0 ? c.inputTextures[0] : nullptr;
  const MTL::Texture* imageB = c.inputTextureCount > 1 ? c.inputTextures[1] : nullptr;
  if (g_dasIgnoreImageB) imageB = nullptr;  // golden injectBug: drop the second image
  if (!imageA) { clearTexture(c.queue, c.output); return; }  // no ImageA -> nothing to render
  if (!imageB) imageB = imageA;  // fork: unwired ImageB -> sample ImageA (keeps picture, no crash)

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "distortandshade_vs", "distortandshade_fs", fmt);
  if (!rps) return;

  // Sampler: linear + MirrorRepeat (TiXL _multiImageFxSetup.WrapMode = "Mirror"; D3D11 MIRROR =
  // Metal MirrorRepeat). The warp pushes uv2 OOB at nonzero Displacement, so the address mode is
  // load-bearing — MirrorRepeat reflects the edge (TiXL parity) instead of clamping.
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeMirrorRepeat);
  sd->setTAddressMode(MTL::SamplerAddressModeMirrorRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL params (DistortAndShade.cs / .t3 defaults). ShadeColor default (1,1,1,1); Displacement 0.5;
  // Shade(=Shading) 0; Center (0.5,0.5). Routing verified 1:1 (see header).
  DistortAndShadeParams p{};
  p.ShadeColorR = cookParam(c, "ShadeColor.x", 1.0f);
  p.ShadeColorG = cookParam(c, "ShadeColor.y", 1.0f);
  p.ShadeColorB = cookParam(c, "ShadeColor.z", 1.0f);
  p.ShadeColorA = cookParam(c, "ShadeColor.w", 1.0f);
  p.Displacement = cookParam(c, "Displacement", 0.5f);
  p.Shade = cookParam(c, "Shading", 0.0f);  // TiXL port is "Shading"; .hlsl field is "Shade"
  p.CenterX = cookParam(c, "Center.x", 0.5f);
  p.CenterY = cookParam(c, "Center.y", 0.5f);

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
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(DistortAndShadeParams), DISTORTANDSHADE_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

int runDistortAndShadeSelfTest(bool injectBug);

// Self-registration. Ports 1:1 from DistortAndShade.cs: ImageA + ImageB (two Texture2D inputs) +
// Displacement/Center(Vec2)/Shading/ShadeColor(Vec4). Defaults verbatim from DistortAndShade.t3.
// FORKS (named): TiXL's WrapMode "Mirror" -> MirrorRepeat sampler (in cook); an unwired ImageB samples
// ImageA. NodeSpec UI ranges are hints only (t3ui declares none); cook uses the actual param values.
static const ImageFilterOp _reg_distortandshade{
    {"DistortAndShade", "DistortAndShade",
     {{"ImageA", "ImageA", "Texture2D", true},
      {"ImageB", "ImageB", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      {"Displacement", "Displacement", "Float", true, 0.5f, -1.0f, 1.0f},
      {"Shading", "Shading", "Float", true, 0.0f, 0.0f, 1.0f},
      {"Center.x", "Center", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"Center.y", "Center.y", "Float", true, 0.5f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ShadeColor.x", "ShadeColor", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ShadeColor.y", "ShadeColor.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"ShadeColor.z", "ShadeColor.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"ShadeColor.w", "ShadeColor.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "DistortAndShade", cookDistortAndShade, "distortandshade", runDistortAndShadeSelfTest};


// --- DistortAndShade EXACT-PIXEL GOLDEN (hand-derived from the displacement math) -----------------
// The op computes uv2 = uv + (uv - Center) * displaceAmount * Displacement, then samples ImageA at uv2
// and lerps toward ShadeColor by Shade*displaceAmount. We pick controlled inputs that make the sampled
// luminance a HAND-COMPUTABLE function of uv:
//
//   ImageA = a HORIZONTAL LUMINANCE RAMP: lum(texel x) = round(x*255/(W-1)) (left dark -> right bright,
//            constant in Y). Sampling at uv2.x reads ~ 255*uv2.x (linear, bilinear-filtered).
//   ImageB = a UNIFORM CONSTANT field B (every texel = kB/255) -> displaceAmount = kB/255 everywhere.
//   Shading = 0 -> lerp weight Shade*displaceAmount = 0 -> output = ImageA.Sample(uv2) (no shade term).
//   Center = (0, 0.5).  Displacement = D = 1.0.
//
// With Center.x=0: uv2.x = uv.x + (uv.x - 0)*B*D = uv.x*(1 + B*D). On the CENTER ROW (y=H/2), uv.y ~=
// 0.5 so (uv.y - Center.y) ~= 0 -> uv2.y ~= uv.y (sample stays on the same row). For a pixel at column
// x, uv.x=(x+0.5)/W; the sampled luminance is the RAMP evaluated at uv2.x = uv.x*(1+B*D) (clamped to
// [0,1] then *255). This is hand-computable per pixel (see kDasPins derivation). The ramp + uniform B
// makes uv2 a SMOOTH magnification from the left edge -> no edge-crossing filtering ambiguity; each
// pin's expected value comes straight from 255*(ramp at uv2.x), pinned ±2.
//
// HAND-DERIVATION (W=H=64, kB=128 -> B=0.50196, D=1.0; g=B*D=0.50196; uv2.x=uv.x*(1+g)=uv.x*1.50196):
//   x=8 : uv.x=8.5/64=0.1328;  uv2.x=0.1995; ramp(0.1995)~=255*0.1995 adj for texel-center ~= 50.
//   x=16: uv.x=16.5/64=0.2578; uv2.x=0.3872; ~= 98.
//   x=24: uv.x=24.5/64=0.3828; uv2.x=0.5750; ~= 147.
//   x=40: uv.x=40.5/64=0.6328; uv2.x=0.9505; ~= 244.
// (Values pinned to the verified GREEN GPU output, which matches these hand predictions within ±2; the
// hand math is the GROUND TRUTH — if the GPU disagreed by more than the sampler's sub-LSB rounding the
// math would be wrong, not the pin.) A wrong Displacement, a swapped Center, a swapped t0/t1 binding,
// or an ignored ImageB MOVES uv2 and shifts every pin by >>2 (see injectBug below).
//
// injectBug (g_dasIgnoreImageB) drops ImageB -> the op falls back to sampling ImageA (the RAMP) as the
// displaceAmount. Then displaceAmount = ramp(uv.x) (spatially varying, 0..1) instead of the uniform
// 0.502, so uv2.x = uv.x*(1 + ramp(uv.x)*D) is a DIFFERENT (nonlinear) magnification. Hand-checked: the
// four pins diverge by ~11-16 LSB (x=8: 50->36, x=16: 98->82, x=24: 147->135, x=40: 244->255), all
// well past ±2 -> RED. A real wiring perturbation that removes the second-image contribution.
constexpr uint32_t kGW = 64, kGH = 64;
constexpr uint8_t kB = 128;  // ImageB uniform value = 0.50196; g = B*D
constexpr float kD = 1.0f;   // Displacement

struct DasPin { uint32_t x; uint8_t v; };  // expected luminance (R=G=B) at center row
constexpr DasPin kDasPins[] = {
    {8, 50}, {16, 98}, {24, 147}, {40, 244},
};

// Cook DistortAndShade on the controlled ramp/uniform inputs; read back center row. ignoreB ->
// injectBug path (ImageB dropped -> ramp self-displaces).
static bool dasCookRampRow(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool ignoreB,
                           std::vector<uint8_t>& out) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kGW, kGH, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* imageA = dev->newTexture(td);
  MTL::Texture* imageB = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // ImageA: horizontal luminance ramp (constant in Y).
  std::vector<uint8_t> a((size_t)kGW * kGH * 4, 0);
  for (uint32_t y = 0; y < kGH; ++y)
    for (uint32_t x = 0; x < kGW; ++x) {
      size_t i = ((size_t)y * kGW + x) * 4;
      uint8_t v = (uint8_t)std::lround((double)x * 255.0 / (double)(kGW - 1));
      a[i] = v; a[i + 1] = v; a[i + 2] = v; a[i + 3] = 255;
    }
  imageA->replaceRegion(MTL::Region::Make2D(0, 0, kGW, kGH), 0, a.data(), kGW * 4);

  // ImageB: uniform kB everywhere -> displaceAmount = kB/255 constant.
  std::vector<uint8_t> b((size_t)kGW * kGH * 4, kB);
  for (size_t i = 0; i < (size_t)kGW * kGH; ++i) b[i * 4 + 3] = 255;
  imageB->replaceRegion(MTL::Region::Make2D(0, 0, kGW, kGH), 0, b.data(), kGW * 4);

  std::map<std::string, float> params;
  params["Displacement"] = kD;
  params["Shading"] = 0.0f;   // no shade -> output = ImageA.Sample(uv2)
  params["Center.x"] = 0.0f;  // displacement radiates from the LEFT edge -> uv2.x = uv.x*(1+B*D)
  params["Center.y"] = 0.5f;
  g_dasIgnoreImageB = ignoreB;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.output = dst; c.params = &params;
  c.inputTextures[0] = imageA; c.inputTextures[1] = imageB; c.inputTextureCount = 2;
  c.inputTexture = imageA;
  cookDistortAndShade(c);
  g_dasIgnoreImageB = false;

  out.assign((size_t)kGW * kGH * 4, 0);
  dst->getBytes(out.data(), kGW * 4, MTL::Region::Make2D(0, 0, kGW, kGH), 0);
  imageA->release(); imageB->release(); dst->release();
  return true;
}

int runDistortAndShadeSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-distortandshade] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const uint32_t Y = kGH / 2;
  auto lum = [&](const std::vector<uint8_t>& v, uint32_t x) {
    return (int)v[((size_t)Y * kGW + x) * 4];
  };
  const int kTol = 2;

  // Cook on the controlled inputs (injectBug -> ImageB dropped). EXACT-pixel pins (hand-derived from
  // the displacement math) must hold for correct wiring; the bug shifts every pin >>2 -> RED.
  std::vector<uint8_t> got;
  dasCookRampRow(dev, q, lib, /*ignoreB=*/injectBug, got);

  // INVARIANT: the pins are a real ramp (not all equal) -> the op genuinely warps + samples a gradient.
  bool reshapes = false;
  for (size_t i = 1; i < sizeof(kDasPins) / sizeof(kDasPins[0]); ++i)
    if (std::abs((int)kDasPins[i].v - (int)kDasPins[0].v) > 3) reshapes = true;

  bool matchPins = true;
  int maxDelta = 0;
  for (const DasPin& p : kDasPins) {
    int d = std::abs(lum(got, p.x) - (int)p.v);
    maxDelta = std::max(maxDelta, d);
    if (d > kTol) matchPins = false;
  }

  bool pass = reshapes && matchPins;
  printf("[selftest-distortandshade] pins maxDelta=%d match(<=%d)=%d reshapes=%d injectBug=%d -> %s\n",
         maxDelta, kTol, matchPins ? 1 : 0, reshapes ? 1 : 0, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");
  for (const DasPin& p : kDasPins)
    printf("  pin px(%u,%u) want=%d got=%d\n", p.x, Y, p.v, lum(got, p.x));

  lib->release(); q->release(); dev->release();
  clearTexOpCache();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
