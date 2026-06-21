// LightRaysFx image-filter texture op — TiXL stylize light-rays (radial streak / "god rays").
// Authority (ported 1:1): external/tixl Operators/Lib/image/fx/stylize/LightRaysFx.{cs,t3} +
// Operators/Lib/Assets/shaders/img/fx/LightRayFx.hlsl (`psMain` entrypoint). Driven through the
// standard single-pass tex-op idiom (one fullscreen draw, cached PSO) — NOT a FeedbackOp (feed-
// forward, no cross-frame state). Mirrors point_ops_blur.cpp's single-pass shape + point_ops_displace
// .cpp's optional-second-Texture2D-input handling.
//
// ★ BACKWARD-TRACE FINDING (查 TiXL 不發明 — reality differs from the "2-pass" scout sketch):
// LightRaysFx is SINGLE-PASS in production. A reachability sweep from the .t3 Output root (slot
// bdc413f2) shows the Output comes ONLY from ExecuteTextureUpdate 4d393e7f → Execute 87a6d72d →
// PixelShader ca5e278b = `psMain`, rendered ONCE into Texture2d ecbbe9ba. The shader's `Pass2Refine`
// entrypoint and its entire chain (PixelShader 1c9ee15f, Texture2d 273c8ffb, Executes be7d9e8c/
// e75e5433, …) are NOT reachable from Output — the author's abandoned refine attempt (.hlsl comment:
// "An ill-fated attempt of a refinement pass. Sadly it has too many artifacts to be usable."), left
// wired-but-dead. So we port psMain only. (Cut55 lesson: trace the ACTUAL output chain backward.)
//
// INPUTS: Image (t0, inputTextures[0]) + TextureFX (t1, inputTextures[1], OPTIONAL). When TextureFX is
// unwired, the shader treats FxImage as white(1,1,1,1) — byte-identical to TiXL's FirstValidTexture
// (TextureFX null → Lib:images/basic/white-pixel.png) default. PORTS/DEFAULTS from LightRaysFx.t3:
// Direction(Vec2, 0,0) Samples(int, 100) Length(0.4) RayColor(Vec4, 1,1,1,1) Decay(0.9)
// ApplyFXToBackground(1.0) Amount(5.0).
//
// R-2 (auto-route): registerTexOp("LightRaysFx", cookLightRaysFx) auto-routes through the standard
// cookTexNode branch on BOTH flat and resident — multi-Texture2D-input gather is already supported by
// the cook driver (Displace established it). No special routing.
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
#include "runtime/graph_bridge.h"
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/lightraysfx_params.h"         // LightRaysFxParams, LIGHTRAYSFX_Params
#include "runtime/point_graph.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/tex_op_cache.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// Clear `out` to black (no Image input -> nothing to streak; mirrors cookBlur/cookDisplace).
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

// LightRaysFx cook: read Image (inputTextures[0]) + optional TextureFX (inputTextures[1]); one
// fullscreen psMain pass into c.output.
void cookLightRaysFx(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  const MTL::Texture* image = c.inputTextureCount > 0 ? c.inputTextures[0] : nullptr;
  const MTL::Texture* fx = c.inputTextureCount > 1 ? c.inputTextures[1] : nullptr;
  if (!image) { clearTexture(c.queue, c.output); return; }  // no Image -> nothing to streak

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "lightraysfx_vs", "lightraysfx_fs", fmt);
  if (!rps) return;

  // Sampler: linear + Clamp U/V (TiXL SamplerState b210bba2: AddressU/V Clamp, MinMagMipLinear).
  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // Ports/defaults (LightRaysFx.t3). Direction = Vec2; Samples = int (carried as float, rounded).
  LightRaysFxParams p{};
  p.Center[0] = cookParam(c, "Direction.x", 0.0f);
  p.Center[1] = cookParam(c, "Direction.y", 0.0f);
  p.NumSamples = std::round(cookParam(c, "Samples", 100.0f));
  p.Length = cookParam(c, "Length", 0.4f);
  p.RayColor[0] = cookParam(c, "RayColor.x", 1.0f);
  p.RayColor[1] = cookParam(c, "RayColor.y", 1.0f);
  p.RayColor[2] = cookParam(c, "RayColor.z", 1.0f);
  p.RayColor[3] = cookParam(c, "RayColor.w", 1.0f);
  p.Decay = cookParam(c, "Decay", 0.9f);
  p.ApplyFx = cookParam(c, "ApplyFXToBackground", 1.0f);
  p.Amount = cookParam(c, "Amount", 5.0f);
  p.HasFx = fx ? 1.0f : 0.0f;

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(image), 0);  // texture(0) = Image
  // texture(1) = FxImage. Bind the Image as a harmless placeholder when unwired (HasFx=0 makes the
  // shader ignore it and use white) so slot 1 is never an unbound texture.
  enc->setFragmentTexture(const_cast<MTL::Texture*>(fx ? fx : image), 1);
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(LightRaysFxParams), LIGHTRAYSFX_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache)
}

}  // namespace

int runLightRaysFxSelfTest(bool injectBug);

// Self-registration (PIXEL leaf, single pass). Ports 1:1 with LightRaysFx.cs; OUTER defaults from
// LightRaysFx.t3. Resolution picks the output texture size (same enum as the other filters).
static const ImageFilterOp _reg_lightraysfx{
    {"LightRaysFx", "LightRaysFx",
     {{"Image", "Image", "Texture2D", true},
      {"TextureFX", "TextureFX", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      // LightRaysFx.t3 outer defaults.
      {"Direction.x", "Direction", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 2},
      {"Direction.y", "Direction.y", "Float", true, 0.0f, -2.0f, 2.0f, Widget::Vec, {}, true, 1},
      {"Samples", "Samples", "Float", true, 100.0f, 1.0f, 500.0f},
      {"Length", "Length", "Float", true, 0.4f, 0.0f, 2.0f},
      {"RayColor.x", "RayColor", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 4},
      {"RayColor.y", "RayColor.y", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 3},
      {"RayColor.z", "RayColor.z", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 2},
      {"RayColor.w", "RayColor.w", "Float", true, 1.0f, 0.0f, 1.0f, Widget::Vec, {}, true, 1},
      {"Decay", "Decay", "Float", true, 0.9f, 0.0f, 4.0f},
      {"ApplyFXToBackground", "ApplyFXToBackground", "Float", true, 1.0f, 0.0f, 1.0f},
      {"Amount", "Amount", "Float", true, 5.0f, 0.0f, 20.0f},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "LightRaysFx", cookLightRaysFx, "lightraysfx", runLightRaysFxSelfTest};

// --- LightRaysFx STRUCTURAL golden ------------------------------------------------------------
// psMain at a pixel P walks INWARD toward the light source (centerproof), accumulating Image samples
// along P→source. So a bright feature lying BETWEEN P and the source streaks OUTWARD: a previously-
// black pixel radially BEYOND the feature (feature between it and the source) becomes lit.
//
// Setup: Direction = (0,0) → centerproof = (0.5,0.5) = image CENTER (the light source). Image = black
// with a single bright WHITE block placed BELOW center (so it sits between the center and the bottom
// edge along the vertical radial). A test pixel FURTHER below the block (block between it and center)
// must light up — the rays streak from the center, through the block, out to that pixel.
//   (a) STREAK: a black pixel below the block, on the center→block→down radial, becomes lit.
//   (b) The same pixel is black in a no-op baseline (Amount=0 → colorSum*0, only orgColor=black).
//   (c) RESIDENT parity (R-2): flat ~= resident over the standard resident gather.
// injectBug: Amount=0 (kills the ray accumulation contribution) → no streak → the lit assertion
// FAILS (teeth). `want` stays FIXED (we assert the streak is present; the bug removes it).
static int lrfxCountLit(const std::vector<uint8_t>& px, uint32_t W, uint32_t H, int thresh) {
  int n = 0;
  for (size_t i = 0; i < (size_t)W * H; ++i)
    if ((int)px[i * 4] + px[i * 4 + 1] + px[i * 4 + 2] > thresh) ++n;
  return n;
}

int runLightRaysFxSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();  // fresh device: drop PSOs/scratch built on a now-released device
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-lightraysfx] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);

  // Image: black, with a bright WHITE block centered horizontally (cx) and placed BELOW center.
  // Center = (cx, cy) = (32, 32). Block spans rows [40,46) — below center, on the vertical radial.
  const uint32_t cx = W / 2, cy = H / 2;                 // light source pixel = (32,32)
  const uint32_t BX0 = cx - 3, BX1 = cx + 3;             // block x: [29,35)
  const uint32_t BY0 = 40, BY1 = 46;                     // block y: [40,46) (below center)
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      uint8_t v = (x >= BX0 && x < BX1 && y >= BY0 && y < BY1) ? 255 : 0;
      in[i] = v; in[i + 1] = v; in[i + 2] = v; in[i + 3] = 255;
    }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  auto lum = [](const std::vector<uint8_t>& px, uint32_t W2, uint32_t x, uint32_t y) {
    size_t i = ((size_t)y * W2 + x) * 4;
    return (int)px[i] + px[i + 1] + px[i + 2];
  };

  // Test pixel: directly below the block, OUTSIDE it (the block is between this pixel and the source).
  // y=52 is below BY1=46; x=cx on the vertical radial. Black in the input.
  const uint32_t TX = cx, TY = 52;

  // --- Main run (default params; injectBug zeroes Amount -> no streak contribution).
  std::map<std::string, float> params;
  params["Direction.x"] = 0.0f;
  params["Direction.y"] = 0.0f;
  params["Samples"] = 100.0f;
  params["Length"] = 0.4f;
  params["Decay"] = 0.9f;
  params["ApplyFXToBackground"] = 1.0f;
  params["Amount"] = injectBug ? 0.0f : 5.0f;
  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1;
  c.inputTextures[0] = src; c.inputTextureCount = 1;
  c.inputTexture = src;
  c.output = dst; c.params = &params;
  cookLightRaysFx(c);

  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  int testPix = lum(out, W, TX, TY);              // below the block, was black; rays should light it
  int blockPix = lum(out, W, cx, (BY0 + BY1) / 2);  // the block itself: stays bright
  bool streak = testPix > 15;                      // ray bled past the block to a previously-black px
  bool blockLit = blockPix > 200;                  // the bright feature remains bright

  // --- BASELINE (Amount=0): the same test pixel must be BLACK (only orgColor=black contributes).
  std::map<std::string, float> pBase = params;
  pBase["Amount"] = 0.0f;
  MTL::Texture* dst2 = dev->newTexture(td);
  TexCookCtx c2;
  c2.dev = dev; c2.lib = lib; c2.queue = q;
  c2.nodeId = 1;
  c2.inputTextures[0] = src; c2.inputTextureCount = 1; c2.inputTexture = src;
  c2.output = dst2; c2.params = &pBase;
  cookLightRaysFx(c2);
  std::vector<uint8_t> out2((size_t)W * H * 4, 0);
  dst2->getBytes(out2.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  int baseTest = lum(out2, W, TX, TY);
  bool baselineBlack = baseTest < 10;             // Amount 0 -> no rays -> test pixel stays black

  // --- RESIDENT parity leg (R-2): RadialPoints->DrawPoints->RenderTarget->LightRaysFx through both
  // the flat and resident paths; assert both lit and within a tolerant band.
  registerBuiltinPointOps();
  const uint32_t RW = 128, RH = 128;
  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = 128.0f; gen.params["Radius"] = 1.5f; g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  Node rt; rt.id = 3; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)RW; rt.params["CustomH"] = (float)RH;
  g.nodes.push_back(rt);
  Node lr; lr.id = 4; lr.type = "LightRaysFx";
  lr.params["Resolution"] = 4.0f; lr.params["CustomW"] = (float)RW; lr.params["CustomH"] = (float)RH;
  lr.params["Amount"] = injectBug ? 0.0f : 5.0f;
  lr.params["Length"] = 0.4f; lr.params["Samples"] = 100.0f; lr.params["Decay"] = 0.9f;
  g.nodes.push_back(lr);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});
  g.connections.push_back({103, pinId(3, 1), pinId(4, 0)});  // RenderTarget out -> LightRaysFx Image

  int flatNonBlack = 0, residentNonBlack = 0;
  {
    PointGraph pg(dev, lib, q, 64, 64);
    int term = pg.defaultDrawTarget(g);
    EvaluationContext ctx{}; ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
    pg.cook(g, ctx, nullptr, term);
    MTL::Texture* tex = pg.target();
    if (tex && (uint32_t)tex->width() == RW && (uint32_t)tex->height() == RH) {
      std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
      tex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
      flatNonBlack = lrfxCountLit(px, RW, RH, 20);
    }
  }
  {
    PointGraph rpg(dev, lib, q, 64, 64);
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    EvaluationContext rctx{}; rctx.frameIndex = 0; rctx.time = 0.0f; rctx.deltaTime = 1.0f / 60.0f;
    rpg.cookResident(rg, rctx, nullptr, "4");
    MTL::Texture* rtex = rpg.target();
    if (rtex && (uint32_t)rtex->width() == RW && (uint32_t)rtex->height() == RH) {
      std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
      rtex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
      residentNonBlack = lrfxCountLit(px, RW, RH, 20);
    }
  }
  bool bothLit = flatNonBlack > 50 && residentNonBlack > 50;
  int diff = std::abs(flatNonBlack - residentNonBlack);
  int maxNB = std::max(flatNonBlack, residentNonBlack);
  bool residentParity = bothLit && (diff <= maxNB / 4 + 5);  // within ~25%

  bool pass = streak && blockLit && baselineBlack && residentParity;
  printf("[selftest-lightraysfx] test=%d block=%d streak=%d blockLit=%d | baseTest=%d baselineBlack=%d "
         "| flatNB=%d residNB=%d residParity=%d -> %s\n",
         testPix, blockPix, streak ? 1 : 0, blockLit ? 1 : 0, baseTest, baselineBlack ? 1 : 0,
         flatNonBlack, residentNonBlack, residentParity ? 1 : 0, pass ? "PASS" : "FAIL");

  src->release(); dst->release(); dst2->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
