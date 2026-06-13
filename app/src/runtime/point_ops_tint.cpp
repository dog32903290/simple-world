// Tint image-filter texture op (lane F3-1) — filter wave 3, op 1.
// TiXL authority: Operators/Lib/image/color/Tint.cs (Image/Amount/MapBlackTo/MapWhiteTo/
// Exposure/ChannelWeights/GainAndBias inputs) + Tint.t3 (defaults) + Assets/shaders/img/fx/
// Tint.hlsl (the single-pass kernel: luminance via ChannelWeights dot, GainAndBias remap,
// lerp black->white, mix with Amount, clamp alpha).
//
// Single-pass port: cookTint reads c.inputTexture (the upstream RenderTarget's Texture2D via
// the I1 gather direct-through), runs one fullscreen pass of tint_vs/tint_fs, writes c.output.
// Vec4 inputs (MapBlackTo, MapWhiteTo, ChannelWeights) are decomposed via four cookParam calls
// each (x/y/z/w components stored as separate float params, matching the NodeSpec Vec ports).
//
// Self-contained leaf: cookTint + registerTintOp() + runTintSelfTest/runTintChainSelfTest.
// Shares the D2-2 PSO+scratch cache seam (tex_op_cache.h) with Blur/Displace.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/tint_params.h"    // TintParams, TINT_Params
#include "runtime/eval_context.h"   // EvaluationContext (chain selftest builds one)
#include "runtime/graph.h"          // Graph/Node/pinId
#include "runtime/graph_bridge.h"   // libFromGraph (chain selftest's resident leg)
#include "runtime/point_graph.h"    // TexCookCtx, cookParam, registerTexOp
#include "runtime/resident_eval_graph.h"  // buildEvalGraph (chain selftest's resident leg)
#include "runtime/tex_op_cache.h"   // cachedTexPSO (D2-2 PSO reuse)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// Tint texture op: single pass. Reads c.inputTexture (upstream tex op's output), writes c.output.
// No upstream texture wired: clear output to black (nothing to tint).
void cookTint(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

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

  MTL::RenderPipelineState* rps = cachedTexPSO(c.dev, c.lib, "tint_vs", "tint_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp (see tint.metal)
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL params: Tint.cs defaults. Vec4 components read as x/y/z/w scalar ports.
  TintParams p{};
  // MapBlackTo (TiXL default ~(0,0,0,1) — near-zero to avoid pure black in the lerp numerically)
  p.MapBlackR = cookParam(c, "MapBlackTo.r", 1e-6f);
  p.MapBlackG = cookParam(c, "MapBlackTo.g", 1e-6f);
  p.MapBlackB = cookParam(c, "MapBlackTo.b", 1e-6f);
  p.MapBlackA = cookParam(c, "MapBlackTo.a", 1.0f);
  // MapWhiteTo (TiXL default (1,1,1,1))
  p.MapWhiteR = cookParam(c, "MapWhiteTo.r", 1.0f);
  p.MapWhiteG = cookParam(c, "MapWhiteTo.g", 1.0f);
  p.MapWhiteB = cookParam(c, "MapWhiteTo.b", 1.0f);
  p.MapWhiteA = cookParam(c, "MapWhiteTo.a", 1.0f);
  // ChannelWeights (TiXL default (1,1,1,0))
  p.ChannelR = cookParam(c, "ChannelWeights.r", 1.0f);
  p.ChannelG = cookParam(c, "ChannelWeights.g", 1.0f);
  p.ChannelB = cookParam(c, "ChannelWeights.b", 1.0f);
  p.ChannelA = cookParam(c, "ChannelWeights.a", 0.0f);
  // Scalars
  p.Amount  = cookParam(c, "Amount", 1.0f);
  p.GainX   = cookParam(c, "GainAndBias.x", 0.5f);
  p.GainY   = cookParam(c, "GainAndBias.y", 0.5f);
  p.Exposure = cookParam(c, "Exposure", 1.0f);

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  ca->setClearColor(MTL::ClearColor::Make(0.0, 0.0, 0.0, 1.0));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  enc->setRenderPipelineState(rps);
  enc->setFragmentTexture(const_cast<MTL::Texture*>(c.inputTexture), 0);
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(TintParams), TINT_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

void registerTintOp() { registerTexOp("Tint", cookTint); }

// --- Tint MATH golden -------------------------------------------------------------------------
// Fill a source texture with a solid mid-grey (128,128,128,255). Run Tint with Amount=1.0,
// MapBlackTo=(0,0,0,1), MapWhiteTo=(1,0,0,1) (red), ChannelWeights=(1,1,1,0) default,
// Exposure=1.0, GainAndBias=(0.5,0.5) (identity). With a mid-grey input the luminance t ~0.5;
// the mapped color is lerp(black,red,0.5) ≈ (128,0,0). The output red channel > 64 and the
// green channel < 16 — the tint pushed color toward red.
// injectBug forces Amount=0.0 (no blend) so the output is the original grey (r≈g≈b) and the
// red>64/green<16 assertion FAILS (teeth).
int runTintSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-tint] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  // Source texture: solid mid-grey (all channels = 128).
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* src = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);
  std::vector<uint8_t> in((size_t)W * H * 4, 128);
  for (size_t i = 3; i < (size_t)W * H * 4; i += 4) in[i] = 255;  // alpha = 255
  src->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, in.data(), W * 4);

  std::map<std::string, float> params;
  params["Amount"]       = injectBug ? 0.0f : 1.0f;  // bug: no tint (passthrough)
  params["MapBlackTo.r"] = 0.0f; params["MapBlackTo.g"] = 0.0f;
  params["MapBlackTo.b"] = 0.0f; params["MapBlackTo.a"] = 1.0f;
  params["MapWhiteTo.r"] = 1.0f; params["MapWhiteTo.g"] = 0.0f;  // red ramp
  params["MapWhiteTo.b"] = 0.0f; params["MapWhiteTo.a"] = 1.0f;
  params["ChannelWeights.r"] = 1.0f; params["ChannelWeights.g"] = 1.0f;
  params["ChannelWeights.b"] = 1.0f; params["ChannelWeights.a"] = 0.0f;
  params["Exposure"] = 1.0f;
  params["GainAndBias.x"] = 0.5f; params["GainAndBias.y"] = 0.5f;

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.inputTexture = src; c.output = dst; c.params = &params;
  cookTint(c);

  // Readback: center pixel should have red pushed up, green pulled down vs grey.
  std::vector<uint8_t> out((size_t)W * H * 4, 0);
  dst->getBytes(out.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  const uint32_t cx = W / 2, cy = H / 2;
  size_t i = ((size_t)cy * W + cx) * 4;
  int R = out[i], G = out[i + 1];
  // Tinted grey -> red: R should be pushed up from 128, G down toward 0.
  bool redUp  = R > 64;
  bool greenDown = G < 96;  // injectBug (passthrough): G stays ~128
  bool pass = redUp && greenDown;
  printf("[selftest-tint] center R=%d G=%d redUp=%d greenDown=%d -> %s\n",
         R, G, redUp ? 1 : 0, greenDown ? 1 : 0, pass ? "PASS" : "FAIL");

  src->release(); dst->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// --- Tint CHAIN golden (the gather direct-through, I1 single-input) ---------------------------
// Build RadialPoints -> DrawPoints(Command) -> RenderTarget(Texture2D) -> Tint(Texture2D) through
// PointGraph::cook with Tint as the terminal, and assert the displayed texture is non-empty: the
// RenderTarget's Texture2D output really threaded into Tint's input via cookTexNode.
// injectBug omits the RenderTarget->Tint wire -> Tint gets no input texture -> black -> FAIL.
// Also proves the resident cook path (same chain through libFromGraph/buildEvalGraph).
int runTintChainSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 128, RW = 256, RH = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-tintchain] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // RadialPoints + DrawPoints(cmd) + RenderTarget(tex) + Tint(tex) + ...

  PointGraph pg(dev, lib, q, 64, 64);
  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)N; gen.params["Radius"] = 2.0f; g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  Node rt; rt.id = 3; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f;  // Custom
  rt.params["CustomW"] = (float)RW; rt.params["CustomH"] = (float)RH; g.nodes.push_back(rt);
  Node tn; tn.id = 4; tn.type = "Tint";
  tn.params["Resolution"] = 4.0f; tn.params["CustomW"] = (float)RW; tn.params["CustomH"] = (float)RH;
  tn.params["Amount"] = 1.0f;  // full tint so visible
  g.nodes.push_back(tn);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points -> DrawPoints.points
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // DrawPoints.out -> RenderTarget.command
  if (!injectBug)
    g.connections.push_back({103, pinId(3, 1), pinId(4, 0)});  // RenderTarget.out -> Tint.Image

  int term = pg.defaultDrawTarget(g);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, term);

  MTL::Texture* tex = pg.target();
  bool termOK = term == 4;  // Tint is the sink
  bool sized = tex && (uint32_t)tex->width() == RW && (uint32_t)tex->height() == RH;
  int nonBlack = 0;
  if (sized) {
    std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
    tex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
    for (size_t i = 0; i < (size_t)RW * RH; ++i)
      if (px[i * 4] > 20 || px[i * 4 + 1] > 20 || px[i * 4 + 2] > 20) ++nonBlack;
  }
  bool pass = termOK && sized && nonBlack > 50;
  printf("[selftest-tintchain] term=%d(want 4) size=%lux%lu(want %ux%u) nonBlack=%d(need>50) -> %s\n",
         term, tex ? tex->width() : 0, tex ? tex->height() : 0, RW, RH, nonBlack,
         pass ? "PASS" : "FAIL");

  // --- resident leg (production walks cookResident, not flat cook(); same chain through the ONE
  // canonical lib path on a FRESH PointGraph, Tint#4 terminal). injectBug reuses the missing-wire
  // graph -> resident Tint gets no input texture -> black -> red on its own merit.
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
    residentPass = rSized && rNonBlack > 50;
    printf("[selftest-tintchain] resident nonBlack=%d (need>50) -> %s\n", rNonBlack,
           residentPass ? "PASS" : "FAIL");
  }

  bool allPass = pass && residentPass;
  lib->release(); q->release(); dev->release(); pool->release();
  return allPass ? 0 : 1;
}

}  // namespace sw
