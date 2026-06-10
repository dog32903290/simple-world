// RenderTarget texture op (lane A render-target pivot, batch 1) — the THIRD cook flow.
// Executes an upstream RenderCommand (Command stream) into a sized texture: TiXL's
// RenderTarget (external/tixl .../image/generate/basic/RenderTarget.cs). This is the
// RESOLUTION PIN point — Resolution param decides the output texture size; WindowFollow
// tracks the output window (dynamic, no squash), fixed modes pin 16:9 / HD / 4K.
//
// Self-contained leaf: cookRenderTarget + resolveRenderResolution + registerRenderTargetOp()
// + runRenderTargetSelfTest(). Batch 1 lands the op + texture-stream machinery and proves
// it in isolation; the cook() terminal dispatch wires it in batch 2/3 (until then texReg is
// empty in production — zero behavior change, exactly like batch 0's cmd stream).
//
// The draw is faithful to cookDrawPoints (same draw_points pipeline + DRAW_* bindings),
// but loops the RenderCommand's items into ONE render pass: clear once, draw each item.
// That single-pass-N-draws is the payoff of RenderCommand being a data record, not a
// closure (compositing = the executor walks the chain; layers don't clear each other).
#include "runtime/point_ops.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#include "runtime/graph.h"            // Graph/Node
#include "runtime/particle_params.h"  // DRAW_Points, DRAW_ViewExtent
#include "runtime/point_graph.h"      // TexCookCtx, RenderResolution, registerTexOp
#include "runtime/render_command.h"   // RenderCommand / RenderDrawItem
#include "runtime/tixl_point.h"       // SwPoint (64B)

#ifndef SW_SHADER_METALLIB
#define SW_SHADER_METALLIB "shaders.metallib"
#endif

namespace sw {
namespace {

float paramOr(const Node* n, const char* id, float def) {
  if (!n) return def;
  auto it = n->params.find(id);
  return it != n->params.end() ? it->second : def;
}

// RenderTarget draw: build the draw_points pipeline, open one render pass on `output`,
// clear it once, then draw every item in the command chain in order (later items
// composite on top). Builds the PSO per call for now — the live loop (batch 3+) caches it.
void cookRenderTarget(TexCookCtx& c) {
  if (!c.lib || !c.output) return;
  MTL::Function* vs = c.lib->newFunction(NS::String::string("draw_points_vs", NS::UTF8StringEncoding));
  MTL::Function* fs = c.lib->newFunction(NS::String::string("draw_points_fs", NS::UTF8StringEncoding));
  MTL::RenderPipelineState* rps = nullptr;
  if (vs && fs) {
    MTL::RenderPipelineDescriptor* rpd = MTL::RenderPipelineDescriptor::alloc()->init();
    rpd->setVertexFunction(vs);
    rpd->setFragmentFunction(fs);
    rpd->colorAttachments()->object(0)->setPixelFormat(c.output->pixelFormat());
    NS::Error* err = nullptr;
    rps = c.dev->newRenderPipelineState(rpd, &err);
    rpd->release();
  }
  if (vs) vs->release();
  if (fs) fs->release();

  MTL::RenderPassDescriptor* pass = MTL::RenderPassDescriptor::renderPassDescriptor();
  auto* ca = pass->colorAttachments()->object(0);
  ca->setTexture(c.output);
  ca->setLoadAction(MTL::LoadActionClear);
  float cc[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // ClearColor param (Vec4); default black, opaque.
  if (c.graph) {
    if (const Node* n = c.graph->node(c.nodeId)) readVecN(*n, "ClearColor", cc, 4, cc);
  }
  ca->setClearColor(MTL::ClearColor::Make(cc[0], cc[1], cc[2], cc[3]));
  ca->setStoreAction(MTL::StoreActionStore);
  MTL::CommandBuffer* cmd = c.queue->commandBuffer();
  MTL::RenderCommandEncoder* enc = cmd->renderCommandEncoder(pass);
  if (rps && c.command) {
    enc->setRenderPipelineState(rps);
    for (const RenderDrawItem& it : c.command->items) {
      if (!it.points || it.count == 0) continue;
      enc->setVertexBuffer(const_cast<MTL::Buffer*>(it.points), 0, DRAW_Points);
      float viewExtent = it.viewExtent;
      enc->setVertexBytes(&viewExtent, sizeof(float), DRAW_ViewExtent);
      enc->drawPrimitives(MTL::PrimitiveTypePoint, NS::UInteger(0), NS::UInteger(it.count));
    }
  }
  enc->endEncoding();
  cmd->commit();
  cmd->waitUntilCompleted();
  if (rps) rps->release();
}

}  // namespace

// Resolution enum (Float param + Widget::Enum): WindowFollow tracks `windowSize`; the
// fixed modes ignore it and pin a standard output size; Custom reads CustomW/H.
RenderResolution resolveRenderResolution(const Node* n, RenderResolution windowSize) {
  int mode = (int)std::lround(paramOr(n, "Resolution", 0.0f));
  switch (mode) {
    case 1: return {1280, 720};    // HD720
    case 2: return {1920, 1080};   // HD1080
    case 3: return {3840, 2160};   // UHD4K
    case 4: {                      // Custom
      uint32_t w = (uint32_t)std::lround(std::fmax(1.0f, paramOr(n, "CustomW", 512.0f)));
      uint32_t h = (uint32_t)std::lround(std::fmax(1.0f, paramOr(n, "CustomH", 512.0f)));
      return {w, h};
    }
    default: return windowSize;    // WindowFollow (0)
  }
}

void registerRenderTargetOp() { registerTexOp("RenderTarget", cookRenderTarget); }

// Batch 1 golden: drive a CPU-filled point bag through a 1-item RenderCommand into a
// RenderTarget texture, readback, assert lit (non-black). Plus the resolution contract:
// HD1080 -> 1920x1080, WindowFollow -> windowSize. injectBug = 0 points -> all black -> FAIL.
int runRenderTargetSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 64, W = 256, H = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-rendertarget] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerRenderTargetOp();

  // CPU-fill a ring of white points inside the view (radius 1.5 < viewExtent 3.5).
  MTL::Buffer* pts = dev->newBuffer((NS::UInteger)N * sizeof(SwPoint), MTL::ResourceStorageModeShared);
  SwPoint* d = (SwPoint*)pts->contents();
  for (uint32_t i = 0; i < N; ++i) {
    d[i] = SwPoint{};
    float a = 6.2831853f * (float)i / (float)N;
    d[i].Position = {1.5f * std::cos(a), 1.5f * std::sin(a), 0.0f};
    d[i].Color = {1.0f, 1.0f, 1.0f, 1.0f};
    d[i].Scale = {1.0f, 1.0f, 1.0f};
  }

  // Output texture (256² for a cheap readback; resolution contract is checked separately).
  MTL::TextureDescriptor* td =
      MTL::TextureDescriptor::texture2DDescriptor(MTL::PixelFormatRGBA8Unorm, W, H, false);
  td->setUsage(MTL::TextureUsageRenderTarget | MTL::TextureUsageShaderRead);
  td->setStorageMode(MTL::StorageModeShared);
  MTL::Texture* tex = dev->newTexture(td);

  RenderCommand rc;
  rc.items.push_back(RenderDrawItem{pts, injectBug ? 0u : N, 3.5f});

  TexCookCtx c;
  c.dev = dev; c.lib = lib; c.queue = q;
  c.nodeId = 1; c.command = &rc; c.output = tex;
  cookRenderTarget(c);

  std::vector<uint8_t> px((size_t)W * H * 4, 0);
  tex->getBytes(px.data(), W * 4, MTL::Region::Make2D(0, 0, W, H), 0);
  int nonBlack = 0;
  for (size_t i = 0; i < (size_t)W * H; ++i)
    if (px[i * 4] > 30 || px[i * 4 + 1] > 30 || px[i * 4 + 2] > 30) ++nonBlack;

  // Resolution contract (pure function, no giant texture needed).
  Node rt; rt.id = 2; rt.type = "RenderTarget"; rt.params["Resolution"] = 2.0f;  // HD1080
  RenderResolution win{800, 600};
  RenderResolution hd = resolveRenderResolution(&rt, win);
  Node rtw; rtw.id = 3; rtw.type = "RenderTarget"; rtw.params["Resolution"] = 0.0f;  // WindowFollow
  RenderResolution wf = resolveRenderResolution(&rtw, win);
  bool resOK = hd.w == 1920 && hd.h == 1080 && wf.w == 800 && wf.h == 600;

  bool pass = nonBlack > 50 && resOK;
  printf("[selftest-rendertarget] nonBlack=%d(need>50) hd=%ux%u wf=%ux%u resOK=%d -> %s\n",
         nonBlack, hd.w, hd.h, wf.w, wf.h, resOK ? 1 : 0, pass ? "PASS" : "FAIL");

  pts->release(); tex->release();
  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

// Batch 3 golden (the WIRED three-flow): build RadialPoints -> DrawPoints(Command) ->
// RenderTarget(Custom 256x256) and cook it THROUGH PointGraph as the terminal. Proves: (1) the
// tex node wins defaultDrawTarget, (2) cook sizes RenderTarget's own texture to the Resolution
// pin (256x256, not the 64x64 window), (3) the DrawPoints->RenderTarget Command wire threads the
// bag into a lit image. injectBug omits the Command connection -> empty chain -> black -> FAIL.
int runRenderTargetWiredSelfTest(bool injectBug) {
  NS::AutoreleasePool* pool = NS::AutoreleasePool::alloc()->init();
  const uint32_t N = 128, RW = 256, RH = 256;

  MTL::Device* dev = MTL::CreateSystemDefaultDevice();
  MTL::CommandQueue* q = dev->newCommandQueue();
  NS::Error* err = nullptr;
  MTL::Library* lib =
      dev->newLibrary(NS::String::string(SW_SHADER_METALLIB, NS::UTF8StringEncoding), &err);
  if (!lib) {
    printf("[selftest-rendertargetwired] FAIL: no metallib\n");
    q->release(); dev->release(); pool->release();
    return 1;
  }
  registerBuiltinPointOps();  // RadialPoints(cook) + DrawPoints(cmd) + RenderTarget(tex) + ...

  PointGraph pg(dev, lib, q, 64, 64);  // window 64x64; the RenderTarget pins its own 256x256
  Graph g;
  Node gen; gen.id = 1; gen.type = "RadialPoints";
  gen.params["Count"] = (float)N; gen.params["Radius"] = 2.0f; g.nodes.push_back(gen);
  Node drw; drw.id = 2; drw.type = "DrawPoints"; g.nodes.push_back(drw);
  Node rt; rt.id = 3; rt.type = "RenderTarget";
  rt.params["Resolution"] = 4.0f;  // Custom
  rt.params["CustomW"] = (float)RW; rt.params["CustomH"] = (float)RH; g.nodes.push_back(rt);
  g.connections.push_back({101, pinId(1, 0), pinId(2, 0)});  // RadialPoints.points -> DrawPoints.points
  if (!injectBug)
    g.connections.push_back({102, pinId(2, 1), pinId(3, 0)});  // DrawPoints.out(Command) -> RenderTarget.command

  EvaluationContext ctx{};
  ctx.frameIndex = 0; ctx.time = 0.0f; ctx.deltaTime = 1.0f / 60.0f;
  int term = pg.defaultDrawTarget(g);  // tex preferred -> the RenderTarget node (id 3)
  pg.cook(g, ctx, nullptr, term);

  MTL::Texture* tex = pg.target();
  bool sized = tex && (uint32_t)tex->width() == RW && (uint32_t)tex->height() == RH;
  int nonBlack = 0;
  if (sized) {
    std::vector<uint8_t> px((size_t)RW * RH * 4, 0);
    tex->getBytes(px.data(), RW * 4, MTL::Region::Make2D(0, 0, RW, RH), 0);
    for (size_t i = 0; i < (size_t)RW * RH; ++i)
      if (px[i * 4] > 30 || px[i * 4 + 1] > 30 || px[i * 4 + 2] > 30) ++nonBlack;
  }
  bool pass = term == 3 && sized && nonBlack > 50;
  printf("[selftest-rendertargetwired] term=%d(want 3) size=%lux%lu(want %ux%u) nonBlack=%d(need>50) -> %s\n",
         term, tex ? tex->width() : 0, tex ? tex->height() : 0, RW, RH, nonBlack,
         pass ? "PASS" : "FAIL");

  lib->release(); q->release(); dev->release(); pool->release();
  return pass ? 0 : 1;
}

}  // namespace sw
