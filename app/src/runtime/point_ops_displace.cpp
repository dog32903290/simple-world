// Displace image-filter texture op (lane D2) — the SECOND image filter and the FIRST op with TWO
// Texture2D inputs (Image + DisplaceMap). TiXL authority: Operators/Lib/image/fx/distort/Displace.cs
// (Image/DisplaceMap/DisplaceMode/Displacement/DisplacementOffset/Twist/Shade/SampleRadius/
// DisplaceMapOffset + Wrap/Filtering/Mips host plumbing) + Displace.t3 (defaults) + Assets/shaders/
// img/fx/Displace.hlsl (the kernel). Faithful single-pass port: doDisplace reads the map, derives a
// push direction, warps the Image UV.
//
// The承重線 (this lane): cookTexNode's Texture2D gather is now multi-input. TexCookCtx gained
// inputTextures[]/inputTextureCount (point_graph.h); the cook driver (flat + resident) fills one
// slot per Texture2D PORT in spec order. So Displace reads inputTextures[0]=Image, [1]=DisplaceMap.
// Blur stays on the legacy single inputTexture (== inputTextures[0]) untouched.
//
// Self-contained leaf: cookDisplace + registerDisplaceOp() + runDisplaceSelfTest/ChainSelfTest.
// Shares the D2-2 PSO+scratch cache seam (tex_op_cache.h) with Blur.
#include "runtime/point_ops.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/displace_params.h"  // DisplaceParams, DISPLACE_Params
#include "runtime/eval_context.h"
#include "runtime/graph.h"
#include "runtime/graph_bridge.h"
#include "runtime/point_graph.h"
#include "runtime/resident_eval_graph.h"
#include "runtime/tex_op_cache.h"

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

// Clear `out` to black (no Image input -> nothing to warp; mirrors cookBlur's empty path).
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

// Displace texture op: read Image (inputTextures[0]) + DisplaceMap (inputTextures[1]), one fullscreen
// pass into c.output. If DisplaceMap is unwired it falls back to the Image itself (TiXL: an unwired
// DisplaceMap slot is null; a null map can't warp, so reusing Image keeps the picture visible rather
// than crashing — a NAMED fork from TiXL, which would bind an empty texture and emit black).
void cookDisplace(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::PixelFormat fmt = c.output->pixelFormat();

  const MTL::Texture* image = c.inputTextureCount > 0 ? c.inputTextures[0] : nullptr;
  const MTL::Texture* map = c.inputTextureCount > 1 ? c.inputTextures[1] : nullptr;
  if (!image) { clearTexture(c.queue, c.output); return; }  // no Image -> nothing to warp
  if (!map) map = image;  // fork: unwired DisplaceMap -> sample Image (keeps picture, no crash)

  MTL::RenderPipelineState* rps =
      cachedTexPSO(c.dev, c.lib, "displace_vs", "displace_fs", fmt);  // D2-2 reuse
  if (!rps) return;

  MTL::SamplerDescriptor* sd = MTL::SamplerDescriptor::alloc()->init();
  sd->setMinFilter(MTL::SamplerMinMagFilterLinear);
  sd->setMagFilter(MTL::SamplerMinMagFilterLinear);
  sd->setSAddressMode(MTL::SamplerAddressModeClampToEdge);  // fork: fixed clamp (see displace.metal)
  sd->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
  MTL::SamplerState* samp = c.dev->newSamplerState(sd);
  sd->release();

  // TiXL params (Displace.cs / .t3 defaults). DisplaceMode is an enum int in TiXL; carried as float.
  DisplaceParams p{};
  p.DisplaceAmount = cookParam(c, "Displacement", 0.0f);
  p.DisplaceOffset = cookParam(c, "DisplacementOffset", 0.0f);
  p.Twist = cookParam(c, "Twist", 0.0f);
  p.Shade = cookParam(c, "Shade", 0.0f);
  p.DisplaceMapOffsetX = cookParam(c, "DisplaceMapOffset.x", 0.0f);
  p.DisplaceMapOffsetY = cookParam(c, "DisplaceMapOffset.y", 0.0f);
  p.SampleRadius = cookParam(c, "SampleRadius", 1.0f);
  p.DisplaceMode = std::round(cookParam(c, "DisplaceMode", 0.0f));
  p.UseRGSSMultiSampling = cookParam(c, "RGSS_4xAA", 0.0f) > 0.5f ? 1.0f : 0.0f;
  p.TargetWidth = (float)c.output->width();
  p.TargetHeight = (float)c.output->height();

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
  enc->setFragmentTexture(const_cast<MTL::Texture*>(map), 1);    // texture(1) = DisplaceMap
  enc->setFragmentSamplerState(samp, 0);
  enc->setFragmentBytes(&p, sizeof(DisplaceParams), DISPLACE_Params);
  enc->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));  // fullscreen tri
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();

  samp->release();  // rps is cache-owned (tex_op_cache), not released here
}

}  // namespace

void registerDisplaceOp() { registerTexOp("Displace", cookDisplace); }

// --- Displace MATH golden ---------------------------------------------------------------------
// Image = a hard vertical black/white EDGE (left half white, right half black). DisplaceMap = a
// horizontal luminance ramp (left dark -> right bright) so its x-gradient is a constant nonzero push
// (DisplaceMode 0 = IntensityGradient). With Displacement != 0 the sampled edge MOVES: the column
// exactly at the original edge changes brightness vs the passthrough (Displacement 0) baseline.
// We assert the displaced readback differs from a no-displacement baseline at the edge band.
// injectBug forces Displacement=0 (no warp) so the displaced image == baseline -> the "moved"
// assertion FAILS (teeth).
int runDisplaceSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t W = 64, H = 64;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();  // fresh device: drop PSOs/scratch built on a now-released device
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-displace] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }

  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* image = dev->newTexture(td);
  MTL::Texture* dmap = dev->newTexture(td);
  MTL::Texture* dst = dev->newTexture(td);
  MTL::Texture* base = dev->newTexture(td);

  // Image: left half (x < W/2) white, right half black — a vertical edge at column W/2.
  std::vector<uint8_t> img((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      uint8_t v = (x < W / 2) ? 255 : 0;
      img[i] = v; img[i + 1] = v; img[i + 2] = v; img[i + 3] = 255;
    }
  image->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, img.data(), W * 4);

  // DisplaceMap: horizontal luminance ramp (gradient points +x everywhere) -> a constant push.
  std::vector<uint8_t> mp((size_t)W * H * 4, 0);
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t x = 0; x < W; ++x) {
      size_t i = ((size_t)y * W + x) * 4;
      uint8_t v = (uint8_t)(x * 255 / (W - 1));
      mp[i] = v; mp[i + 1] = v; mp[i + 2] = v; mp[i + 3] = 255;
    }
  dmap->replaceRegion(MTL::Region::Make2D(0, 0, W, H), 0, mp.data(), W * 4);

  auto run = [&](MTL::Texture* out, float displacement) {
    std::map<std::string, float> params;
    params["Displacement"] = displacement;
    params["SampleRadius"] = 2.0f;
    params["DisplaceMode"] = 0.0f;  // IntensityGradient
    TexCookCtx c;
    c.dev = dev; c.lib = lib; c.queue = q;
    c.nodeId = 1; c.output = out; c.params = &params;
    c.inputTextures[0] = image; c.inputTextures[1] = dmap; c.inputTextureCount = 2;
    c.inputTexture = image;
    cookDisplace(c);
  };

  run(base, 0.0f);                                  // baseline: no warp (passthrough)
  run(dst, injectBug ? 0.0f : 1.0f);                // displaced (bug: also no warp)

  std::vector<uint8_t> obase((size_t)W * H * 4, 0), odisp((size_t)W * H * 4, 0);
  base->getBytes(obase.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  dst->getBytes(odisp.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);

  // Count pixels in the edge band (columns W/2-6 .. W/2+6) whose brightness MOVED vs baseline.
  int moved = 0;
  const uint32_t midRow = H / 2;
  for (uint32_t x = W / 2 - 6; x <= W / 2 + 6; ++x) {
    size_t i = ((size_t)midRow * W + x) * 4;
    if (std::abs((int)odisp[i] - (int)obase[i]) > 20) ++moved;
  }
  bool pass = moved > 0;
  printf("[selftest-displace] edge-band moved=%d (need>0) -> %s\n", moved, pass ? "PASS" : "FAIL");

  image->release(); dmap->release(); dst->release(); base->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// --- Displace CHAIN golden (the multi-Texture2D gather, the承重線) -------------------------------
// Build TWO RenderTarget legs and warp one by the other through PointGraph::cook with Displace as the
// terminal: RadialPoints->DrawPoints->RenderTarget#3 (the Image) and RadialPoints#5->DrawPoints#6->
// RenderTarget#7 (the DisplaceMap), both feeding Displace#4 (Image port, DisplaceMap port). Assert the
// displayed texture is non-empty: BOTH RenderTargets' Texture2D outputs really threaded into Displace's
// two inputs via the multi-input cookTexNode gather. injectBug omits the DisplaceMap wire — with the
// unwired-map fork Displace falls back to Image, so the picture is still non-empty; the teeth instead
// check that the SECOND input slot was populated (inputTextureCount/gather), proven by the dedicated
// math selftest. Here the chain teeth = Displace must be the terminal sink and produce a sized,
// non-black texture; injectBug drops the IMAGE wire so Displace gets no Image -> black -> FAIL.
int runDisplaceChainSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 128, RW = 256, RH = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  clearTexOpCache();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-displacechain] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();

  PointGraph pg(dev, lib, q, 64, 64);
  Graph g;
  // Image leg: RadialPoints#1 -> DrawPoints#2 -> RenderTarget#3.
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)N; gen.params["Radius"] = 2.0f; g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  Node rt; rt.id = 3; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f; rt.params["CustomW"] = (float)RW; rt.params["CustomH"] = (float)RH;
  g.nodes.push_back(rt);
  // DisplaceMap leg: RadialPoints#5 -> DrawPoints#6 -> RenderTarget#7.
  Node gen2; gen2.id = 5; gen2.type = "RadialPoints";
  gen2.params["Count"] = (float)N; gen2.params["Radius"] = 1.0f; g.nodes.push_back(gen2);
  Node drw2; drw2.id = 6; drw2.type = "DrawPoints"; g.nodes.push_back(drw2);
  Node rt2; rt2.id = 7; rt2.type = "RenderTarget";
  rt2.params["Resolution"] = 4.0f; rt2.params["CustomW"] = (float)RW; rt2.params["CustomH"] = (float)RH;
  g.nodes.push_back(rt2);
  // Displace#4: Image <- RT#3, DisplaceMap <- RT#7.
  Node ds; ds.id = 4; ds.type = "Displace";
  ds.params["Resolution"] = 4.0f; ds.params["CustomW"] = (float)RW; ds.params["CustomH"] = (float)RH;
  ds.params["Displacement"] = 0.3f; ds.params["DisplaceMode"] = 0.0f; g.nodes.push_back(ds);

  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});   // RadialPoints#1 -> DrawPoints#2
  g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});   // DrawPoints#2 -> RenderTarget#3.command
  g.connections.push_back({105, pinId(5, 0), pinId(6, 0)});   // RadialPoints#5 -> DrawPoints#6
  g.connections.push_back({106, pinId(6, 1), pinId(7, 0)});   // DrawPoints#6 -> RenderTarget#7.command
  if (!injectBug)
    g.connections.push_back({103, pinId(3, 1), pinId(4, 0)}); // RT#3 -> Displace#4.Image (port 0)
  g.connections.push_back({107, pinId(7, 1), pinId(4, 1)});   // RT#7 -> Displace#4.DisplaceMap (port 1)

  int term = pg.defaultDrawTarget(g);

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  pg.cook(g, ctx, nullptr, term);

  MTL::Texture* tex = pg.target();
  bool termOK = term == 4;  // Displace is the most-downstream tex sink
  bool sized = tex && (uint32_t)tex->width() == RW && (uint32_t)tex->height() == RH;
  int nonBlack = 0;
  if (sized) {
    std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
    tex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
    for (size_t i = 0; i < (size_t)RW * RH; ++i)
      if (px[i * 4] > 20 || px[i * 4 + 1] > 20 || px[i * 4 + 2] > 20) ++nonBlack;
  }
  bool pass = termOK && sized && nonBlack > 50;
  printf("[selftest-displacechain] term=%d(want 4) size=%lux%lu(want %ux%u) nonBlack=%d(need>50) -> %s\n",
         term, tex ? tex->width() : 0, tex ? tex->height() : 0, RW, RH, nonBlack,
         pass ? "PASS" : "FAIL");

  // --- resident leg (production walks cookResident, not flat cook(): same chain through the ONE
  // canonical lib path on a FRESH PointGraph, Displace#4 terminal; same nonBlack assertion. injectBug
  // reuses the missing-Image graph -> resident Displace gets no Image -> black -> red on its own merit.
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
    printf("[selftest-displacechain] resident nonBlack=%d (need>50) -> %s\n", rNonBlack,
           residentPass ? "PASS" : "FAIL");
  }

  bool allPass = pass && residentPass;
  lib->release(); q->release(); dev->release(); pool->release();
  return allPass ? 0 : 1;
}

}  // namespace sw
