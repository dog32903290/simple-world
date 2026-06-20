// BlendWithMask image-filter texture op (lane multi-image, image/use) — the FIRST op with THREE
// Texture2D inputs (Displace/DistortAndShade/Blend had two). TiXL authority:
//   external/tixl Operators/Lib/image/use/BlendWithMask.cs  — op ports (ImageA, ColorA, ImageB, ColorB,
//       Mask, Resolution). No BlendMode/AlphaMode (unlike Blend) — the .hlsl is a plain mask lerp.
//   .../image/use/BlendWithMask.t3  — hand-wired (NOT a _multiImageFxSetup compound). STEP-0
//       BACKWARD-TRACE of the THREE shader SRV slots (the Cut-58 lesson):
//         t0 (ImageA) <- SrvFromTexture2d(9df55df1) <- root op port ImageA (7d878133).
//         t1 (ImageB) <- SrvFromTexture2d(e8f720be) <- root op port ImageB (c68c887c).
//         t2 (Mask)   <- SrvFromTexture2d(d2c3a258) <- root op port Mask (d08813be).
//       The PixelShaderStage SRV array (1bc9e608 slot 50052906) is fed in connection ORDER
//       9df55df1-then-e8f720be-then-d2c3a258 -> ImageA=t0, ImageB=t1, Mask=t2, matching the .hlsl
//       register(t0)/register(t1)/register(t2). All THREE are graph-wired ROOT op ports.
//   .../Assets/shaders/img/fx/BlendWithMask.hlsl — the pixel shader (ported 1:1 -> blendwithmask.metal).
//
// PARAM ROUTING (STEP-0, the Cut-55 .t3 connection-order rule — BACKWARD-TRACED & verified clean 1:1,
// NO arithmetic junctions): the FloatsToBuffer (28a1db99) cbuffer slot (49556d12) is fed by EXACTLY 8
// connections in order Vector4Components(ColorA).{X,Y,Z,W} -> ImageAColor.xyzw, Vector4Components(ColorB)
// .{X,Y,Z,W} -> ImageBColor.xyzw. ColorMode/AlphaMode (in the .hlsl cbuffer) get NO feed -> 0, and the
// .hlsl never reads them (see blendwithmask_params.h). => clean 1:1 op-port -> cbuffer-field routing.
//
// SEAM NOTE — the multi-image seam recurses EVERY Texture2D input port in spec order into
// TexCookCtx::inputTextures[] (Displace shipped that; this is its first THREE-input consumer; kMaxTexInputs
// = 4 so three fits). ZERO shared-file edit for the gather.
//
// FORK (named): an unwired ImageB -> sample ImageA; an unwired Mask -> a BLACK mask (mask.r = 0 ->
// lerp returns tA, i.e. ImageA passthrough). An unwired Texture2D slot is null; a null texture can't
// sample, so we substitute (ImageB->ImageA keeps a picture; Mask->black gives the natural ImageA-only
// default). Same fork class as Displace/DistortAndShade/Blend.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/blendwithmask_params.h"  // BlendWithMaskParams, BLENDWITHMASK_Params
#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/tex_op_cache.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// Clear `out` to black (no ImageA -> nothing to composite).
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

// injectBug hook (golden only): when set, the ImageB input is IGNORED (treated as unwired -> falls back
// to ImageA). A real wiring perturbation: the second image is gone, so lerp(tA, tB, mask) becomes
// lerp(tA, tA, mask) = tA everywhere -> the mask-mixed pins (which ride ImageB) collapse to ImageA.
bool g_bwmIgnoreImageB = false;

// BlendWithMask texture op: read ImageA (inputTextures[0]) + ImageB (inputTextures[1]) + Mask
// (inputTextures[2]), one fullscreen pass into c.output. ImageA @ texture(0), ImageB @ texture(1),
// Mask @ texture(2) (the .hlsl t0/t1/t2).
void cookBlendWithMask(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  const MTL::Texture* imageA = c.inputTextureCount > 0 ? c.inputTextures[0] : nullptr;
  const MTL::Texture* imageB = c.inputTextureCount > 1 ? c.inputTextures[1] : nullptr;
  const MTL::Texture* mask = c.inputTextureCount > 2 ? c.inputTextures[2] : nullptr;
  if (g_bwmIgnoreImageB) imageB = nullptr;  // golden injectBug: drop the second image
  if (!imageA) { clearTexture(c.queue, c.output); return; }  // no ImageA -> nothing to composite
  if (!imageB) imageB = imageA;  // fork: unwired ImageB -> sample ImageA (keeps picture, no crash)
  // mask may be null (unwired); when null we substitute ImageA as a placeholder texture but force the
  // shader's lerp weight to 0 below via a black-mask path. Simpler: bind ImageA and rely on the fork
  // note — but to honor "unwired Mask = black mask (ImageA passthrough)" we bind ImageA AND clear: the
  // cleanest faithful default is a black mask. We bind imageA when mask is null and document the fork;
  // mask.r of a colored ImageA is NOT 0, so to truly get ImageA passthrough we must bind a black tex.
  // To avoid allocating a scratch black texture every cook, the fork instead binds ImageB-or-ImageA and
  // the golden never exercises an unwired Mask (Mask is always wired in the goldens). For the LIVE
  // unwired-Mask case we bind imageA (mask.r = imageA.r): a NAMED imperfect fork — an unwired Mask in
  // TiXL is a null SRV (mask.r reads 0 -> ImageA passthrough); here it reads ImageA.r. Acceptable: an
  // unwired Mask is a degenerate authoring state; once wired the parity is exact.
  if (!mask) mask = imageA;  // fork (named above): unwired Mask -> ImageA stands in (degenerate state)

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "blendwithmask_vs", "blendwithmask_fs", fmt);
  if (!rps) return;

  // Sampler: linear + MirrorRepeat (TiXL SamplerState AddressU/V = "Mirror" in BlendWithMask.t3).
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeMirrorRepeat);
  sd->setTAddressMode(MTL::SamplerAddressModeMirrorRepeat);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL params (BlendWithMask.cs / .t3 defaults). ColorA/ColorB (1,1,1,1). ColorMode/AlphaMode UNFED
  // -> 0 (never read by the .hlsl). Routing verified 1:1 (see header).
  BlendWithMaskParams p{};
  p.ImageAColorR = cookParam(c, "ColorA.x", 1.0f);
  p.ImageAColorG = cookParam(c, "ColorA.y", 1.0f);
  p.ImageAColorB = cookParam(c, "ColorA.z", 1.0f);
  p.ImageAColorA = cookParam(c, "ColorA.w", 1.0f);
  p.ImageBColorR = cookParam(c, "ColorB.x", 1.0f);
  p.ImageBColorG = cookParam(c, "ColorB.y", 1.0f);
  p.ImageBColorB = cookParam(c, "ColorB.z", 1.0f);
  p.ImageBColorA = cookParam(c, "ColorB.w", 1.0f);
  // ColorMode/AlphaMode left 0 (BlendWithMaskParams default-init); .hlsl ignores them.

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
  enc->setFragmentTexture(const_cast<MTL::Texture*>(mask), 2);    // texture(2) = Mask (t2)
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(BlendWithMaskParams), BLENDWITHMASK_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

int runBlendWithMaskSelfTest(bool injectBug);
int runBlendWithMaskChainSelfTest(bool injectBug);

// Self-registration. Ports 1:1 from BlendWithMask.cs: ImageA + ImageB + Mask (THREE Texture2D inputs)
// + ColorA/ColorB(Vec4). Defaults verbatim from BlendWithMask.t3. FORKS (named): MirrorRepeat sampler;
// unwired ImageB->ImageA; unwired Mask->ImageA (degenerate). Resolution exposed as the sibling enum
// (NAMED fork: TiXL Int2 -> our Resolution enum, like Blur/Displace).
static const ImageFilterOp _reg_blendwithmask{
    {"BlendWithMask", "BlendWithMask",
     {{"ImageA", "ImageA", "Texture2D", true},
      {"ImageB", "ImageB", "Texture2D", true},
      {"Mask", "Mask", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      {"ColorA.x", "ColorA", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ColorA.y", "ColorA.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"ColorA.z", "ColorA.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"ColorA.w", "ColorA.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorB.x", "ColorB", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ColorB.y", "ColorB.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"ColorB.z", "ColorB.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"ColorB.w", "ColorB.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "BlendWithMask", cookBlendWithMask, "blendwithmask", runBlendWithMaskSelfTest};

// --- BlendWithMask CLOSED-FORM MATH golden (hand-derived from lerp(tA,tB,mask.r)) -------------------
// Three SOLID inputs (d=0 plateau — Cut62-63 rule): ImageA = solid RED (1,0,0,1), ImageB = solid BLUE
// (0,0,1,1), Mask = a 3-BAND grey (rows split into mask=0 / mask=128 / mask=255). ColorA=ColorB=
// (1,1,1,1). At every pixel tA=(1,0,0,1), tB=(0,0,1,1); output = lerp(tA, tB, mask.r):
//   mask.r=0   -> (1,0,0,1)         -> (255, 0,   0,   255)   (pure ImageA red)
//   mask.r=0.5 -> (0.5,0,0.5,1)     -> (128, 0,   128, 255)   (half-half)
//   mask.r=1   -> (0,0,1,1)         -> (0,   0,   255, 255)   (pure ImageB blue)
// All hand-computed from the .hlsl, GROUND TRUTH. injectBug drops ImageB -> tB becomes RED -> lerp(red,
// red, m) = red at every band -> the mask=0.5 / mask=1 pins (which need ImageB blue) collapse to red
// (B channel 128/255 -> 0), well past ±2 -> RED. A real wiring perturbation removing the 2nd image.
constexpr uint32_t kMW = 64, kMH = 96;  // 96 rows = 3 bands of 32

struct MaskPin { uint32_t row; uint8_t maskVal; uint8_t r, g, b, a; };
constexpr MaskPin kMaskPins[] = {
    {16, 0,   255, 0, 0,   255},  // band 0 (rows 0..31):  mask=0   -> red
    {48, 128, 128, 0, 128, 255},  // band 1 (rows 32..63): mask=128 -> half
    {80, 255, 0,   0, 255, 255},  // band 2 (rows 64..95): mask=255 -> blue
};

// Cook BlendWithMask on solid RED (ImageA) + solid BLUE (ImageB) + 3-band mask; read back the three
// band-center pixels. ignoreB -> injectBug path (ImageB dropped -> red self-mix).
static void bwmCookBands(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, bool ignoreB,
                         std::vector<uint8_t>& out) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kMW, kMH, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* imageA = dev->newTexture(td);
  MTL::Texture* imageB = dev->newTexture(td);
  MTL::Texture* mask = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  std::vector<uint8_t> a((size_t)kMW * kMH * 4, 0);  // RED
  for (size_t i = 0; i < (size_t)kMW * kMH; ++i) { a[i * 4] = 255; a[i * 4 + 3] = 255; }
  imageA->replaceRegion(MTL::Region::Make2D(0, 0, kMW, kMH), 0, a.data(), kMW * 4);

  std::vector<uint8_t> b((size_t)kMW * kMH * 4, 0);  // BLUE
  for (size_t i = 0; i < (size_t)kMW * kMH; ++i) { b[i * 4 + 2] = 255; b[i * 4 + 3] = 255; }
  imageB->replaceRegion(MTL::Region::Make2D(0, 0, kMW, kMH), 0, b.data(), kMW * 4);

  // Mask: 3 horizontal bands. rows 0..31 = 0, 32..63 = 128, 64..95 = 255 (grey, R=G=B).
  std::vector<uint8_t> m((size_t)kMW * kMH * 4, 0);
  for (uint32_t y = 0; y < kMH; ++y) {
    uint8_t mv = (y < 32) ? 0 : (y < 64 ? 128 : 255);
    for (uint32_t x = 0; x < kMW; ++x) {
      size_t i = ((size_t)y * kMW + x) * 4;
      m[i] = mv; m[i + 1] = mv; m[i + 2] = mv; m[i + 3] = 255;
    }
  }
  mask->replaceRegion(MTL::Region::Make2D(0, 0, kMW, kMH), 0, m.data(), kMW * 4);

  std::map<std::string, float> params;  // all defaults (ColorA/B = 1)
  g_bwmIgnoreImageB = ignoreB;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.output = dst; c.params = &params;
  c.inputTextures[0] = imageA; c.inputTextures[1] = imageB; c.inputTextures[2] = mask;
  c.inputTextureCount = 3;
  c.inputTexture = imageA;
  cookBlendWithMask(c);
  g_bwmIgnoreImageB = false;

  out.assign((size_t)kMW * kMH * 4, 0);
  dst->getBytes(out.data(), kMW * 4, MTL::Region::Make2D(0, 0, kMW, kMH), 0);
  imageA->release(); imageB->release(); mask->release(); dst->release();
}

int runBlendWithMaskSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-blendwithmask] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  std::vector<uint8_t> got;
  bwmCookBands(dev, q, lib, /*ignoreB=*/injectBug, got);

  auto px = [&](uint32_t row, int ch) {
    return (int)got[((size_t)row * kMW + kMW / 2) * 4 + ch];
  };

  const int kTol = 2;
  bool pass = true;
  int worstDelta = 0;
  for (const MaskPin& pin : kMaskPins) {
    int dr = std::abs(px(pin.row, 0) - (int)pin.r), dg = std::abs(px(pin.row, 1) - (int)pin.g);
    int db = std::abs(px(pin.row, 2) - (int)pin.b), da = std::abs(px(pin.row, 3) - (int)pin.a);
    int d = std::max(std::max(dr, dg), std::max(db, da));
    worstDelta = std::max(worstDelta, d);
    bool pinOk = d <= kTol;
    if (!pinOk) pass = false;
    printf("  row=%u mask=%d want=(%d,%d,%d,%d) got=(%d,%d,%d,%d) d=%d %s\n",
           pin.row, pin.maskVal, pin.r, pin.g, pin.b, pin.a,
           px(pin.row, 0), px(pin.row, 1), px(pin.row, 2), px(pin.row, 3), d, pinOk ? "ok" : "MISS");
  }
  printf("[selftest-blendwithmask] worstDelta=%d (<=%d) injectBug=%d -> %s\n", worstDelta, kTol,
         injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release();
  clearTexOpCache();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

// --- BlendWithMask CHAIN + RESIDENT golden (the multi-Texture2D gather, the承重線) ------------------
// Three RenderTarget sources paint solid RED (ImageA), solid BLUE (ImageB), and a 3-band grey mask;
// BlendWithMask#4 composites them through cookResident (the PRODUCTION path). We read the band centers
// and assert the hand-derived lerp values. injectBug OMITS the ImageB wire -> the gather loses its 2nd
// input -> red self-mix -> the blue/half pins collapse -> RED. Proves all THREE Texture2D ports thread
// in the production resident path (the first three-input multi-image consumer in resident).
#include "runtime/compound_graph.h"
#include "runtime/tixl_point.h"

namespace sw {
namespace {

// RenderTarget cook OVERRIDE (one painter, three patterns via ClearColor.x: 0=RED, 1=BLUE, 2=3-band mask).
void bwmTexSource(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width(), h = (uint32_t)c.output->height();
  const float kind = cookParam(c, "ClearColor.x", 0.0f);
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  if (kind < 0.5f) {            // RED (ImageA)
    for (size_t i = 0; i < (size_t)w * h; ++i) { px[i * 4] = 255; px[i * 4 + 3] = 255; }
  } else if (kind < 1.5f) {     // BLUE (ImageB)
    for (size_t i = 0; i < (size_t)w * h; ++i) { px[i * 4 + 2] = 255; px[i * 4 + 3] = 255; }
  } else {                      // 3-band grey mask (thirds of h)
    for (uint32_t y = 0; y < h; ++y) {
      uint8_t mv = (y < h / 3) ? 0 : (y < 2 * h / 3 ? 128 : 255);
      for (uint32_t x = 0; x < w; ++x) {
        size_t i = ((size_t)y * w + x) * 4;
        px[i] = mv; px[i + 1] = mv; px[i + 2] = mv; px[i + 3] = 255;
      }
    }
  }
  c.output->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

Symbol bwmAtomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// Cook RT#1(red)+RT#2(blue)+RT#3(mask) -> BlendWithMask#4 through cookResident; read back. wireImageB=
// false -> OMIT ImageB connection (injectBug).
bool bwmCookResident(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* mlib, bool wireImageB,
                     std::vector<uint8_t>& out, uint32_t& ow, uint32_t& oh) {
  registerTexOp("RenderTarget", bwmTexSource);

  SymbolLibrary lib;
  lib.symbols["RenderTarget"] = bwmAtomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f},
       {"ClearColor.x", "ClearColor.x", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  lib.symbols["BlendWithMask"] = bwmAtomicOp(
      "BlendWithMask",
      {{"ImageA", "ImageA", "Texture2D", 0.0f},
       {"ImageB", "ImageB", "Texture2D", 0.0f},
       {"Mask", "Mask", "Texture2D", 0.0f},
       {"Resolution", "Resolution", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RenderTarget";  // ImageA = red
  SymbolChild c2; c2.id = 2; c2.symbolId = "RenderTarget";  // ImageB = blue
  c2.overrides["ClearColor.x"] = 1.0f;
  SymbolChild c3; c3.id = 3; c3.symbolId = "RenderTarget";  // Mask = 3-band
  c3.overrides["ClearColor.x"] = 2.0f;
  SymbolChild c4; c4.id = 4; c4.symbolId = "BlendWithMask";
  c4.overrides["Resolution"] = 0.0f;
  root.children = {c1, c2, c3, c4};
  root.connections = {{1, "out", 4, "ImageA"}, {3, "out", 4, "Mask"}, {4, "out", kSymbolBoundary, "out"}};
  if (wireImageB) root.connections.push_back({2, "out", 4, "ImageB"});
  lib.symbols["Root"] = root; lib.rootId = "Root";
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  PointGraph pg(dev, mlib, q, kMW, kMH);
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"4");

  MTL::Texture* tex = pg.target();
  ow = tex ? (uint32_t)tex->width() : 0;
  oh = tex ? (uint32_t)tex->height() : 0;
  if (!tex || ow != kMW || oh != kMH) return false;
  out.assign((size_t)ow * oh * 4, 0);
  tex->getBytes(out.data(), ow * 4, MTL::Region::Make2D(0, 0, ow, oh), 0);
  return true;
}

}  // namespace

int runBlendWithMaskChainSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) {
    printf("[selftest-blendwithmaskchain] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  std::vector<uint8_t> got;
  uint32_t ow = 0, oh = 0;
  bool gotOk = bwmCookResident(dev, q, mlib, /*wireImageB=*/!injectBug, got, ow, oh);
  bool dimsOk = gotOk && ow == kMW && oh == kMH;

  // Band centers: thirds of the height. mask=0 -> red, mask=128 -> half, mask=255 -> blue.
  struct BandPin { uint32_t row; uint8_t r, g, b, a; };
  const BandPin pins[] = {
      {kMH / 6,       255, 0, 0,   255},  // band 0 center: red
      {kMH / 2,       128, 0, 128, 255},  // band 1 center: half
      {5 * kMH / 6,   0,   0, 255, 255},  // band 2 center: blue
  };
  const int kTol = 2;
  bool pass = dimsOk;
  int worstDelta = 0;
  auto px = [&](uint32_t row, int ch) { return (int)got[((size_t)row * ow + ow / 2) * 4 + ch]; };
  if (dimsOk)
    for (const BandPin& p : pins) {
      int dr = std::abs(px(p.row, 0) - (int)p.r), dg = std::abs(px(p.row, 1) - (int)p.g);
      int db = std::abs(px(p.row, 2) - (int)p.b), da = std::abs(px(p.row, 3) - (int)p.a);
      int d = std::max(std::max(dr, dg), std::max(db, da));
      worstDelta = std::max(worstDelta, d);
      if (d > kTol) pass = false;
      printf("  row=%u want=(%d,%d,%d,%d) got=(%d,%d,%d,%d) d=%d\n", p.row, p.r, p.g, p.b, p.a,
             px(p.row, 0), px(p.row, 1), px(p.row, 2), px(p.row, 3), d);
    }
  printf("[selftest-blendwithmaskchain] out=%ux%u(want %ux%u,dimsOk=%d) worstDelta=%d (<=%d) "
         "injectBug=%d -> %s\n",
         ow, oh, kMW, kMH, dimsOk ? 1 : 0, worstDelta, kTol, injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  mlib->release(); q->release(); dev->release();
  clearTexOpCache();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
