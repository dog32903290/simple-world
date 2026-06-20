// Blend image-filter texture op (lane multi-image, image/use) — the THIRD consumer of the multi-image
// seam (Displace = Image+DisplaceMap; DistortAndShade = ImageA+ImageB; this = ImageA+ImageB composite).
// TiXL authority:
//   external/tixl Operators/Lib/image/use/Blend.cs   — op ports (ImageA, ColorA, ImageB, ColorB,
//       BlendMode, AlphaMode, NormalForUpperHalf, ScaleMode, GenerateMips, Resolution) + the three
//       enums (RgbBlendModes 0..9, AlphaBlendModes 0..8, ScaleModes 0..2).
//   .../image/use/Blend.t3   — _multiImageFxSetupStatic compound + defaults. STEP-0 BACKWARD-TRACE of
//       the two shader SRV slots (the Cut-58 lesson — trace each binding back to its true source):
//         t0 (ImageA in the .hlsl) <- root op port ImageA (abaa52e9), wired to _multiImageFxSetupStatic
//             slot 55126bff (the FIRST/ImageA SRV slot).
//         t1 (ImageB in the .hlsl) <- root op port ImageB (c7c524cf), wired to _multiImageFxSetupStatic
//             slot 0bb90f8d (the SECOND/ImageB SRV slot).
//       (Both are graph-wired ROOT op ports — NOT assets, NOT dangling. The .hlsl declares ImageA @
//       register(t0), ImageB @ register(t1); the .t3 binds ImageA-first / ImageB-second -> t0/t1.)
//   .../Assets/shaders/img/fx/Blend.hlsl — the pixel shader (ported 1:1 -> blend.metal).
//
// PARAM ROUTING (STEP-0, the Cut-55 .t3 connection-order rule — BACKWARD-TRACED & verified clean 1:1,
// NO arithmetic junctions): the _multiImageFxSetupStatic FloatsToBuffer MultiInput (target slot
// 2929c4c9) is fed by 12 connections whose order EXACTLY matches the .hlsl cbuffer field order:
//   Vector4Components(ColorA).{X,Y,Z,W} -> ImageAColor.xyzw,
//   Vector4Components(ColorB).{X,Y,Z,W} -> ImageBColor.xyzw,
//   IntToFloat(BlendMode) -> ColorMode, IntToFloat(AlphaMode) -> AlphaMode,
//   BoolToFloat(NormalForUpperHalf) -> UseNormalForUpperHalf, IntToFloat(ScaleMode) -> ScaleMode.
// The Vector4Components are identity splitters; IntToFloat/BoolToFloat are pure type casts (NOT math).
// (See blend_params.h for the field<-source GUID table + the static_assert.)
//
// SEAM NOTE — the multi-image seam is ALREADY OPEN and CONSUMED. cookTexNode (flat point_graph.cpp +
// resident point_graph_resident.cpp) recurses EVERY Texture2D input port in spec order into
// TexCookCtx::inputTextures[] / inputTextureCount (Displace shipped that; DistortAndShade is the second
// consumer). A leaf with TWO Texture2D ports gets both upstream textures cooked-and-bound with ZERO
// shared-file edit. This leaf is the THIRD proof of that seam.
//
// FORK (named): an unwired ImageB -> sample ImageA (an unwired Texture2D slot is null; a null texture
// can't sample, so reusing ImageA keeps the picture visible rather than crashing — same fork class as
// Displace's unwired-DisplaceMap / DistortAndShade's unwired-ImageB).
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/blend_params.h"  // BlendParams, BLEND_Params
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

// Clear `out` to black (no ImageA -> nothing to composite; mirrors cookDisplace's empty path).
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

// injectBug hook (golden only): when set, the ImageB input is IGNORED (treated as if ImageB were
// unwired -> falls back to ImageA). A real wiring perturbation: the second image is gone, so the
// composite collapses to an ImageA-vs-ImageA blend and the pinned mixed-color pixels miss.
bool g_blendIgnoreImageB = false;

// Blend texture op: read ImageA (inputTextures[0]) + ImageB (inputTextures[1]), one fullscreen pass
// into c.output. ImageA @ texture(0), ImageB @ texture(1) (the .hlsl t0/t1).
void cookBlend(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  const MTL::Texture* imageA = c.inputTextureCount > 0 ? c.inputTextures[0] : nullptr;
  const MTL::Texture* imageB = c.inputTextureCount > 1 ? c.inputTextures[1] : nullptr;
  if (g_blendIgnoreImageB) imageB = nullptr;  // golden injectBug: drop the second image
  if (!imageA) { clearTexture(c.queue, c.output); return; }  // no ImageA -> nothing to composite
  if (!imageB) imageB = imageA;  // fork: unwired ImageB -> sample ImageA (keeps picture, no crash)

  MTL::RenderPipelineState* rps = cachedTexPSO(c.dev, c.lib, "blend_vs", "blend_fs", fmt);
  if (!rps) return;

  // Sampler: linear + ClampToEdge (TiXL _multiImageFxSetupStatic.WrapMode = "Clamp" in Blend.t3).
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL params (Blend.cs / .t3 defaults). ColorA/ColorB (1,1,1,1); BlendMode 0 (Normal RGB); AlphaMode
  // 0 (Normal); NormalForUpperHalf false; ScaleMode 0 (Stretch). Routing verified 1:1 (see header).
  BlendParams p{};
  p.ImageAColorR = cookParam(c, "ColorA.x", 1.0f);
  p.ImageAColorG = cookParam(c, "ColorA.y", 1.0f);
  p.ImageAColorB = cookParam(c, "ColorA.z", 1.0f);
  p.ImageAColorA = cookParam(c, "ColorA.w", 1.0f);
  p.ImageBColorR = cookParam(c, "ColorB.x", 1.0f);
  p.ImageBColorG = cookParam(c, "ColorB.y", 1.0f);
  p.ImageBColorB = cookParam(c, "ColorB.z", 1.0f);
  p.ImageBColorA = cookParam(c, "ColorB.w", 1.0f);
  p.ColorMode = std::round(cookParam(c, "BlendMode", 0.0f));   // RgbBlendModes int carried as float
  p.AlphaMode = std::round(cookParam(c, "AlphaMode", 0.0f));   // AlphaBlendModes int
  p.UseNormalForUpperHalf = cookParam(c, "NormalForUpperHalf", 0.0f) > 0.5f ? 1.0f : 0.0f;
  p.ScaleMode = std::round(cookParam(c, "ScaleMode", 0.0f));   // ScaleModes int

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
  enc->setFragmentBytes(&p, sizeof(BlendParams), BLEND_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

int runBlendSelfTest(bool injectBug);
int runBlendChainSelfTest(bool injectBug);

// Self-registration. Ports 1:1 from Blend.cs: ImageA + ImageB (two Texture2D inputs) +
// ColorA/ColorB(Vec4) + BlendMode/AlphaMode/ScaleMode(enum) + NormalForUpperHalf(bool). Defaults
// verbatim from Blend.t3.
// FORKS (named): TiXL's GenerateMips host plumbing omitted (no mips, Blur/Displace fork class); an
// unwired ImageB samples ImageA. The Resolution port is an Int2 in TiXL (custom size); we expose it as
// the same Resolution enum the other image filters use (WindowFollow/HD720/HD1080/UHD4K/Custom) — a
// NAMED fork: TiXL's Int2 Resolution (0,0 = follow input) becomes our enum, matching every sibling
// image filter (Blur/Displace/DistortAndShade) rather than introducing an Int2 port type. NodeSpec UI
// ranges are hints only.
static const ImageFilterOp _reg_blend{
    {"Blend", "Blend",
     {{"ImageA", "ImageA", "Texture2D", true},
      {"ImageB", "ImageB", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      {"ColorA.x", "ColorA", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ColorA.y", "ColorA.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"ColorA.z", "ColorA.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"ColorA.w", "ColorA.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"ColorB.x", "ColorB", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"ColorB.y", "ColorB.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"ColorB.z", "ColorB.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"ColorB.w", "ColorB.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"BlendMode", "BlendMode", "Float", true, 0.0f, 0.0f, 9.0f, Widget::Enum,
       {"Normal", "Screen", "Multiply", "Overlay", "Difference", "UseImageA_RGB", "UseImageB_RGB",
        "Max", "Sub", "MixUsingImageB_A"}, true},
      {"AlphaMode", "AlphaMode", "Float", true, 0.0f, 0.0f, 8.0f, Widget::Enum,
       {"Normal", "Multiply", "SetToOne", "UseImageA_Alpha", "UseImageB_Alpha", "UseImageA_Brightness",
        "UseImageB_Brightness", "Additive", "Max"}, true},
      {"NormalForUpperHalf", "NormalForUpperHalf", "Float", true, 0.0f, 0.0f, 1.0f, Widget::Bool, {}, true},
      {"ScaleMode", "ScaleMode", "Float", true, 0.0f, 0.0f, 2.0f, Widget::Enum,
       {"Stretch", "Fit", "Cover"}, true},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "Blend", cookBlend, "blend", runBlendSelfTest};

// --- Blend CLOSED-FORM MATH golden (hand-derived from the blend formulas) ---------------------------
// Two SOLID-COLOR images (d=0 saturated plateau everywhere — Cut62-63 rule, no gradient/edge/fwidth
// ambiguity): ImageA = solid RED (1,0,0,1), ImageB = solid GREEN (0,1,0, alpha=0.5). ColorA=ColorB=
// (1,1,1,1), ScaleMode=0 (Stretch -> uvB=uv, no aspect issue, both 64x64 so aspect=1 anyway). So at
// EVERY pixel: tA=(1,0,0,1), tB=(0,1,0,0.5). We pin three RGB modes (AlphaMode 0 throughout):
//   Normal (BlendMode 0): rgb = (1-tB.a)*tA.rgb + tB.a*tB.rgb = 0.5*(1,0,0)+0.5*(0,1,0)=(0.5,0.5,0).
//     alpha = tA.a+tB.a-tA.a*tB.a = 1+0.5-0.5 = 1.0.  -> (128, 128, 0, 255).
//   Screen (BlendMode 1): rgb = tA.rgb + tB.rgb*tB.a = (1,0,0)+(0,0.5,0) = (1,0.5,0). -> (255,128,0).
//   Multiply (BlendMode 2): rgb = lerp(tA.rgb, tA.rgb*tB.rgb, tB.a) = lerp((1,0,0),(0,0,0),0.5)
//     = (0.5,0,0). -> (128,0,0).
// All hand-computed from the .hlsl, GROUND TRUTH. 0.5 in 8-bit unorm = round(0.5*255)=128 (±2 tol for
// sampler/round). injectBug drops ImageB -> ImageB becomes ImageA(red) -> e.g. Normal mode gives
// 0.5*(1,0,0)+0.5*(1,0,0)=(1,0,0)=(255,0,0): the G channel collapses 128->0, well past ±2 -> RED.
constexpr uint32_t kBW = 64, kBH = 64;

struct BlendPin { float blendMode; uint8_t r, g, b, a; };  // expected RGBA at any pixel (solid)
constexpr BlendPin kBlendPins[] = {
    {0.0f, 128, 128, 0, 255},  // Normal
    {1.0f, 255, 128, 0, 255},  // Screen
    {2.0f, 128, 0,   0, 255},  // Multiply
};

// Cook Blend on solid RED (ImageA) + solid GREEN-alpha0.5 (ImageB) at the given BlendMode; read back
// the center pixel. ignoreB -> injectBug path (ImageB dropped -> red self-blend).
static void blendCookSolid(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* lib, float blendMode,
                           bool ignoreB, uint8_t out[4]) {
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, kBW, kBH, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* imageA = dev->newTexture(td);
  MTL::Texture* imageB = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // ImageA: solid RED (1,0,0,1).
  std::vector<uint8_t> a((size_t)kBW * kBH * 4, 0);
  for (size_t i = 0; i < (size_t)kBW * kBH; ++i) { a[i * 4] = 255; a[i * 4 + 3] = 255; }
  imageA->replaceRegion(MTL::Region::Make2D(0, 0, kBW, kBH), 0, a.data(), kBW * 4);

  // ImageB: solid GREEN (0,1,0) with alpha 0.5 (128).
  std::vector<uint8_t> b((size_t)kBW * kBH * 4, 0);
  for (size_t i = 0; i < (size_t)kBW * kBH; ++i) { b[i * 4 + 1] = 255; b[i * 4 + 3] = 128; }
  imageB->replaceRegion(MTL::Region::Make2D(0, 0, kBW, kBH), 0, b.data(), kBW * 4);

  std::map<std::string, float> params;
  params["BlendMode"] = blendMode;
  params["AlphaMode"] = 0.0f;
  params["ScaleMode"] = 0.0f;  // Stretch -> uvB=uv (both square, aspect 1)
  g_blendIgnoreImageB = ignoreB;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.output = dst; c.params = &params;
  c.inputTextures[0] = imageA; c.inputTextures[1] = imageB; c.inputTextureCount = 2;
  c.inputTexture = imageA;
  cookBlend(c);
  g_blendIgnoreImageB = false;

  std::vector<uint8_t> px((size_t)kBW * kBH * 4, 0);
  dst->getBytes(px.data(), kBW * 4, MTL::Region::Make2D(0, 0, kBW, kBH), 0);
  size_t mid = ((size_t)(kBH / 2) * kBW + kBW / 2) * 4;
  out[0] = px[mid]; out[1] = px[mid + 1]; out[2] = px[mid + 2]; out[3] = px[mid + 3];

  imageA->release(); imageB->release(); dst->release();
}

int runBlendSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-blend] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  const int kTol = 2;
  bool pass = true;
  int worstDelta = 0;
  for (const BlendPin& pin : kBlendPins) {
    uint8_t got[4];
    blendCookSolid(dev, q, lib, pin.blendMode, /*ignoreB=*/injectBug, got);
    int dr = std::abs((int)got[0] - (int)pin.r), dg = std::abs((int)got[1] - (int)pin.g);
    int db = std::abs((int)got[2] - (int)pin.b), da = std::abs((int)got[3] - (int)pin.a);
    int d = std::max(std::max(dr, dg), std::max(db, da));
    worstDelta = std::max(worstDelta, d);
    bool pinOk = d <= kTol;
    if (!pinOk) pass = false;
    printf("  blendMode=%d want=(%d,%d,%d,%d) got=(%d,%d,%d,%d) d=%d %s\n",
           (int)pin.blendMode, pin.r, pin.g, pin.b, pin.a, got[0], got[1], got[2], got[3], d,
           pinOk ? "ok" : "MISS");
  }
  printf("[selftest-blend] worstDelta=%d (<=%d) injectBug=%d -> %s\n", worstDelta, kTol,
         injectBug ? 1 : 0, pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release();
  clearTexOpCache();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw

// --- Blend CHAIN + RESIDENT golden (the multi-Texture2D gather, the承重線) --------------------------
// Mirrors the DistortAndShade resident golden (resident_distortandshade_selftest.cpp) but lives in the
// leaf (Displace's runDisplaceChainSelfTest precedent: chain+resident in one fn). Two RenderTarget
// sources paint solid RED (ImageA) and solid GREEN-alpha0.5 (ImageB); Blend#3 composites them through
// cookResident (the PRODUCTION path frame_cook.cpp drives). Normal mode -> center pixel (128,128,0,255)
// (the hand-derived flat-golden value). Asserts: terminal sized + the composite is the MIXED color
// (G channel lit -> ImageB really threaded). injectBug OMITS the ImageB wire -> the gather loses its 2nd
// input -> Blend falls back to ImageA(red) -> G collapses to 0 -> RED.
#include "runtime/compound_graph.h"
#include "runtime/tixl_point.h"

namespace sw {
namespace {

// RenderTarget cook OVERRIDE (one painter, two patterns) — same trick as the DistortAndShade resident
// golden. findSpec only resolves REGISTERED types, so the sources must be a real registered type
// ("RenderTarget"); we override its cook with a painter that picks the pattern from ClearColor.x:
// 0 -> solid RED (ImageA), 1 -> solid GREEN with alpha 0.5 (ImageB).
void blendTexSource(TexCookCtx& c) {
  if (!c.output) return;
  const uint32_t w = (uint32_t)c.output->width(), h = (uint32_t)c.output->height();
  const bool wantGreen = cookParam(c, "ClearColor.x", 0.0f) > 0.5f;
  std::vector<uint8_t> px((size_t)w * h * 4, 0);
  for (size_t i = 0; i < (size_t)w * h; ++i) {
    if (wantGreen) { px[i * 4 + 1] = 255; px[i * 4 + 3] = 128; }  // GREEN, alpha 0.5
    else           { px[i * 4 + 0] = 255; px[i * 4 + 3] = 255; }  // RED, alpha 1
  }
  c.output->replaceRegion(MTL::Region::Make2D(0, 0, w, h), 0, px.data(), w * 4);
}

Symbol blendAtomicOp(const char* id, std::vector<SlotDef> ins, std::vector<SlotDef> outs) {
  Symbol s; s.id = id; s.name = id; s.atomic = true;
  s.inputDefs = std::move(ins); s.outputDefs = std::move(outs);
  return s;
}

// Cook RenderTarget#1(red) + RenderTarget#2(green) -> Blend#3 through cookResident; read back the center
// pixel. wireImageB=false -> OMIT the ImageB connection (injectBug: multi-image gather loses 2nd input).
bool blendCookResidentMid(MTL::Device* dev, MTL::CommandQueue* q, MTL::Library* mlib, bool wireImageB,
                          uint8_t out[4], uint32_t& ow, uint32_t& oh) {
  registerTexOp("RenderTarget", blendTexSource);

  SymbolLibrary lib;
  lib.symbols["RenderTarget"] = blendAtomicOp(
      "RenderTarget",
      {{"command", "command", "Command", 0.0f}, {"Resolution", "Resolution", "Float", 0.0f},
       {"CustomW", "CustomW", "Float", 512.0f}, {"CustomH", "CustomH", "Float", 512.0f},
       {"ClearColor.x", "ClearColor.x", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  lib.symbols["Blend"] = blendAtomicOp(
      "Blend",
      {{"ImageA", "ImageA", "Texture2D", 0.0f},
       {"ImageB", "ImageB", "Texture2D", 0.0f},
       {"BlendMode", "BlendMode", "Float", 0.0f},
       {"AlphaMode", "AlphaMode", "Float", 0.0f},
       {"ScaleMode", "ScaleMode", "Float", 0.0f},
       {"Resolution", "Resolution", "Float", 0.0f}},
      {{"out", "out", "Texture2D", 0.0f}});
  Symbol root; root.id = "Root"; root.name = "Root"; root.atomic = false;
  root.outputDefs = {{"out", "out", "Texture2D", 0.0f}};
  SymbolChild c1; c1.id = 1; c1.symbolId = "RenderTarget";  // ImageA = red (ClearColor.x=0 default)
  SymbolChild c2; c2.id = 2; c2.symbolId = "RenderTarget";  // ImageB = green alpha0.5
  c2.overrides["ClearColor.x"] = 1.0f;
  SymbolChild c3; c3.id = 3; c3.symbolId = "Blend";
  c3.overrides["BlendMode"] = 0.0f;  // Normal
  c3.overrides["AlphaMode"] = 0.0f;
  c3.overrides["ScaleMode"] = 0.0f;
  c3.overrides["Resolution"] = 0.0f;
  root.children = {c1, c2, c3};
  root.connections = {{1, "out", 3, "ImageA"}, {3, "out", kSymbolBoundary, "out"}};
  if (wireImageB) root.connections.push_back({2, "out", 3, "ImageB"});
  lib.symbols["Root"] = root; lib.rootId = "Root";
  ResidentEvalGraph rg = buildEvalGraph(lib, "Root");

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;

  PointGraph pg(dev, mlib, q, kBW, kBH);
  pg.cookResident(rg, ctx, /*reg=*/nullptr, /*targetPath=*/"3");

  MTL::Texture* tex = pg.target();
  ow = tex ? (uint32_t)tex->width() : 0;
  oh = tex ? (uint32_t)tex->height() : 0;
  if (!tex || ow != kBW || oh != kBH) return false;
  std::vector<uint8_t> px((size_t)ow * oh * 4, 0);
  tex->getBytes(px.data(), ow * 4, MTL::Region::Make2D(0, 0, ow, oh), 0);
  size_t mid = ((size_t)(oh / 2) * ow + ow / 2) * 4;
  out[0] = px[mid]; out[1] = px[mid + 1]; out[2] = px[mid + 2]; out[3] = px[mid + 3];
  return true;
}

}  // namespace

int runBlendChainSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* mlib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!mlib) {
    printf("[selftest-blendchain] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Production resident path (cookResident -> cookTexNode -> blend leaf -> displayTex -> target()).
  uint8_t got[4] = {0, 0, 0, 0};
  uint32_t ow = 0, oh = 0;
  bool gotOk = blendCookResidentMid(dev, q, mlib, /*wireImageB=*/!injectBug, got, ow, oh);
  bool dimsOk = gotOk && ow == kBW && oh == kBH;

  // Normal-mode hand-derived center pixel: (128,128,0,255). The G channel (128) is the load-bearing
  // proof ImageB threaded; injectBug drops ImageB -> red self-blend -> G=0.
  const int kTol = 2;
  int dr = dimsOk ? std::abs((int)got[0] - 128) : 999;
  int dg = dimsOk ? std::abs((int)got[1] - 128) : 999;
  int db = dimsOk ? std::abs((int)got[2] - 0) : 999;
  int da = dimsOk ? std::abs((int)got[3] - 255) : 999;
  bool matchPin = dimsOk && dr <= kTol && dg <= kTol && db <= kTol && da <= kTol;
  bool pass = dimsOk && matchPin;
  printf("[selftest-blendchain] out=%ux%u(want %ux%u,dimsOk=%d) center got=(%d,%d,%d,%d) "
         "want=(128,128,0,255) injectBug=%d -> %s\n",
         ow, oh, kBW, kBH, dimsOk ? 1 : 0, got[0], got[1], got[2], got[3], injectBug ? 1 : 0,
         pass ? "PASS" : "FAIL");

  mlib->release(); q->release(); dev->release();
  clearTexOpCache();
  pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
