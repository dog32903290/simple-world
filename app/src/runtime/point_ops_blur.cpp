// Blur image-filter texture op (lane I) — the FIRST image filter, opening the whole filter family
// (Texture2D in -> Texture2D out). TiXL authority: Operators/Lib/image/fx/blur/Blur.cs (signature:
// Image/Size/Samples/Offset/Opacity/Resolution/Wrap) + Blur.t3 (a 2-pass composite: horizontal
// then vertical instance of Blur.hlsl) + Assets/shaders/img/fx/Blur.hlsl (the per-pass kernel).
//
// Faithful structure: TWO passes of blur.metal (blur_vs/blur_fs). Pass 1 blurs `inputTexture`
// horizontally into a scratch texture; pass 2 blurs the scratch vertically into `output`. The two
// passes are the TiXL .t3 graph's two child Blur.hlsl instances; running them in one op (vs a
// sub-graph) is the named fork — keeps Blur a single leaf op + a single texture-flow node.
//
// Self-contained leaf: cookBlur + registerBlurOp() + runBlurSelfTest/runBlurChainSelfTest. Wired
// into the texture stream (texReg) by registerBuiltinPointOps(); the cook driver's recursive
// cookTexNode hands the upstream RenderTarget's texture in via TexCookCtx::inputTexture (the I2
// gather direct-through).
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/blur_params.h"   // BlurParams, BLUR_Params
#include "runtime/eval_context.h"  // EvaluationContext (chain selftest builds one)
#include "runtime/graph.h"         // Graph/Node/pinId
#include "runtime/graph_bridge.h"  // libFromGraph (chain selftest's resident leg)
#include "runtime/image_filter_op_registry.h"  // ImageFilterOp self-registration
#include "runtime/point_graph.h"   // TexCookCtx, cookParam, registerTexOp
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (chain selftest's resident leg)
#include "runtime/tex_op_cache.h"  // cachedTexPSO/cachedScratchTex (D2-2 PSO+scratch reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// One blur pass: sample `src` along (dirX,dirY) with the given params, render into `dst`.
void blurPass(MTL::Device* dev, MTL::CommandQueue* q, MTL::RenderPipelineState* rps,
              MTL::SamplerState* samp, MTL::Texture* src, MTL::Texture* dst, const BlurParams& p) {
  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(dst);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = q->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(src, 0);
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(BlurParams), BLUR_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
}

// Blur texture op: 2 passes (H then V). Reads c.inputTexture (the upstream tex op's output, via the
// gather direct-through), writes c.output. No input texture -> clear to black (nothing to blur).
void cookBlur(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  // No upstream texture wired: there is nothing to filter. Clear output so we don't show stale.
  if (!c.inputTexture) {
    MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
    auto* ca = pass->colorAttachments()->object(0);
    ca->setTexture(c.output);
    ca->setLoadAction(MTL::LoadActionClear);
    ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
    ca->setStoreAction(MTL::StoreActionStore);
    MTL::CommandBuffer* cmd = c.queue->commandBuffer();
    cmd->renderCommandEncoder(pass)->endEncoding();
    cmd->commit();
    cmd->waitUntilCompleted();
    return;
  }

  const uint32_t W = (uint32_t)c.output->width(), H = (uint32_t)c.output->height();
  MTL::RenderPipelineState* rps = cachedTexPSO(c.dev, c.lib, "blur_vs", "blur_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp (see blur.metal)
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // Scratch intermediate for the H pass result (same size/format as output); reused across frames.
  MTL::Texture* scratch = cachedScratchTex(c.dev, fmt, W ? W : 1, H ? H : 1, "blur.h");  // D2-2 reuse

  // TiXL params: Size/Samples/Offset/Opacity(->Glow2). widthToHeight keeps the blur circular.
  BlurParams p{};
  p.Size = cookParam(c, "Size", 1.0f);
  p.NumberOfSamples = std::max(1.0f, std::round(cookParam(c, "Samples", 8.0f)));
  p.Offset = cookParam(c, "Offset", 0.0f);
  p.Glow2 = cookParam(c, "Opacity", 1.0f);
  p.WidthToHeight = (H > 0) ? (float)W / (float)H : 1.0f;

  // Pass 1: horizontal (1,0) over the input -> scratch.
  p.DirectionX = 1.0f; p.DirectionY = 0.0f;
  blurPass(c.dev, c.queue, rps, samp, const_cast<MTL::Texture*>(c.inputTexture), scratch, p);
  // Pass 2: vertical (0,1) over scratch -> output.
  p.DirectionX = 0.0f; p.DirectionY = 1.0f;
  blurPass(c.dev, c.queue, rps, samp, scratch, c.output, p);

  samp->release();  // scratch + rps are cache-owned (tex_op_cache), not released here
}

}  // namespace

// Self-registration. NodeSpec literal moved verbatim from node_registry_image_filter.cpp.
// (blurchain golden stays in selftests.cpp kTable — the registrar carries one selftest per op.)
static const ImageFilterOp _reg_blur{
    // Blur (TiXL Lib.image.fx.blur.Blur): the FIRST image filter — Texture2D in -> Texture2D out,
    // a 2-pass directional Gaussian (point_ops_blur.cpp). Params mirror Blur.cs: Size (reach),
    // Samples (taps), Offset (added constant), Opacity (rgb intensity -> shader Glow2). Resolution
    // picks the output texture size (same enum as RenderTarget; default WindowFollow). FORK
    // (named): TiXL's Wrap (TextureAddressMode) input is omitted — the op uses a fixed clamp
    // sampler (= MirrorOnce default for blur); non-default Wrap is a follow-up.
    {"Blur", "Blur",
     {{"Image", "Image", "Texture2D", true},
      {"out", "out", "Texture2D", false},
      {"Size", "Size", "Float", true, 1.0f, 0.0f, 100.0f},
      {"Samples", "Samples", "Float", true, 8.0f, 1.0f, 10.0f},
      {"Offset", "Offset", "Float", true, 0.0f, -1.0f, 1.0f},
      {"Opacity", "Opacity", "Float", true, 1.0f, 0.0f, 4.0f},
      {"Resolution", "Resolution", "Float", true, 0.0f, 0.0f, 4.0f, Widget::Enum,
       {"WindowFollow", "HD720", "HD1080", "UHD4K", "Custom"}, true},
      {"CustomW", "CustomW", "Float", true, 512.0f, 1.0f, 8192.0f},
      {"CustomH", "CustomH", "Float", true, 512.0f, 1.0f, 8192.0f}},
     nullptr},
    "Blur", cookBlur, "blur", runBlurSelfTest};

// --- Blur MATH golden -------------------------------------------------------------------------
// Fill a source texture with a single hard 1px-wide WHITE vertical line on black, run Blur (H+V),
// and assert the line SPREADS horizontally: columns adjacent to the line become lit (a no-op /
// passthrough leaves them black). Also asserts the line center stays lit. injectBug forces Size=0
// (no reach) so neighbouring columns stay black -> the spread assertion FAILS (teeth).
int runBlurSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();  // fresh device: drop PSOs/scratch built on a now-released device
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-blur] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Source texture: white vertical line at column W/2, else black.
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);
  std::vector<uint8_t> in((size_t)W * H * 4, 0);
  const uint32_t lineCol = W / 2;
  for (uint32_t y = 0; y < H; ++y) {
    size_t idx = ((size_t)y * W + lineCol) * 4;
    in[idx] = 255; in[idx + 1] = 255; in[idx + 2] = 255; in[idx + 3] = 255;
  }
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  // Build the op ctx by hand (cookParam falls back to def when no param map). Big Size so the
  // spread is unmistakable; injectBug zeroes it via a params map override.
  std::map<std::string, float> params;
  params["Size"] = injectBug ? 0.0f : 30.0f;
  params["Samples"] = 8.0f;
  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookBlur(c);

  // Readback the center row; check center lit + a column several px off the line lit.
  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  const uint32_t midRow = H / 2;
  auto lum = [&](uint32_t x, uint32_t y) {
    size_t i = ((size_t)y * W + x) * 4;
    return (int)out[i] + out[i + 1] + out[i + 2];
  };
  int center = lum(lineCol, midRow);
  int off = lum(lineCol + 5, midRow);  // 5 px off the original line
  bool spread = off > 20;              // blurred light reached here (passthrough = 0)
  bool centerLit = center > 20;
  bool pass = centerLit && spread;
  printf("[selftest-blur] center=%d off5=%d spread=%d centerLit=%d -> %s\n",
         center, off, spread ? 1 : 0, centerLit ? 1 : 0, pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// --- Blur CHAIN golden (the gather direct-through, I2) -----------------------------------------
// Build RadialPoints -> DrawPoints(Command) -> RenderTarget(Texture2D) -> Blur(Texture2D) through
// PointGraph::cook with Blur as the terminal, and assert the displayed texture is non-empty: the
// RenderTarget's Texture2D output really threaded into the Blur's input via cookTexNode. injectBug
// omits the RenderTarget->Blur wire -> Blur gets no input texture -> black -> FAIL.
int runBlurChainSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 128, RW = 256, RH = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();  // fresh device: drop PSOs/scratch built on a now-released device
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-blurchain] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // RadialPoints + DrawPoints(cmd) + RenderTarget(tex) + Blur(tex) + ...

  PointGraph pg(dev, lib, q, 64, 64);
  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)N; gen.params["Radius"] = 2.0f; g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  Node rt; rt.id = 3; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f;  // Custom
  rt.params["CustomW"] = (float)RW; rt.params["CustomH"] = (float)RH; g.nodes.push_back(rt);
  Node bl; bl.id = 4; bl.type = "Blur";
  bl.params["Resolution"] = 4.0f; bl.params["CustomW"] = (float)RW; bl.params["CustomH"] = (float)RH;
  bl.params["Size"] = 5.0f; g.nodes.push_back(bl);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points -> DrawPoints.points
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // DrawPoints.out -> RenderTarget.command
  if (!injectBug)
    g.connections.push_back({103, pinId(3, 1), pinId(4, 0)});  // RenderTarget.out -> Blur.Image

  // defaultDrawTarget must pick the SINK tex node (Blur, id 4), not the upstream RenderTarget
  // (id 3) — else the live app shows the un-filtered image and the filter is invisible. injectBug
  // omits the RT->Blur wire, which leaves BOTH tex nodes as unconsumed sinks — defaultDrawTarget
  // then returns the first in node order (the RenderTarget, 3), so the bug trips on termOK
  // (the live symptom: the canvas shows the UN-filtered image; refuter-R-I corrected this note).
  int term = pg.defaultDrawTarget(g);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, term);  // Blur is the terminal (most-downstream tex sink)

  MTL::Texture* tex = pg.target();
  bool termOK = term == 4;  // Blur is the sink, RenderTarget feeds it
  bool sized = tex && (uint32_t)tex->width() == RW && (uint32_t)tex->height() == RH;
  int nonBlack = 0;
  if (sized) {
    std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
    tex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
    for (size_t i = 0; i < (size_t)RW * RH; ++i)
      if (px[i * 4] > 20 || px[i * 4 + 1] > 20 || px[i * 4 + 2] > 20) ++nonBlack;
  }
  bool pass = termOK && sized && nonBlack > 50;
  printf("[selftest-blurchain] term=%d(want 4) size=%lux%lu(want %ux%u) nonBlack=%d(need>50) -> %s\n",
         term, tex ? tex->width() : 0, tex ? tex->height() : 0, RW, RH, nonBlack,
         pass ? "PASS" : "FAIL");

  // --- resident leg (refuter-R-I 修1: production walks cookResident, not flat cook(); the flat
  // leg above would keep passing even if the resident gather went deaf). Same chain through the
  // ONE canonical lib path (libFromGraph -> buildEvalGraph) on a FRESH PointGraph, cooked at the
  // Blur terminal; same nonBlack assertion. injectBug reuses the missing-wire graph, where the
  // resident gather hands Blur no input texture.
  bool residentPass = false;
  {
    PointGraph rpg(dev, lib, q, 64, 64);
    SymbolLibrary slib = libFromGraph(g);
    ResidentEvalGraph rg = buildEvalGraph(slib, slib.rootId);
    EvaluationContext rctx{};
    rctx.frameIndex = 0; rctx.time = 0.0f; rctx.deltaTime = 1.0f / 60.0f;
    rpg.cookResident(rg, rctx, nullptr, "4");
    MTL::Texture* rtex = rpg.target();
    int rNonBlack = 0;
    bool rSized = rtex && (uint32_t)rtex->width() == RW && (uint32_t)rtex->height() == RH;
    if (rSized) {
      std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
      rtex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
      for (size_t i = 0; i < (size_t)RW * RH; ++i)
        if (px[i * 4] > 20 || px[i * 4 + 1] > 20 || px[i * 4 + 2] > 20) ++rNonBlack;
    }
    // Same assertion as the flat leg — under injectBug the missing wire leaves the resident
    // Blur with no input texture (cooks black), so this leg goes red on its own merit.
    residentPass = rSized && rNonBlack > 50;
    printf("[selftest-blurchain] resident nonBlack=%d (need>50) -> %s\n", rNonBlack,
           residentPass ? "PASS" : "FAIL");
  }

  bool allPass = pass && residentPass;
  lib->release(); q->release(); dev->release(); pool->release();
  return allPass ? 0 : 1;
}

}  // namespace sw
